# frozen_string_literal: true

require 'json'
require 'minitest/autorun'
require 'open3'
require_relative '../lib/power_intent/renderer'

class PowerIntentRendererTest < Minitest::Test
  def test_renders_deterministic_upf_2_1_and_traceable_receipt
    result = render
    upf = result.fetch('upf')
    receipt = result.fetch('receipt')

    assert_equal 'finepaper.noc-power-intent-render-result', result.fetch('format')
    assert_equal 'finepaper.noc-power-intent-render-receipt', receipt.fetch('format')
    assert_includes upf, 'upf_version 2.1'
    assert_includes upf, 'set_design_top "power_top"'
    assert_includes upf, 'create_supply_port "VDD_MAIN"'
    assert_includes upf, 'create_supply_net "VDD_SW"'
    refute_includes upf, 'create_supply_port "VDD_SW"'
    assert_includes upf, 'create_power_domain "pd_a"'
    assert_includes upf, 'create_supply_set "ss_'
    assert_includes upf, 'associate_supply_set "ss_'
    assert_includes upf, 'add_power_state "ss_'
    assert_includes upf, 'create_power_switch "sw_b"'
    assert_includes upf, 'set_retention "ret_b"'
    assert_includes upf, 'set_retention_control "ret_b"'
    assert_includes upf, 'set_isolation "iso_b_to_a"'
    assert_includes upf, 'set_level_shifter "ls_b_to_a"'
    assert_includes upf, 'use_interface_cell "uic_'
    assert_includes upf, 'map_retention_cell "ret_b"'
    assert_includes upf, 'map_power_switch_cell "sw_b"'

    assert_equal 'emitted', receipt.dig('technology', 'bindingStatus')
    binding_command_count = receipt.fetch('commands').count do |command|
      %w[
        map_power_switch_cell
        map_retention_cell
        use_interface_cell
      ].include?(command.fetch('kind'))
    end
    assert_operator binding_command_count, :>, 0
    assert_equal binding_command_count,
                 receipt.dig('technology', 'bindingCommandCount')
    assert_equal 'not-performed',
                 receipt.dig('validation', 'commercialSemanticValidation')
    assert_equal receipt.fetch('commands').count { |entry| entry.fetch('emitted') },
                 receipt.fetch('commandCount')
    isolation = strategy(receipt, 'isolation', 'isolate b output; [set ::pwned 1]')
    assert_equal 'emitted', isolation.fetch('renderStatus')
    assert_equal %w[create_supply_set set_isolation use_interface_cell],
                 isolation.fetch('commandKinds')
    deferred = strategy(receipt, 'isolation', 'combined cdc power')
    assert_equal 'deferred', deferred.fetch('renderStatus')
    assert_empty deferred.fetch('commandKinds')
    assert_equal 'run', receipt.dig('systemStates', 'defaultSystemState')
    assert_equal 'receipt-only', receipt.dig('systemStates', 'status')
    assert_equal %w[run sleep],
                 receipt.dig('systemStates', 'vectors').map { |entry| entry.fetch('id') }
    assert result.frozen?
    assert receipt.frozen?
    assert receipt.fetch('commands').first.frozen?
  end

  def test_reordered_equivalent_plan_is_byte_deterministic
    expected = render
    reordered = deep_copy(implementation_plan)
    %w[
      supplies controls domains powerSwitches retentions edgeOrientations
      systemStates
    ].each { |key| reordered.fetch(key).reverse! }
    reordered.fetch('supplies').each { |supply| supply.fetch('states').reverse! }
    reordered.fetch('domains').each do |domain|
      domain.fetch('states').reverse!
      domain.fetch('elements').reverse!
    end
    reordered.fetch('edgeOrientations').each do |orientation|
      orientation.fetch('signalFlows').reverse!
    end
    reordered.fetch('systemStates').each do |state|
      state.fetch('domainStates').reverse!
    end
    reordered.dig('technology', 'interfaceCells').reverse!
    reordered.dig('technology', 'interfaceCells').each do |mapping|
      mapping.fetch('cells').reverse!
    end

    actual = render(reordered)
    assert_equal expected.fetch('upf'), actual.fetch('upf')
    assert_equal JSON.generate(expected.fetch('receipt')),
                 JSON.generate(actual.fetch('receipt'))
  end

  def test_abstract_profile_emits_policy_but_never_claims_cell_binding
    plan = deep_copy(implementation_plan)
    plan['technology'] = {'profile' => 'abstract', 'interfaceCells' => []}
    plan.fetch('powerSwitches').each do |entry|
      entry['technologyCellMappingId'] = nil
    end
    plan.fetch('retentions').each do |entry|
      entry['technologyCellMappingId'] = nil
    end
    plan.fetch('edgeOrientations').each do |orientation|
      orientation.fetch('signalFlows').each do |flow|
        %w[isolation levelShifter].each do |key|
          flow[key]['technologyCellMappingId'] = nil if flow[key]
        end
      end
    end

    result = render(plan)
    upf = result.fetch('upf')
    receipt = result.fetch('receipt')
    assert_includes upf, 'set_isolation'
    assert_includes upf, 'set_level_shifter'
    assert_includes upf, 'set_retention'
    assert_includes upf, 'create_power_switch'
    refute_includes upf, 'use_interface_cell'
    refute_includes upf, 'map_retention_cell'
    refute_includes upf, 'map_power_switch_cell'
    assert_equal 'abstract-unbound', receipt.dig('technology', 'bindingStatus')
    assert_equal 0, receipt.dig('technology', 'bindingCommandCount')
  end

  def test_deferred_cdc_power_flow_never_emits_a_strategy
    result = render
    upf = result.fetch('upf')

    refute_includes upf, 'iso_deferred'
    refute_includes upf, 'ls_deferred'
    deferred_commands = result.dig('receipt', 'commands').select do |entry|
      entry.fetch('itemId') == 'combined cdc power'
    end
    assert_empty deferred_commands
  end

  def test_expected_strategy_cannot_emit_from_a_deferred_flow
    plan = deep_copy(implementation_plan)
    plan.fetch('edgeOrientations').first.fetch('signalFlows').first['status'] =
      'deferred'

    error = assert_raises(FinepaperNoc::PowerIntent::Renderer::Error) do
      render(plan)
    end
    assert_equal 'power_renderer.strategy_on_non_emittable_parent', error.code
    assert_match(%r{/isolation/status\z}, error.path)
  end

  def test_tclsh_stub_checks_tcl_syntax_and_required_argument_shape_only
    result = render
    stdout, stderr, status = Open3.capture3(
      'tclsh', stdin_data: "#{tcl_stub}\n#{result.fetch('upf')}\n" \
                           "puts \"CALLS=[llength $::calls]\"\n" \
                           "puts \"PWNED=$::pwned\"\n"
    )

    assert status.success?, "tclsh stub smoke failed:\n#{stderr}\n#{stdout}"
    assert_match(/CALLS=\d+/, stdout)
    assert_includes stdout, 'PWNED=0'
    assert_empty stderr
  end

  def test_upf_port_policies_reference_the_connected_logic_net
    plan = deep_copy(implementation_plan)
    %w[isolation\ control save\ control restore\ control].each do |id|
      plan.fetch('controls').find { |entry| entry.fetch('id') == id }['source'] =
        'upf-port'
    end
    plan.dig('hierarchyFacts', 'logicControlPorts').clear

    result = render(plan)
    receipt = result.fetch('receipt')
    expected_nets = %w[
      switch\ control isolation\ control save\ control restore\ control
    ].to_h { |id| [id, generated_logic_net(id)] }
    expected_nets.each do |id, net|
      creation = receipt.fetch('commands').find do |entry|
        entry.fetch('kind') == 'create_logic_net' && entry.fetch('itemId') == id
      end
      refute_nil creation, "missing logic net creation for #{id}"
      assert_includes creation.fetch('arguments'), %Q{"#{net}"}
    end

    switch_command = receipt.fetch('commands').find do |entry|
      entry.fetch('kind') == 'create_power_switch'
    end
    isolation_command = receipt.fetch('commands').find do |entry|
      entry.fetch('kind') == 'set_isolation'
    end
    retention_command = receipt.fetch('commands').find do |entry|
      entry.fetch('kind') == 'set_retention_control'
    end
    assert_includes switch_command.fetch('arguments').join(' '),
                    expected_nets.fetch('switch control')
    assert_includes isolation_command.fetch('arguments').join(' '),
                    expected_nets.fetch('isolation control')
    assert_includes retention_command.fetch('arguments').join(' '),
                    expected_nets.fetch('save control')
    assert_includes retention_command.fetch('arguments').join(' '),
                    expected_nets.fetch('restore control')
    refute_includes switch_command.fetch('arguments').join(' '), 'power_enable_n'
    refute_includes isolation_command.fetch('arguments').join(' '), 'isolate_b'
    refute_includes retention_command.fetch('arguments').join(' '), 'save_b'
    refute_includes retention_command.fetch('arguments').join(' '), 'restore_b'
  end

  def test_opaque_ids_are_comment_sanitized_and_command_tokens_fail_closed
    result = render
    upf = result.fetch('upf')

    refute_includes upf, '; [set ::pwned 1]'
    strategy_ids = result.dig('receipt', 'strategyCoverage').map do |entry|
      entry.fetch('id')
    end
    assert_includes strategy_ids, 'isolate b output; [set ::pwned 1]'

    plan = deep_copy(implementation_plan)
    plan.fetch('edgeOrientations').first.fetch('signalFlows').first
        .fetch('isolation')['token'] = 'iso; set ::pwned 1'
    error = assert_raises(FinepaperNoc::PowerIntent::Renderer::Error) do
      render(plan)
    end
    assert_equal 'power_renderer.invalid_identifier', error.code
    assert_match(%r{/isolation/token\z}, error.path)
  end

  def test_expected_strategy_requires_complete_renderer_facing_parameters
    plan = deep_copy(implementation_plan)
    isolation = plan.fetch('edgeOrientations').first.fetch('signalFlows').first
                    .fetch('isolation')
    isolation.delete('clampValue')

    error = assert_raises(FinepaperNoc::PowerIntent::Renderer::Error) do
      render(plan)
    end
    assert_equal 'power_renderer.missing_field', error.code
    assert_match(%r{/isolation/clampValue\z}, error.path)
  end

  def test_source_contract_formats_and_versions_are_exact
    cases = [
      [lambda do |plan|
        plan.dig('sourceContracts', 'domainImplementationPlan')['format'] =
          'finepaper.trick'
      end, 'power_renderer.invalid_source_contract'],
      [lambda do |plan|
        plan.dig('sourceContracts', 'powerIntentPlan')['formatVersion'] = 2
      end, 'power_renderer.invalid_source_contract_version'],
      [lambda do |plan|
        plan.dig('sourceContracts', 'rtlHierarchy')['format'] =
          'finepaper.noc-domain-implementation-plan'
      end, 'power_renderer.invalid_source_contract'],
      [lambda do |plan|
        plan.fetch('hierarchyFacts')['formatVersion'] = 2
      end, 'power_renderer.invalid_hierarchy_contract_version']
    ]

    cases.each do |mutation, code|
      plan = deep_copy(implementation_plan)
      mutation.call(plan)
      error = assert_raises(FinepaperNoc::PowerIntent::Renderer::Error) do
        render(plan)
      end
      assert_equal code, error.code
    end
  end

  def test_recipe_stage_order_is_preserved_and_must_be_non_negative
    plan = deep_copy(implementation_plan)
    recipe = plan.fetch('edgeOrientations').first.fetch('signalFlows').first
                 .fetch('isolation').fetch('recipes').first
    recipe['order'] = 200
    receipt = render(plan).fetch('receipt')
    rendered = strategy(
      receipt, 'isolation', 'isolate b output; [set ::pwned 1]'
    )
    assert_equal 200, rendered.dig('recipes', 0, 'order')

    recipe['order'] = -1
    error = assert_raises(FinepaperNoc::PowerIntent::Renderer::Error) do
      render(plan)
    end
    assert_equal 'power_renderer.invalid_recipe_order', error.code
    assert_match(%r{/recipes/0/order\z}, error.path)
  end

  def test_hierarchy_logic_controls_are_an_exact_top_port_bijection
    cases = [
      [lambda do |plan|
        plan.dig('hierarchyFacts', 'logicControlPorts').pop
      end, 'power_renderer.logic_control_bijection_mismatch'],
      [lambda do |plan|
        plan.dig('hierarchyFacts', 'logicControlPorts') << {
          'id' => 'switch control', 'signal' => 'power_enable_n',
          'source' => 'top-port', 'direction' => 'input'
        }
      end, 'power_renderer.logic_control_bijection_mismatch'],
      [lambda do |plan|
        plan.dig('hierarchyFacts', 'logicControlPorts').first['signal'] =
          'wrong_isolation_signal'
      end, 'power_renderer.logic_control_mismatch']
    ]

    cases.each do |mutation, code|
      plan = deep_copy(implementation_plan)
      mutation.call(plan)
      error = assert_raises(FinepaperNoc::PowerIntent::Renderer::Error) do
        render(plan)
      end
      assert_equal code, error.code
    end
  end

  def test_internal_switched_supply_requires_exactly_one_switch_driver
    plan = deep_copy(implementation_plan)
    plan.fetch('powerSwitches').clear

    error = assert_raises(FinepaperNoc::PowerIntent::Renderer::Error) do
      render(plan)
    end
    assert_equal 'power_renderer.invalid_internal_driver', error.code
    assert_match(%r{/supplies/\d+/exposure\z}, error.path)
  end

  def test_deferred_or_not_required_active_items_never_emit_upf
    cases = [
      ['supply', lambda { |plan| plan.fetch('supplies').first }, 'deferred'],
      ['control', lambda { |plan| plan.fetch('controls').first }, 'not-required'],
      ['Domain', lambda { |plan| plan.fetch('domains').first }, 'deferred'],
      ['switch', lambda { |plan| plan.fetch('powerSwitches').first }, 'deferred'],
      ['retention', lambda { |plan| plan.fetch('retentions').first }, 'not-required']
    ]

    cases.each do |label, selector, status|
      plan = deep_copy(implementation_plan)
      selector.call(plan)['status'] = status
      error = assert_raises(FinepaperNoc::PowerIntent::Renderer::Error, label) do
        render(plan)
      end
      assert_equal 'power_renderer.non_emittable_item', error.code, label
      assert_match(%r{/status\z}, error.path, label)
    end
  end

  def test_render_result_does_not_alias_or_freeze_caller_input
    plan = deep_copy(implementation_plan)
    original_id = plan.fetch('powerSwitches').first.fetch('id').dup
    deferred = plan.fetch('edgeOrientations').last.fetch('signalFlows').first
                   .fetch('isolation')
    deferred_status = deferred.fetch('status')
    deferred_reason = deferred.fetch('reason')
    deferred_recipes = deferred.fetch('recipes')
    original_status = deferred_status.dup
    original_reason = deferred_reason.dup
    original_recipe = deferred_recipes.first.fetch('recipe').dup
    result = render(plan)

    refute plan.fetch('powerSwitches').first.fetch('id').frozen?
    refute deferred_status.frozen?
    refute deferred_reason.frozen?
    refute deferred_recipes.frozen?
    refute deferred_recipes.first.frozen?
    refute deferred_recipes.first.fetch('recipe').frozen?
    plan.fetch('powerSwitches').first.fetch('id') << '_changed'
    deferred_status << '_changed'
    deferred_reason << '_changed'
    deferred_recipes.first.fetch('recipe') << '_changed'
    receipt_ids = result.dig('receipt', 'commands').map { |entry| entry.fetch('itemId') }
    assert_includes receipt_ids, original_id
    refute_includes receipt_ids, plan.fetch('powerSwitches').first.fetch('id')
    deferred_receipt = strategy(
      result.fetch('receipt'), 'isolation', 'combined cdc power'
    )
    assert_equal original_status, deferred_receipt.fetch('sourceStatus')
    assert_equal original_reason, deferred_receipt.fetch('reasonCode')
    assert_equal original_recipe,
                 deferred_receipt.dig('recipes', 0, 'recipe')
  end

  private

  def render(plan = implementation_plan)
    FinepaperNoc::PowerIntent::Renderer.render(plan: plan)
  end

  def strategy(receipt, kind, id)
    receipt.fetch('strategyCoverage').find do |entry|
      entry.fetch('kind') == kind && entry.fetch('id') == id
    end || flunk("missing #{kind} strategy #{id}")
  end

  def deep_copy(value)
    JSON.parse(JSON.generate(value))
  end

  def implementation_plan
    {
      'format' => 'finepaper.noc-power-implementation-plan',
      'formatVersion' => 1,
      'design' => 'power fixture; [set ::pwned 1]',
      'topModule' => 'power_top',
      'sourceContracts' => {
        'domainImplementationPlan' => {
          'format' => 'finepaper.noc-domain-implementation-plan',
          'formatVersion' => 1
        },
        'powerIntentPlan' => {
          'format' => 'finepaper.noc-power-intent-plan', 'formatVersion' => 1
        },
        'rtlHierarchy' => {
          'format' => 'finepaper.noc-rtl-hierarchy', 'formatVersion' => 1
        }
      },
      'hierarchyFacts' => {
        'format' => 'finepaper.noc-rtl-hierarchy', 'formatVersion' => 1,
        'topArtifact' => 'power_top.sv', 'resetSynchronizers' => [],
        'logicControlPorts' => [
          {
            'id' => 'isolation control', 'signal' => 'isolate_b',
            'source' => 'top-port', 'direction' => 'input'
          },
          {
            'id' => 'save control', 'signal' => 'save_b',
            'source' => 'top-port', 'direction' => 'input'
          },
          {
            'id' => 'restore control', 'signal' => 'restore_b',
            'source' => 'top-port', 'direction' => 'input'
          }
        ]
      },
      'supplies' => [
        supply('main rail', 'sup_main', 'power', 'external-port', 'VDD_MAIN',
               [{'id' => 'on', 'condition' => 'full-on', 'voltageMv' => 1000}],
               port: 'VDD_MAIN'),
        supply('low rail', 'sup_low', 'power', 'external-port', 'VDD_LOW',
               [{'id' => 'on', 'condition' => 'full-on', 'voltageMv' => 800}],
               port: 'VDD_LOW'),
        supply('switched rail', 'sup_sw', 'power', 'internal-switched', 'VDD_SW',
               [
                 {'id' => 'on', 'condition' => 'full-on', 'voltageMv' => 800},
                 {'id' => 'off', 'condition' => 'off'}
               ]),
        supply('ground rail', 'sup_ground', 'ground', 'external-port', 'VSS',
               [{'id' => 'on', 'condition' => 'full-on', 'voltageMv' => 0}],
               port: 'VSS')
      ],
      'controls' => [
        control('switch control', 'ctl_switch', 'power_enable_n', 'upf-port', 'low'),
        control('isolation control', 'ctl_isolate', 'isolate_b', 'top-port', 'high'),
        control('save control', 'ctl_save', 'save_b', 'top-port', 'high'),
        control('restore control', 'ctl_restore', 'restore_b', 'top-port', 'high')
      ],
      'domains' => [
        {
          'domain' => 'power-a', 'token' => 'pd_a', 'elements' => %w[u_a],
          'name' => 'Always on', 'mode' => 'always-on',
          'primaryPower' => 'main rail', 'primaryGround' => 'ground rail',
          'defaultState' => 'run',
          'parameters' => {},
          'elementBindings' => [
            {
              'element' => {'kind' => 'router', 'id' => 'a'},
              'module' => 'router_node', 'instance' => 'u_a'
            }
          ],
          'status' => 'expected', 'reason' => 'active Domain', 'recipes' => [],
          'states' => [
            {
              'id' => 'run', 'token' => 'ps_a_run',
              'powerState' => 'on', 'groundState' => 'on',
              'behavior' => 'operational'
            }
          ]
        },
        {
          'domain' => 'power-b', 'token' => 'pd_b', 'elements' => %w[u_b],
          'name' => 'Switchable', 'mode' => 'switchable',
          'primaryPower' => 'switched rail', 'primaryGround' => 'ground rail',
          'defaultState' => 'run',
          'parameters' => {},
          'elementBindings' => [
            {
              'element' => {'kind' => 'router', 'id' => 'b'},
              'module' => 'router_node', 'instance' => 'u_b'
            }
          ],
          'status' => 'expected', 'reason' => 'active Domain', 'recipes' => [],
          'states' => [
            {
              'id' => 'run', 'token' => 'ps_b_run',
              'powerState' => 'on', 'groundState' => 'on',
              'behavior' => 'operational'
            },
            {
              'id' => 'sleep', 'token' => 'ps_b_sleep',
              'powerState' => 'off', 'groundState' => 'on',
              'behavior' => 'retained'
            }
          ]
        }
      ],
      'powerSwitches' => [
        {
          'id' => 'switch b', 'token' => 'sw_b', 'domain' => 'power-b',
          'inputSupply' => 'low rail', 'outputSupply' => 'switched rail',
          'control' => 'switch control', 'onSense' => 'low',
          'technologyCellMappingId' => 'switch mapping',
          'status' => 'expected', 'reason' => 'switchable Domain',
          'recipes' => recipes('power-switch')
        }
      ],
      'retentions' => [
        {
          'id' => 'retain b', 'token' => 'ret_b', 'domain' => 'power-b',
          'supply' => 'main rail', 'saveControl' => 'save control',
          'restoreControl' => 'restore control', 'saveEdge' => 'posedge',
          'restoreEdge' => 'posedge', 'location' => 'self',
          'technologyCellMappingId' => 'retention mapping',
          'status' => 'expected', 'reason' => 'retained state',
          'recipes' => recipes('power-retention')
        }
      ],
      'edgeOrientations' => [
        {
          'id' => 'edge/0/reverse', 'token' => 'edge_0_reverse',
          'edge' => {'kind' => 'link', 'id' => 'edge/0'},
          'orientation' => 'reverse',
          'producer' => {'kind' => 'router', 'id' => 'b'},
          'consumer' => {'kind' => 'router', 'id' => 'a'},
          'sourceSupplyDomain' => 'power-b',
          'destinationSupplyDomain' => 'power-a',
          'powerBoundary' => {'status' => 'resolvable'},
          'status' => 'expected', 'reason' => 'direct power crossing',
          'recipes' => recipes('power-isolation', 'power-level-shifter'),
          'signalFlows' => [resolved_flow]
        },
        {
          'id' => 'edge/1/forward', 'token' => 'edge_1_forward',
          'edge' => {'kind' => 'link', 'id' => 'edge/1'},
          'orientation' => 'forward',
          'producer' => {'kind' => 'router', 'id' => 'bridge-source'},
          'consumer' => {'kind' => 'router', 'id' => 'a'},
          'sourceSupplyDomain' => nil,
          'destinationSupplyDomain' => 'power-a',
          'powerBoundary' => {
            'status' => 'deferred',
            'reasonCode' => 'rtl_hierarchy.infrastructure_bridge_supply_unowned'
          },
          'status' => 'deferred', 'reason' => 'bridge supply is unowned',
          'recipes' => recipes('power-isolation', 'power-level-shifter'),
          'signalFlows' => [deferred_flow]
        }
      ],
      'defaultSystemState' => 'run',
      'systemStates' => [
        {
          'id' => 'run',
          'domainStates' => [
            {'domain' => 'power-a', 'state' => 'run'},
            {'domain' => 'power-b', 'state' => 'run'}
          ]
        },
        {
          'id' => 'sleep',
          'domainStates' => [
            {'domain' => 'power-a', 'state' => 'run'},
            {'domain' => 'power-b', 'state' => 'sleep'}
          ]
        }
      ],
      'technology' => {
        'profile' => 'upf-interface-cells',
        'interfaceCells' => [
          {
            'id' => 'isolation mapping', 'kind' => 'isolation',
            'cells' => %w[LIB.ISO_B LIB.ISO_A]
          },
          {
            'id' => 'level up mapping', 'kind' => 'level-shifter',
            'direction' => 'up', 'cells' => %w[LIB.LS_UP]
          },
          {
            'id' => 'retention mapping', 'kind' => 'retention',
            'cells' => %w[LIB.RET]
          },
          {
            'id' => 'switch mapping', 'kind' => 'power-switch',
            'cells' => %w[LIB.SWITCH]
          }
        ]
      },
      'inactiveIntent' => {'domains' => [], 'supplies' => [], 'controls' => []},
      'coverage' => {
        'complete' => false,
        'summary' => {
          'expected' => 0, 'notRequired' => 0, 'deferred' => 1, 'total' => 1
        },
        'items' => [
          {
            'id' => 'shutdown sequencing', 'kind' => 'shutdown-sequencing',
            'subject' => '/domains/power-b', 'status' => 'deferred',
            'reason' => 'sequencing is not implemented',
            'recipes' => recipes('power-shutdown-sequencing')
          }
        ]
      }
    }
  end

  def resolved_flow
    {
      'id' => 'b-to-a payload', 'token' => 'flow_b_to_a', 'net' => 'b_to_a_data',
      'type' => 'payload', 'side' => 'direct',
      'direction' => 'producer-to-consumer',
      'driver' => {'instance' => 'u_b', 'pin' => 'out_data', 'domain' => 'power-b'},
      'receiver' => {'instance' => 'u_a', 'pin' => 'in_data', 'domain' => 'power-a'},
      'powerBoundary' => {'status' => 'resolvable'},
      'status' => 'expected', 'reason' => 'direct power crossing',
      'recipes' => recipes('power-isolation', 'power-level-shifter'),
      'isolation' => {
        'id' => 'isolate b output; [set ::pwned 1]', 'token' => 'iso_b_to_a',
        'status' => 'expected', 'domain' => 'power-b',
        'elements' => %w[u_b/out_data], 'appliesTo' => 'outputs',
        'clampValue' => 0, 'isolationControl' => 'isolation control',
        'isolationSense' => 'high', 'supply' => 'main rail',
        'location' => 'parent',
        'technologyCellMappingId' => 'isolation mapping',
        'reason' => 'source may power off',
        'recipes' => recipes('power-isolation')
      },
      'levelShifter' => {
        'id' => 'shift b output', 'token' => 'ls_b_to_a',
        'status' => 'expected', 'domain' => 'power-b',
        'elements' => %w[u_b/out_data], 'appliesTo' => 'outputs',
        'rule' => 'low_to_high', 'location' => 'self',
        'technologyCellMappingId' => 'level up mapping',
        'reason' => 'voltage increases',
        'recipes' => recipes('power-level-shifter')
      }
    }
  end

  def deferred_flow
    {
      'id' => 'combined cdc power', 'token' => 'flow_deferred',
      'net' => 'fifo_payload',
      'type' => 'payload', 'side' => 'destination',
      'direction' => 'producer-to-consumer',
      'driver' => {'instance' => 'u_fifo', 'pin' => 'dst_data', 'domain' => nil},
      'receiver' => {'instance' => 'u_a', 'pin' => 'in_fifo', 'domain' => 'power-a'},
      'powerBoundary' => {
        'status' => 'deferred',
        'reasonCode' => 'rtl_hierarchy.infrastructure_bridge_supply_unowned'
      },
      'status' => 'deferred', 'reason' => 'bridge supply is unowned',
      'recipes' => recipes('power-isolation', 'power-level-shifter'),
      'isolation' => {
        'id' => 'combined cdc power', 'token' => 'iso_deferred',
        'domain' => nil, 'elements' => [], 'appliesTo' => nil,
        'technologyCellMappingId' => nil, 'status' => 'deferred',
        'reason' => 'bridge supply is unowned',
        'recipes' => recipes('power-isolation')
      },
      'levelShifter' => {
        'id' => 'combined cdc power', 'token' => 'ls_deferred',
        'domain' => nil, 'elements' => [], 'appliesTo' => nil,
        'technologyCellMappingId' => nil, 'status' => 'deferred',
        'reason' => 'bridge supply is unowned',
        'recipes' => recipes('power-level-shifter')
      }
    }
  end

  def supply(id, token, kind, exposure, net, states, port: nil)
    states.each_with_index do |state, index|
      state['token'] ||= "#{token}_state_#{index}"
    end
    {
      'id' => id, 'token' => token, 'kind' => kind, 'exposure' => exposure,
      'net' => net, 'states' => states, 'status' => 'expected',
      'reason' => 'active supply', 'recipes' => []
    }.tap { |entry| entry['port'] = port if port }
  end

  def control(id, token, signal, source, active_sense)
    {
      'id' => id, 'token' => token, 'signal' => signal, 'source' => source,
      'activeSense' => active_sense, 'status' => 'expected',
      'reason' => 'referenced control', 'recipes' => []
    }
  end

  def recipes(*ids)
    ids.map { |id| {'recipe' => id} }
  end

  def generated_logic_net(id)
    "ln_#{Digest::SHA256.hexdigest("logic_net\0#{id}")}"
  end

  def tcl_stub
    <<~'TCL'
      set ::calls {}
      set ::pwned 0

      proc require_options {name args options} {
        foreach option $options {
          if {[lsearch -exact $args $option] < 0} {
            error "$name is missing required option $option"
          }
        }
      }

      proc upf_dispatch {name args} {
        switch -- $name {
          upf_version - set_design_top - create_supply_port - create_supply_net -
          create_logic_net {
            if {[llength $args] != 1} { error "$name expects one argument" }
          }
          connect_supply_net - connect_logic_net {
            require_options $name $args {-ports}
          }
          create_logic_port {
            require_options $name $args {-direction}
          }
          create_power_domain {
            require_options $name $args {-elements}
          }
          create_supply_set {
            require_options $name $args {-function}
            if {[llength $args] < 5} { error "$name needs power and ground functions" }
          }
          associate_supply_set {
            require_options $name $args {-handle}
          }
          add_power_state {
            require_options $name $args {-state}
          }
          create_power_switch {
            require_options $name $args {
              -domain -input_supply_port -output_supply_port -control_port
              -on_state -off_state
            }
          }
          set_retention {
            require_options $name $args {-domain -retention_supply_set -location}
          }
          set_retention_control {
            require_options $name $args {-domain -save_signal -restore_signal}
          }
          set_isolation {
            require_options $name $args {
              -domain -elements -applies_to -clamp_value -isolation_signal
              -isolation_sense -isolation_supply_set -location
            }
          }
          set_level_shifter {
            require_options $name $args {-domain -elements -applies_to -rule -location}
          }
          use_interface_cell {
            require_options $name $args {-domain -strategy -lib_cells}
          }
          map_retention_cell - map_power_switch_cell {
            require_options $name $args {-domain -lib_cells}
          }
          default { error "unexpected command $name" }
        }
        lappend ::calls [linsert $args 0 $name]
      }

      foreach name {
        upf_version set_design_top create_supply_port create_supply_net
        connect_supply_net create_logic_port create_logic_net connect_logic_net
        create_power_domain create_supply_set associate_supply_set add_power_state
        create_power_switch set_retention set_retention_control set_isolation
        set_level_shifter use_interface_cell map_retention_cell
        map_power_switch_cell
      } {
        interp alias {} $name {} upf_dispatch $name
      }
    TCL
  end
end
