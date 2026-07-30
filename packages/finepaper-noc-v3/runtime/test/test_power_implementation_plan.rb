# frozen_string_literal: true

require 'json'
require 'minitest/autorun'
require_relative '../lib/power_intent/compiler'
require_relative '../lib/power_intent/implementation_plan'
require_relative '../lib/rtl_hierarchy_manifest'

class PowerImplementationPlanTest < Minitest::Test
  def test_builds_renderer_contract_with_actual_ready_driver_and_coverage
    context, power_plan, hierarchy = fixture
    plan = build(context, power_plan, hierarchy)

    assert_equal 'finepaper.noc-power-implementation-plan', plan.fetch('format')
    assert_equal 1, plan.fetch('formatVersion')
    assert_equal 'power_ir_fixture_top', plan.fetch('topModule')
    assert_equal %w[power-always power-switch],
                 plan.fetch('domains').map { |domain| domain.fetch('domain') }
    assert_equal ['power-unused'], plan.dig('inactiveIntent', 'domains')
    assert_equal ['vdd_unused'], plan.dig('inactiveIntent', 'supplies')
    assert_equal ['unused'], plan.dig('inactiveIntent', 'controls')
    assert_equal %w[isolate restore save switch],
                 plan.fetch('controls').map { |control| control.fetch('id') }

    switch = plan.fetch('powerSwitches').fetch(0)
    retention = plan.fetch('retentions').fetch(0)
    assert_equal 'switch_cell', switch.fetch('technologyCellMappingId')
    assert_equal 'retention_cell',
                 retention.fetch('technologyCellMappingId')
    assert_equal 'posedge', retention.fetch('saveEdge')
    assert_equal 'negedge', retention.fetch('restoreEdge')

    forward = edge_direction(plan, 'from-to')
    payload = signal_flow(forward, 'direct', 'payload')
    ready = signal_flow(forward, 'direct', 'ready')
    assert_equal 'power-always', payload.dig('driver', 'domain')
    assert_equal 'power-switch', ready.dig('driver', 'domain')
    assert_equal 'high_to_low', payload.dig('levelShifter', 'rule')
    assert_equal 'low_to_high', ready.dig('levelShifter', 'rule')
    assert_equal 'not-required', payload.dig('isolation', 'status')
    assert_equal 'expected', ready.dig('isolation', 'status')
    assert_equal 'isolate', ready.dig('isolation', 'isolationControl')
    assert_equal ['u_router_right/in_ready_o'],
                 ready.dig('isolation', 'elements')
    assert_equal 'outputs', ready.dig('isolation', 'appliesTo')
    assert_equal 'ls_up_cell',
                 ready.dig('levelShifter', 'technologyCellMappingId')

    refute plan.dig('coverage', 'complete')
    reasons = plan.dig('coverage', 'items').map { |item| item.fetch('reason') }
    assert_includes reasons,
                    'power_implementation.shutdown_sequence_unmaterialized'
    assert_includes reasons,
                    'power_implementation.router_power_state_routing_unverified'
    assert_includes reasons,
                    'power_implementation.system_states_receipt_only'
    assert_includes reasons,
                    'power_implementation.reset_synchronizer_supply_unowned'
    ownership = plan.dig('coverage', 'items').select do |item|
      item.fetch('kind') == 'infrastructure-supply-ownership'
    end
    assert_equal hierarchy.fetch('resetSynchronizers').size, ownership.size
    assert ownership.all? { |item| item.fetch('status') == 'deferred' }
    assert_all_coverage_subjects_resolve(plan)
    assert plan.frozen?
    assert plan.dig('edgeOrientations', 0, 'signalFlows', 0).frozen?
    assert_canonical_hashes(plan)
  end

  def test_combined_clock_and_power_boundary_is_explicitly_deferred
    context, power_plan, hierarchy = fixture(cdc: true)
    plan = build(context, power_plan, hierarchy)

    plan.fetch('edgeOrientations').each do |edge|
      assert_equal 'deferred', edge.dig('powerBoundary', 'status')
      edge.fetch('signalFlows').each do |flow|
        assert_equal 'deferred', flow.fetch('status')
        assert_equal(
          'rtl_hierarchy.infrastructure_bridge_supply_unowned',
          flow.fetch('reason')
        )
        assert_nil flow.dig('driver', 'domain') if
          flow.dig('driver', 'instance').start_with?('u_fifo_')
        assert_nil flow.dig('receiver', 'domain') if
          flow.dig('receiver', 'instance').start_with?('u_fifo_')
        %w[isolation levelShifter].each do |strategy|
          assert_equal 'deferred', flow.dig(strategy, 'status')
          assert_equal(
            'rtl_hierarchy.infrastructure_bridge_supply_unowned',
            flow.dig(strategy, 'reason')
          )
        end
      end
    end
    ownership = plan.dig('coverage', 'items').select do |item|
      item.fetch('kind') == 'infrastructure-supply-ownership'
    end
    expected_count = hierarchy.fetch('resetSynchronizers').size +
                     hierarchy.fetch('edgeDirections').count do |edge|
                       edge['bridge']
                     end
    assert_equal expected_count, ownership.size
    bridge_ownership = ownership.select do |item|
      item.fetch('reason') ==
        'rtl_hierarchy.infrastructure_bridge_supply_unowned'
    end
    assert_equal 2, bridge_ownership.size
    assert bridge_ownership.all? do |item|
      item.fetch('subject').start_with?('/edgeOrientations/')
    end
    assert_all_coverage_subjects_resolve(plan)
  end

  def test_same_supply_cdc_still_defers_unowned_bridge_supply
    context_plan = implementation_plan(cdc: true)
    right = reference('router', 'r1')
    always_on = context_plan.fetch('domainBindings').find do |domain|
      domain.fetch('domain') == 'power-always'
    end
    switchable = context_plan.fetch('domainBindings').find do |domain|
      domain.fetch('domain') == 'power-switch'
    end
    always_on.fetch('members') << right
    switchable['members'] = []
    context_plan.fetch('entityBindings').find do |entry|
      entry.fetch('element') == right
    end.fetch('bindings').find do |binding|
      binding.fetch('role') == 'supply-domain'
    end['domain'] = 'power-always'
    edge = context_plan.fetch('edgeBindings').fetch(0)
    edge.fetch('toBindings').find do |binding|
      binding.fetch('role') == 'supply-domain'
    end['domain'] = 'power-always'
    edge['stages'] = edge.fetch('stages').select do |stage|
      stage.fetch('domainType') == 'clock'
    end
    context = FinepaperNoc::DomainRtlContext.new(context_plan)
    document = power_intent
    document['systemStates'] = [{
      'id' => 'run',
      'domainStates' => [{'domain' => 'power-always', 'state' => 'on'}]
    }]
    power_plan = FinepaperNoc::PowerIntent::Compiler.compile(
      context: context, document: document
    )
    hierarchy = hierarchy_manifest(context, power_plan)
    plan = build(context, power_plan, hierarchy)

    assert_equal 2, plan.fetch('edgeOrientations').size
    plan.fetch('edgeOrientations').each do |orientation|
      assert_equal 'power-always', orientation.fetch('sourceSupplyDomain')
      assert_equal 'power-always', orientation.fetch('destinationSupplyDomain')
      assert_equal 'deferred', orientation.dig('powerBoundary', 'status')
      assert_equal(
        'rtl_hierarchy.infrastructure_bridge_supply_unowned',
        orientation.dig('powerBoundary', 'reasonCode')
      )
      orientation.fetch('signalFlows').each do |flow|
        assert_equal 'deferred', flow.fetch('status')
        assert_equal 'deferred', flow.dig('isolation', 'status')
        assert_equal 'deferred', flow.dig('levelShifter', 'status')
      end
    end
    refute plan.dig('coverage', 'complete')
    bridge_ownership = plan.dig('coverage', 'items').select do |item|
      item.fetch('kind') == 'infrastructure-supply-ownership' &&
        item.fetch('reason') ==
        'rtl_hierarchy.infrastructure_bridge_supply_unowned'
    end
    assert_equal 2, bridge_ownership.size
    assert_all_coverage_subjects_resolve(plan)
  end

  def test_output_is_deterministic_for_equivalent_reordering
    context, power_plan, hierarchy = fixture
    expected = build(context, power_plan, hierarchy)
    reordered_power = deep_copy(power_plan)
    %w[supplies controls domains systemStates].each do |key|
      reordered_power.fetch(key).reverse!
    end
    reordered_power.fetch('supplies').each { |supply| supply.fetch('states').reverse! }
    reordered_power.fetch('domains').each { |domain| domain.fetch('states').reverse! }
    reordered_power.fetch('systemStates').each do |state|
      state.fetch('domainStates').reverse!
    end
    reordered_power.dig('technology', 'interfaceCells').reverse!
    reordered_hierarchy = deep_copy(hierarchy)
    %w[elements resetSynchronizers logicControlPorts edgeDirections].each do |key|
      reordered_hierarchy.fetch(key).reverse!
    end
    reordered_hierarchy.fetch('edgeDirections').each do |edge|
      edge.fetch('signalFlows').reverse!
    end

    actual = build(context, reordered_power, reordered_hierarchy)
    assert_equal JSON.generate(expected), JSON.generate(actual)
  end

  def test_output_does_not_alias_or_freeze_mutable_plan_and_hierarchy
    context, compiled, emitted = fixture
    power_plan = deep_copy(compiled)
    hierarchy = deep_copy(emitted)
    plan = build(context, power_plan, hierarchy)
    input_net = power_plan.dig('supplies', 0, 'net')
    input_instance = hierarchy.dig('elements', 0, 'instance')

    refute input_net.frozen?
    refute input_instance.frozen?
    input_net << '_caller_mutation'
    input_instance << '_caller_mutation'
    refute_equal input_net, plan.dig('supplies', 0, 'net')
    refute plan.fetch('domains').flat_map { |domain| domain.fetch('elements') }
               .include?(input_instance)
  end

  def test_cross_contract_and_wiring_failures_are_structured
    context, power_plan, hierarchy = fixture
    cases = [
      ['design mismatch', 'power_implementation.design_mismatch', lambda do |power, _rtl|
        power['design'] = 'wrong'
      end],
      ['unknown escaped field', 'power_implementation.unknown_field', lambda do |_power, rtl|
        rtl['trick/~field'] = true
      end],
      ['ready wiring', 'power_implementation.signal_flow_wiring_mismatch', lambda do |_power, rtl|
        flow = rtl.fetch('edgeDirections').first.fetch('signalFlows').find do |entry|
          entry.fetch('type') == 'ready'
        end
        flow['driver'], flow['receiver'] = flow.fetch('receiver'), flow.fetch('driver')
      end],
      ['element supply', 'power_implementation.element_supply_mismatch', lambda do |_power, rtl|
        rtl.fetch('elements').first['supplyDomain'] = 'power-switch'
      end]
    ]

    cases.each do |label, code, mutate|
      power = deep_copy(power_plan)
      rtl = deep_copy(hierarchy)
      mutate.call(power, rtl)
      error = assert_raises(FinepaperNoc::PowerIntent::PowerImplementationPlanError,
                            label) do
        build(context, power, rtl)
      end
      assert_equal code, error.code, label
      assert_match(%r{\A/}, error.path, label)
      if label == 'unknown escaped field'
        assert_equal '/hierarchy/trick~1~0field', error.path
      end
    end
  end

  def test_absent_crossing_recipes_are_explicitly_not_required
    plan_hash = implementation_plan
    plan_hash.fetch('edgeBindings').first['stages'] = []
    context = FinepaperNoc::DomainRtlContext.new(plan_hash)
    document = power_intent
    document.fetch('domains').each do |domain|
      domain.delete('levelShifter')
      domain.delete('isolation')
    end
    document.fetch('technology')['interfaceCells'] = document.dig(
      'technology', 'interfaceCells'
    ).reject do |cell|
      %w[isolation level-shifter].include?(cell.fetch('kind'))
    end
    power_plan = FinepaperNoc::PowerIntent::Compiler.compile(
      context: context, document: document
    )
    hierarchy = hierarchy_manifest(context, power_plan)
    plan = build(context, power_plan, hierarchy)

    plan.fetch('edgeOrientations').each do |edge|
      edge.fetch('signalFlows').each do |flow|
        assert_equal 'not-required', flow.dig('isolation', 'status')
        assert_equal 'power_implementation.isolation_not_planned',
                     flow.dig('isolation', 'reason') unless
          flow.dig('driver', 'domain') == flow.dig('receiver', 'domain')
        assert_equal 'not-required', flow.dig('levelShifter', 'status')
        assert_equal 'power_implementation.level_shifter_not_planned',
                     flow.dig('levelShifter', 'reason') unless
          flow.dig('driver', 'domain') == flow.dig('receiver', 'domain')
      end
    end
  end

  def test_explicit_directional_recipe_is_preserved_at_equal_voltage
    plan_hash = implementation_plan
    switch_domain = plan_hash.fetch('domainBindings').find do |domain|
      domain.fetch('domain') == 'power-switch'
    end
    switch_domain.dig('parameters', 'nominal-voltage-mv')['value'] = 900
    context = FinepaperNoc::DomainRtlContext.new(plan_hash)
    document = power_intent
    switched_supply = document.fetch('supplies').find do |supply|
      supply.fetch('id') == 'vdd_b'
    end
    switched_supply.fetch('states').find do |state|
      state.fetch('condition') == 'full-on'
    end['voltageMv'] = 900
    power_plan = FinepaperNoc::PowerIntent::Compiler.compile(
      context: context, document: document
    )
    plan = build(context, power_plan,
                 hierarchy_manifest(context, power_plan))
    forward = edge_direction(plan, 'from-to')

    assert_equal 'high_to_low',
                 signal_flow(forward, 'direct', 'payload')
                   .dig('levelShifter', 'rule')
    assert_equal 'low_to_high',
                 signal_flow(forward, 'direct', 'ready')
                   .dig('levelShifter', 'rule')
  end

  private

  def fixture(cdc: false)
    context = FinepaperNoc::DomainRtlContext.new(implementation_plan(cdc: cdc))
    power_plan = FinepaperNoc::PowerIntent::Compiler.compile(
      context: context, document: power_intent
    )
    [context, power_plan, hierarchy_manifest(context, power_plan)]
  end

  def build(context, power_plan, hierarchy)
    FinepaperNoc::PowerIntent::ImplementationPlanBuilder.build(
      context: context, power_plan: power_plan, hierarchy: hierarchy
    )
  end

  def implementation_plan(cdc: false)
    left = reference('router', 'r0')
    right = reference('router', 'r1')
    left_clock = cdc ? 'clock-left' : 'clock-main'
    right_clock = cdc ? 'clock-right' : 'clock-main'
    left_bindings = [binding('supply-domain', 'power', 'power-always'),
                     binding('timing-domain', 'clock', left_clock)]
    right_bindings = [binding('supply-domain', 'power', 'power-switch'),
                      binding('timing-domain', 'clock', right_clock)]
    timing_domains = [[left_clock, [left]]]
    timing_domains << [right_clock, [right]] if cdc
    timing_domains[0][1] << right unless cdc
    stages = []
    stages << async_fifo_stage(left_clock, right_clock) if cdc
    stages.concat(power_stages)
    {
      'format' => 'finepaper.noc-domain-implementation-plan',
      'formatVersion' => 1,
      'design' => 'power_ir_fixture',
      'source' => {
        'format' => 'finepaper.noc-domain-constraints', 'formatVersion' => 1
      },
      'realization' => {
        'format' => 'finepaper.noc-domain-realization', 'formatVersion' => 1
      },
      'domainBindings' => timing_domains.map do |id, members|
        domain(id, 'clock', 'timing-domain', members,
               {'reset-release-stages' => parameter('integer', 2,
                                                     'resetReleaseStages')})
      end + [
        power_domain('power-always', [left], false, 900),
        power_domain('power-switch', [right], true, 750),
        power_domain('power-unused', [], false, 650)
      ],
      'relationBindings' => [],
      'entityBindings' => [
        {'element' => left, 'bindings' => left_bindings},
        {'element' => right, 'bindings' => right_bindings}
      ],
      'edgeBindings' => [{
        'edge' => reference('router-link', 'r0-r1'),
        'fromElement' => left,
        'toElement' => right,
        'fromBindings' => deep_copy(left_bindings),
        'toBindings' => deep_copy(right_bindings),
        'stages' => stages
      }]
    }
  end

  def power_stages
    common = {
      'domainType' => 'power',
      'fromDomain' => 'power-always',
      'toDomain' => 'power-switch',
      'policy' => {'source' => 'policy', 'id' => 'power-crossing'},
      'parameters' => {}
    }
    [
      common.merge(
        'order' => 200,
        'role' => 'power-isolation-boundary',
        'recipe' => 'power-isolation',
        'recipeKind' => 'bidirectional-stage'
      ),
      common.merge(
        'order' => 300,
        'role' => 'voltage-translation-boundary',
        'directions' => [
          level_direction('from-to', 'down'),
          level_direction('to-from', 'up')
        ]
      )
    ]
  end

  def async_fifo_stage(from_domain, to_domain)
    {
      'order' => 100,
      'role' => 'timing-boundary',
      'domainType' => 'clock',
      'fromDomain' => from_domain,
      'toDomain' => to_domain,
      'policy' => {'source' => 'policy', 'id' => 'clock-crossing'},
      'parameters' => {
        'fifo-depth' => parameter('integer', 4, 'fifoDepth'),
        'metastability-stages' => parameter('integer', 3,
                                            'synchronizerStages')
      },
      'recipe' => 'clock-async-fifo',
      'recipeKind' => 'bidirectional-stage'
    }
  end

  def level_direction(orientation, direction)
    {
      'orientation' => orientation,
      'recipe' => 'power-level-shifter',
      'recipeKind' => 'directional-stage',
      'parameters' => {
        'translation-direction' => parameter('enum', direction, 'levelShift')
      }
    }
  end

  def power_intent
    {
      'format' => 'finepaper.noc-power-intent',
      'formatVersion' => 1,
      'supplies' => [
        supply('vdd_unused', 'power', 650),
        supply('vdd_b', 'power', 750, exposure: 'internal-switched'),
        supply('vdd_a', 'power', 900),
        supply('vdd_in', 'power', 900, off: false),
        supply('vret', 'power', 900, off: false),
        supply('vss', 'ground', 0, off: false)
      ],
      'controls' => [
        control('unused', 'unused_req', 'upf-port'),
        control('restore', 'restore_req', 'upf-port'),
        control('switch', 'power_enable', 'top-port'),
        control('save', 'save_req', 'upf-port'),
        control('isolate', 'isolate_req', 'top-port')
      ],
      'domains' => [
        always_on_intent('power-unused', 'vdd_unused'),
        switchable_intent,
        always_on_intent('power-always', 'vdd_a', level_shifter: true)
      ],
      'defaultSystemState' => 'run',
      'systemStates' => [
        system_state('standby', 'sleep'), system_state('run', 'on')
      ],
      'technology' => {
        'profile' => 'upf-interface-cells',
        'interfaceCells' => [
          cell('ls_up_cell', 'level-shifter', direction: 'up'),
          cell('switch_cell', 'power-switch'),
          cell('isolation_cell', 'isolation'),
          cell('retention_cell', 'retention'),
          cell('ls_down_cell', 'level-shifter', direction: 'down')
        ]
      }
    }
  end

  def switchable_intent
    {
      'domain' => 'power-switch',
      'primaryPower' => 'vdd_b',
      'primaryGround' => 'vss',
      'mode' => 'switchable',
      'defaultState' => 'on',
      'states' => [
        {'id' => 'sleep', 'powerState' => 'OFF', 'groundState' => 'ON',
         'behavior' => 'retained'},
        {'id' => 'on', 'powerState' => 'ON', 'groundState' => 'ON',
         'behavior' => 'operational'}
      ],
      'powerSwitch' => {
        'inputSupply' => 'vdd_in', 'outputSupply' => 'vdd_b',
        'control' => 'switch', 'onSense' => 'high'
      },
      'retention' => {
        'supply' => 'vret', 'saveControl' => 'save',
        'restoreControl' => 'restore', 'saveEdge' => 'posedge',
        'restoreEdge' => 'negedge', 'location' => 'self'
      },
      'isolation' => {
        'control' => 'isolate', 'supply' => 'vdd_in', 'clampValue' => 0,
        'location' => 'parent'
      },
      'levelShifter' => {'location' => 'automatic'}
    }
  end

  def always_on_intent(id, supply_id, level_shifter: false)
    result = {
      'domain' => id,
      'primaryPower' => supply_id,
      'primaryGround' => 'vss',
      'mode' => 'always-on',
      'defaultState' => 'on',
      'states' => [{
        'id' => 'on', 'powerState' => 'ON', 'groundState' => 'ON',
        'behavior' => 'operational'
      }]
    }
    result['levelShifter'] = {'location' => 'self'} if level_shifter
    result
  end

  def hierarchy_manifest(context, power_plan)
    top_ports = power_plan.fetch('controls').select do |control|
      control.fetch('source') == 'top-port'
    end.map do |control|
      {
        'id' => control.fetch('id'), 'signal' => control.fetch('signal'),
        'source' => 'top-port', 'direction' => 'input'
      }
    end
    builder = FinepaperNoc::RtlHierarchyManifestBuilder.new(
      context: context,
      design: 'power_ir_fixture',
      top_module: 'power_ir_fixture_top',
      top_artifact: 'power_ir_fixture_top.v',
      expected_logic_control_ports: top_ports
    )
    top_ports.reverse_each do |port|
      builder.register_logic_control_port(
        control_id: port.fetch('id'), signal: port.fetch('signal'),
        source: port.fetch('source'), direction: port.fetch('direction')
      )
    end
    {
      %w[router r0] => ['router_left', 'u_router_left'],
      %w[router r1] => ['router_right', 'u_router_right']
    }.each do |key, names|
      builder.register_element(
        element: {'kind' => key.fetch(0), 'id' => key.fetch(1)},
        module_name: names.fetch(0), instance: names.fetch(1)
      )
    end
    context.domains_for_role('timing-domain').each do |domain|
      token = domain.fetch('token')
      builder.register_reset_synchronizer(
        timing_domain: domain.fetch('domain'),
        module_name: 'fp_reset_synchronizer',
        instance: "u_reset_#{token}",
        clock_signal: "clk_#{token}", async_reset_signal: 'rst_n',
        local_reset_signal: "rst_n_#{token}"
      )
    end
    FinepaperNoc::DomainRtlContext::ORIENTATIONS.each do |orientation|
      traffic = context.traffic('router-link', 'r0-r1', orientation)
      bridged = !context.edge_stage(
        'router-link', 'r0-r1', 'clock-async-fifo'
      ).nil?
      suffix = orientation.tr('-', '_')
      source_name = "link_#{suffix}#{bridged ? '_src' : ''}"
      destination_name = bridged ? "link_#{suffix}_dst" : source_name
      bridge = if bridged
                 {
                   'module' => 'fp_async_ready_valid_fifo',
                   'instance' => "u_fifo_#{suffix}",
                   'placement' => 'infrastructure',
                   'sourcePins' => pins('src', input_payload: true),
                   'destinationPins' => pins('dst', input_payload: false)
                 }
               end
      builder.register_edge_direction(
        edge: reference('router-link', 'r0-r1'),
        orientation: orientation,
        producer: traffic.fetch('producer'),
        consumer: traffic.fetch('consumer'),
        producer_pins: pins('out', input_payload: false),
        consumer_pins: pins('in', input_payload: true),
        source_bundle: bundle(source_name),
        destination_bundle: bundle(destination_name),
        bridge: bridge
      )
    end
    builder.build
  end

  def pins(prefix, input_payload:)
    if input_payload
      {'payload' => "#{prefix}_payload_i", 'valid' => "#{prefix}_valid_i",
       'ready' => "#{prefix}_ready_o"}
    else
      {'payload' => "#{prefix}_payload_o", 'valid' => "#{prefix}_valid_o",
       'ready' => "#{prefix}_ready_i"}
    end
  end

  def bundle(name)
    {'name' => name, 'payload' => "#{name}_payload",
     'valid' => "#{name}_valid", 'ready' => "#{name}_ready"}
  end

  def system_state(id, switch_state)
    {
      'id' => id,
      'domainStates' => [
        {'domain' => 'power-switch', 'state' => switch_state},
        {'domain' => 'power-always', 'state' => 'on'}
      ]
    }
  end

  def supply(id, kind, voltage, exposure: 'external-port', off: true)
    states = [{'id' => 'ON', 'condition' => 'full-on', 'voltageMv' => voltage}]
    states.unshift({'id' => 'OFF', 'condition' => 'off'}) if off
    result = {
      'id' => id, 'kind' => kind, 'exposure' => exposure,
      'net' => id, 'states' => states
    }
    result['port'] = "#{id}_port" if exposure == 'external-port'
    result
  end

  def control(id, signal, source)
    {
      'id' => id, 'signal' => signal, 'source' => source,
      'activeSense' => 'high', 'ownerDomain' => 'power-always'
    }
  end

  def cell(id, kind, direction: nil)
    result = {'id' => id, 'kind' => kind, 'cells' => [id.upcase]}
    result['direction'] = direction if direction
    result
  end

  def power_domain(id, members, retains, voltage)
    domain(
      id, 'power', 'supply-domain', members,
      {
        'nominal-voltage-mv' => parameter('integer', voltage, 'voltageMv'),
        'retains-state' => parameter('boolean', retains, 'retention')
      }
    )
  end

  def domain(id, type, role, members, parameters)
    {
      'domain' => id, 'domainType' => type, 'role' => role, 'name' => id,
      'parameters' => parameters, 'members' => deep_copy(members)
    }
  end

  def binding(role, type, domain_id)
    {'role' => role, 'domainType' => type, 'domain' => domain_id}
  end

  def parameter(type, value, source_id)
    {
      'type' => type, 'value' => value,
      'source' => {'kind' => 'property', 'id' => source_id}
    }
  end

  def reference(kind, id)
    {'kind' => kind, 'id' => id}
  end

  def edge_direction(plan, orientation)
    plan.fetch('edgeOrientations').find do |edge|
      edge.fetch('orientation') == orientation
    end
  end

  def signal_flow(edge, side, type)
    edge.fetch('signalFlows').find do |flow|
      flow.fetch('side') == side && flow.fetch('type') == type
    end
  end

  def assert_all_coverage_subjects_resolve(plan)
    plan.dig('coverage', 'items').each do |item|
      value = plan
      item.fetch('subject').split('/').drop(1).each do |encoded|
        token = encoded.gsub('~1', '/').gsub('~0', '~')
        value = value.is_a?(Array) ? value.fetch(Integer(token, 10)) :
          value.fetch(token)
      end
      refute_nil value, item.fetch('subject')
    end
  end

  def assert_canonical_hashes(value)
    case value
    when Hash
      assert_equal value.keys.sort, value.keys
      value.each_value { |child| assert_canonical_hashes(child) }
    when Array
      value.each { |child| assert_canonical_hashes(child) }
    end
  end

  def deep_copy(value)
    JSON.parse(JSON.generate(value))
  end
end
