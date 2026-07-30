# frozen_string_literal: true

require 'json'
require 'minitest/autorun'
require_relative '../lib/power_intent/compiler'

class PowerIntentCompilerTest < Minitest::Test
  RUNTIME_ROOT = File.expand_path('..', __dir__)
  SCHEMA_PATH = File.join(RUNTIME_ROOT, 'power-intent.schema.json')

  def setup
    @context = FinepaperNoc::DomainRtlContext.new(implementation_plan)
    @document = power_intent
  end

  def test_schema_publishes_the_strict_v1_namespace
    schema = JSON.parse(File.read(SCHEMA_PATH))

    assert_equal 'https://json-schema.org/draft/2020-12/schema', schema.fetch('$schema')
    assert_equal false, schema.fetch('additionalProperties')
    assert_equal 'finepaper.noc-power-intent',
                 schema.dig('properties', 'format', 'const')
    assert_equal 1, schema.dig('properties', 'formatVersion', 'const')
    assert_equal false, schema.dig('$defs', 'domain', 'additionalProperties')
    assert_includes schema.fetch('required'), 'defaultSystemState'
    assert_equal '^[A-Za-z_][A-Za-z0-9_$]*$',
                 schema.dig('$defs', 'hdlIdentifier', 'pattern')
    assert schema.dig('$defs', 'technology', 'allOf')
    assert schema.dig('$defs', 'interfaceCell', 'allOf')
    assert_equal 1, schema.dig('$defs', 'interfaceCell', 'properties',
                               'cells', 'minItems')
    assert_equal true, schema.dig('$defs', 'interfaceCell', 'properties',
                                  'cells', 'uniqueItems')
    assert_equal '^[A-Za-z_][A-Za-z0-9_$.:/-]*$',
                 schema.dig('$defs', 'interfaceCell', 'properties', 'cells',
                            'items', 'pattern')
    assert_equal ['direction'],
                 schema.dig('$defs', 'interfaceCell', 'allOf', 0, 'then',
                            'required')
    assert_equal %w[external-port internal-switched],
                 schema.dig('$defs', 'supply', 'properties', 'exposure', 'enum')
    assert schema.dig('$defs', 'supply', 'allOf')
    assert_equal false, schema.dig('$defs', 'isolation', 'additionalProperties')
    assert_equal %w[self parent automatic],
                 schema.dig('$defs', 'levelShifter', 'properties',
                            'location', 'enum')
    assert_includes schema.dig('$defs', 'interfaceCell', 'properties',
                               'kind', 'enum'), 'power-switch'
  end

  def test_compiles_a_standalone_canonical_plan_with_context_facts
    original = deep_copy(@document)
    plan = compile

    assert_equal 'finepaper.noc-power-intent-plan', plan.fetch('format')
    assert_equal 1, plan.fetch('formatVersion')
    assert_equal 'power_fixture', plan.fetch('design')
    assert_equal({'format' => 'finepaper.noc-power-intent', 'formatVersion' => 1},
                 plan.fetch('source'))
    assert_equal 'finepaper.noc-domain-implementation-plan',
                 plan.dig('implementationPlan', 'format')
    assert_equal %w[vdd_a vdd_b vdd_in vret vss],
                 plan.fetch('supplies').map { |entry| entry.fetch('id') }
    assert_equal %w[isolate_b restore save switch_b],
                 plan.fetch('controls').map { |entry| entry.fetch('id') }
    assert_equal %w[power-always power-switch power-unused],
                 plan.fetch('domains').map { |entry| entry.fetch('domain') }
    assert_equal %w[run standby],
                 plan.fetch('systemStates').map { |entry| entry.fetch('id') }
    assert_equal 'run', plan.fetch('defaultSystemState')

    switchable = plan.fetch('domains').find do |entry|
      entry.fetch('domain') == 'power-switch'
    end
    assert switchable.fetch('active')
    assert_match(/_[0-9a-f]{64}\z/, switchable.fetch('token'))
    assert_equal [{'kind' => 'router', 'id' => 'r1'}], switchable.fetch('members')
    assert_equal true,
                 switchable.dig('parameters', 'retains-state', 'value')
    assert_equal 'isolate_b', switchable.dig('isolation', 'control')
    assert_equal 'vret', switchable.dig('isolation', 'supply')
    assert_equal 0, switchable.dig('isolation', 'clampValue')
    assert_equal 'self', switchable.dig('isolation', 'location')
    assert_equal 'automatic', switchable.dig('levelShifter', 'location')
    assert_equal 'posedge', switchable.dig('retention', 'saveEdge')
    assert_equal 'negedge', switchable.dig('retention', 'restoreEdge')
    assert_equal %w[on sleep],
                 switchable.fetch('states').map { |entry| entry.fetch('id') }

    unused = plan.fetch('domains').find do |entry|
      entry.fetch('domain') == 'power-unused'
    end
    refute unused.fetch('active')
    assert_empty unused.fetch('members')
    interface_cell_ids = plan.dig('technology', 'interfaceCells').map do |entry|
      entry.fetch('id')
    end
    assert_equal %w[
      iso_generic ls_down ls_up power_switch_generic retention_generic
    ],
                 interface_cell_ids
    assert_equal %w[ISO_A ISO_B],
                 plan.dig('technology', 'interfaceCells', 0, 'cells')
    internal_supply = plan.fetch('supplies').find do |entry|
      entry.fetch('id') == 'vdd_b'
    end
    assert_equal 'internal-switched', internal_supply.fetch('exposure')
    refute internal_supply.key?('port')
    assert plan.frozen?
    assert plan.fetch('domains').first.frozen?
    assert plan.dig('supplies', 0, 'port').frozen?
    assert_equal original, @document
  end

  def test_output_does_not_freeze_or_alias_mutable_json_input
    document = JSON.parse(JSON.generate(@document))
    plan = compile(document: document)
    input_port = document.dig('supplies', 0, 'port')
    output_port = plan.fetch('supplies').find do |entry|
      entry.fetch('id') == document.dig('supplies', 0, 'id')
    end.fetch('port')

    refute input_port.frozen?
    input_port << '_changed'
    refute_equal input_port, output_port
    assert output_port.frozen?
  end

  def test_integral_json_voltage_is_canonicalized_to_integer
    document = JSON.parse(JSON.generate(@document))
    supply(document, 'vdd_a').fetch('states').find do |state|
      state.fetch('condition') == 'full-on'
    end['voltageMv'] = 900.0
    domain(document, 'power-switch').fetch('isolation')['clampValue'] = 0.0

    plan = compile(document: document)
    voltage = plan.fetch('supplies').find do |entry|
      entry.fetch('id') == 'vdd_a'
    end.fetch('states').find do |state|
      state.fetch('condition') == 'full-on'
    end.fetch('voltageMv')
    assert_instance_of Integer, voltage
    assert_equal 900, voltage
    clamp_value = plan.fetch('domains').find do |entry|
      entry.fetch('domain') == 'power-switch'
    end.dig('isolation', 'clampValue')
    assert_instance_of Integer, clamp_value
    assert_equal 0, clamp_value
  end

  def test_equivalent_input_and_context_reordering_is_byte_deterministic
    expected = compile
    document = deep_copy(@document)
    %w[supplies controls domains systemStates].each do |key|
      document.fetch(key).reverse!
    end
    document.fetch('supplies').each { |supply| supply.fetch('states').reverse! }
    document.fetch('domains').each { |domain| domain.fetch('states').reverse! }
    document.fetch('systemStates').each do |state|
      state.fetch('domainStates').reverse!
    end
    document.dig('technology', 'interfaceCells').reverse!
    document.dig('technology', 'interfaceCells').each do |cell|
      cell.fetch('cells').reverse!
    end

    reordered_plan = implementation_plan
    reordered_plan.fetch('domainBindings').reverse!
    reordered_plan.fetch('entityBindings').reverse!
    context = FinepaperNoc::DomainRtlContext.new(reordered_plan)
    actual = FinepaperNoc::PowerIntent::Compiler.compile(
      context: context, document: document
    )

    assert_equal JSON.generate(expected), JSON.generate(actual)
  end

  def test_shape_and_identifier_failures_are_structured_and_fail_closed
    cases = [
      ['unknown field', 'power_intent.unknown_field', lambda { |doc| doc['trick'] = true }],
      ['duplicate supply', 'power_intent.duplicate_supply', lambda do |doc|
        doc.fetch('supplies') << deep_copy(doc.fetch('supplies').first)
      end],
      ['duplicate supply state', 'power_intent.duplicate_supply_state', lambda do |doc|
        doc.fetch('supplies').first.fetch('states') <<
          deep_copy(doc.fetch('supplies').first.fetch('states').first)
      end],
      ['duplicate control', 'power_intent.duplicate_control', lambda do |doc|
        doc.fetch('controls') << deep_copy(doc.fetch('controls').first)
      end],
      ['duplicate Domain', 'power_intent.duplicate_domain', lambda do |doc|
        doc.fetch('domains') << deep_copy(doc.fetch('domains').first)
      end],
      ['duplicate system state', 'power_intent.duplicate_system_state', lambda do |doc|
        doc.fetch('systemStates') << deep_copy(doc.fetch('systemStates').first)
      end],
      ['ground voltage', 'power_intent.invalid_ground_voltage', lambda do |doc|
        supply(doc, 'vss').fetch('states').first['voltageMv'] = 1
      end],
      ['off voltage', 'power_intent.unexpected_voltage', lambda do |doc|
        supply(doc, 'vdd_a').fetch('states').find do |state|
          state.fetch('condition') == 'off'
        end['voltageMv'] = 0
      end],
      ['missing external port', 'power_intent.missing_supply_port', lambda do |doc|
        supply(doc, 'vdd_a').delete('port')
      end],
      ['internal port', 'power_intent.unexpected_supply_port', lambda do |doc|
        supply(doc, 'vdd_b')['port'] = 'vdd_b_port'
      end]
    ]

    cases.each do |label, code, mutation|
      document = deep_copy(@document)
      mutation.call(document)
      error = assert_raises(FinepaperNoc::PowerIntent::Error, label) do
        compile(document: document)
      end
      assert_equal code, error.code, label
      assert_match(%r{\A/}, error.path, label)
    end
  end

  def test_domain_supply_switch_and_control_contracts_fail_closed
    cases = [
      ['missing Domain', 'power_intent.missing_domain', lambda do |doc|
        doc.fetch('domains').reject! { |entry| entry.fetch('domain') == 'power-unused' }
      end],
      ['unknown Domain', 'power_intent.unknown_domain', lambda do |doc|
        domain(doc, 'power-unused')['domain'] = 'not-in-plan'
      end],
      ['switchable owner', 'power_intent.invalid_control_owner', lambda do |doc|
        control(doc, 'switch_b')['ownerDomain'] = 'power-switch'
      end],
      ['unknown owner', 'power_intent.unknown_owner_domain', lambda do |doc|
        control(doc, 'switch_b')['ownerDomain'] = 'missing'
      end],
      ['wrong primary kind', 'power_intent.invalid_supply_kind', lambda do |doc|
        domain(doc, 'power-always')['primaryPower'] = 'vss'
      end],
      ['unknown power state', 'power_intent.unknown_power_state', lambda do |doc|
        domain(doc, 'power-switch').fetch('states').first['powerState'] = 'missing'
      end],
      ['missing default', 'power_intent.unknown_default_state', lambda do |doc|
        domain(doc, 'power-switch')['defaultState'] = 'missing'
      end],
      ['non-operational default', 'power_intent.invalid_default_state', lambda do |doc|
        domain(doc, 'power-switch')['defaultState'] = 'sleep'
      end],
      ['missing switch', 'power_intent.missing_power_switch', lambda do |doc|
        domain(doc, 'power-switch').delete('powerSwitch')
      end],
      ['switch on always-on', 'power_intent.unexpected_power_switch', lambda do |doc|
        domain(doc, 'power-always')['powerSwitch'] =
          deep_copy(domain(doc, 'power-switch').fetch('powerSwitch'))
      end],
      ['wrong switch output', 'power_intent.invalid_switch_output', lambda do |doc|
        domain(doc, 'power-switch').fetch('powerSwitch')['outputSupply'] = 'vdd_a'
      end],
      ['unknown switch control', 'power_intent.unknown_control', lambda do |doc|
        domain(doc, 'power-switch').fetch('powerSwitch')['control'] = 'missing'
      end]
    ]

    assert_failures(cases)
  end

  def test_retention_system_state_and_technology_contracts_fail_closed
    cases = [
      ['missing retention', 'power_intent.missing_retention', lambda do |doc|
        domain(doc, 'power-switch').delete('retention')
      end],
      ['unexpected retention', 'power_intent.unexpected_retention', lambda do |doc|
        domain(doc, 'power-always')['retention'] =
          deep_copy(domain(doc, 'power-switch').fetch('retention'))
      end],
      ['retention ground supply', 'power_intent.invalid_supply_kind', lambda do |doc|
        domain(doc, 'power-switch').fetch('retention')['supply'] = 'vss'
      end],
      ['incomplete system state', 'power_intent.incomplete_system_state', lambda do |doc|
        doc.fetch('systemStates').first.fetch('domainStates').pop
      end],
      ['inactive Domain in system state', 'power_intent.incomplete_system_state', lambda do |doc|
        doc.fetch('systemStates').first.fetch('domainStates') <<
          {'domain' => 'power-unused', 'state' => 'on'}
      end],
      ['unknown Domain state', 'power_intent.unknown_domain_state', lambda do |doc|
        doc.fetch('systemStates').first.fetch('domainStates').first['state'] = 'missing'
      end],
      ['abstract cell binding', 'power_intent.abstract_profile_has_cells', lambda do |doc|
        doc.fetch('technology')['profile'] = 'abstract'
      end],
      ['empty concrete binding', 'power_intent.missing_interface_cells', lambda do |doc|
        doc.fetch('technology')['interfaceCells'] = []
      end],
      ['isolation direction', 'power_intent.invalid_cell_direction', lambda do |doc|
        doc.dig('technology', 'interfaceCells').find do |entry|
          entry.fetch('kind') == 'isolation'
        end['direction'] = 'up'
      end],
      ['missing level-shifter direction',
       'power_intent.missing_cell_direction', lambda do |doc|
        doc.dig('technology', 'interfaceCells').find do |entry|
          entry.fetch('kind') == 'level-shifter'
        end.delete('direction')
      end],
      ['invalid library cell name', 'power_intent.invalid_library_cell', lambda do |doc|
        doc.dig('technology', 'interfaceCells').first.fetch('cells')[0] =
          'unsafe cell'
      end]
    ]

    assert_failures(cases)
  end

  def test_typed_retains_state_binding_is_mandatory
    plan = implementation_plan
    plan.fetch('domainBindings').find do |entry|
      entry.fetch('domain') == 'power-switch'
    end.fetch('parameters').delete('retains-state')
    context = FinepaperNoc::DomainRtlContext.new(plan)

    error = assert_raises(FinepaperNoc::PowerIntent::Error) do
      compile(context: context)
    end
    assert_equal 'power_intent.missing_retention_binding', error.code
    assert_match(%r{/parameters/retains-state\z}, error.path)
  end

  def test_supply_state_and_nominal_voltage_invariants_fail_closed
    cases = [
      ['zero power voltage', 'power_intent.invalid_power_voltage', lambda do |doc|
        full_on_supply_state(doc, 'vdd_a')['voltageMv'] = 0
      end],
      ['fractional voltage', 'power_intent.expected_integer', lambda do |doc|
        full_on_supply_state(doc, 'vdd_a')['voltageMv'] = 900.5
      end],
      ['operational while off', 'power_intent.invalid_state_power_condition', lambda do |doc|
        domain_state(doc, 'power-switch', 'on')['powerState'] = 'OFF'
      end],
      ['retained while on', 'power_intent.invalid_state_power_condition', lambda do |doc|
        domain_state(doc, 'power-switch', 'sleep')['powerState'] = 'ON'
      end],
      ['ground off', 'power_intent.invalid_state_ground_condition', lambda do |doc|
        supply(doc, 'vss').fetch('states') << {'id' => 'OFF', 'condition' => 'off'}
        domain_state(doc, 'power-switch', 'sleep')['groundState'] = 'OFF'
      end],
      ['always-on non-operational', 'power_intent.invalid_always_on_state', lambda do |doc|
        state = domain_state(doc, 'power-always', 'on')
        state['powerState'] = 'OFF'
        state['behavior'] = 'corrupt'
      end],
      ['switchable without off state', 'power_intent.missing_off_state', lambda do |doc|
        domain(doc, 'power-switch').fetch('states').reject! do |state|
          state.fetch('id') == 'sleep'
        end
      end],
      ['nominal mismatch', 'power_intent.nominal_voltage_mismatch', lambda do |doc|
        full_on_supply_state(doc, 'vdd_b')['voltageMv'] = 751
      end]
    ]
    assert_failures(cases)

    plan = implementation_plan
    power_plan_domain(plan, 'power-switch').fetch('parameters')
                                             .delete('nominal-voltage-mv')
    error = assert_raises(FinepaperNoc::PowerIntent::Error) do
      compile(context: FinepaperNoc::DomainRtlContext.new(plan))
    end
    assert_equal 'power_intent.missing_nominal_voltage_binding', error.code

    plan = implementation_plan
    binding = power_plan_domain(plan, 'power-switch')
              .dig('parameters', 'nominal-voltage-mv')
    binding['value'] = 0
    error = assert_raises(FinepaperNoc::PowerIntent::Error) do
      compile(context: FinepaperNoc::DomainRtlContext.new(plan))
    end
    assert_equal 'power_intent.invalid_nominal_voltage_binding', error.code
  end

  def test_switch_and_retention_electrical_invariants_fail_closed
    cases = [
      ['switch input equals output', 'power_intent.identical_switch_supplies', lambda do |doc|
        domain(doc, 'power-switch').fetch('powerSwitch')['inputSupply'] = 'vdd_b'
      end],
      ['switch input can turn off', 'power_intent.switch_input_can_turn_off', lambda do |doc|
        supply(doc, 'vdd_in').fetch('states') << {'id' => 'OFF', 'condition' => 'off'}
      end],
      ['switch sense mismatch', 'power_intent.switch_sense_mismatch', lambda do |doc|
        domain(doc, 'power-switch').fetch('powerSwitch')['onSense'] = 'low'
      end],
      ['retention supply can turn off', 'power_intent.retention_supply_can_turn_off', lambda do |doc|
        supply(doc, 'vret').fetch('states') << {'id' => 'OFF', 'condition' => 'off'}
      end],
      ['retains without retained state', 'power_intent.missing_retained_state', lambda do |doc|
        domain_state(doc, 'power-switch', 'sleep')['behavior'] = 'corrupt'
      end],
      ['isolation supply can turn off', 'power_intent.isolation_supply_can_turn_off', lambda do |doc|
        domain(doc, 'power-switch').fetch('isolation')['supply'] = 'vdd_a'
      end]
    ]
    assert_failures(cases)

    plan = implementation_plan
    power_plan_domain(plan, 'power-switch')
      .dig('parameters', 'retains-state')['value'] = false
    error = assert_raises(FinepaperNoc::PowerIntent::Error) do
      compile(context: FinepaperNoc::DomainRtlContext.new(plan))
    end
    assert_equal 'power_intent.unexpected_retained_state', error.code
  end

  def test_supply_exposure_and_switch_ownership_fail_closed
    cases = [
      ['external switch output', 'power_intent.externally_driven_switch_output', lambda do |doc|
        target = supply(doc, 'vdd_b')
        target['exposure'] = 'external-port'
        target['port'] = 'vdd_b_port'
      end],
      ['unowned internal supply', 'power_intent.invalid_internal_supply_driver', lambda do |doc|
        target = supply(doc, 'vret')
        target['exposure'] = 'internal-switched'
        target.delete('port')
      end],
      ['internal ground supply', 'power_intent.invalid_internal_supply_kind', lambda do |doc|
        target = supply(doc, 'vss')
        target['exposure'] = 'internal-switched'
        target.delete('port')
      end]
    ]
    assert_failures(cases)
  end

  def test_always_on_domain_rejects_an_internal_switched_primary_supply
    document = power_intent
    domain(document, 'power-always')['primaryPower'] = 'vdd_b'

    error = assert_raises(FinepaperNoc::PowerIntent::Error) do
      compile(document: document)
    end

    assert_equal 'power_intent.invalid_always_on_primary_supply', error.code
    assert_equal '/domains/2/primaryPower', error.path
  end

  def test_default_system_state_and_state_reachability_fail_closed
    cases = [
      ['no system states', 'power_intent.missing_system_states', lambda do |doc|
        doc.fetch('systemStates').clear
      end],
      ['unknown default system state', 'power_intent.unknown_default_system_state', lambda do |doc|
        doc['defaultSystemState'] = 'missing'
      end],
      ['wrong default vector', 'power_intent.default_system_state_mismatch', lambda do |doc|
        doc['defaultSystemState'] = 'standby'
      end],
      ['unreachable Domain state', 'power_intent.unreachable_domain_state', lambda do |doc|
        doc.fetch('systemStates').reject! { |state| state.fetch('id') == 'standby' }
      end]
    ]
    assert_failures(cases)
  end

  def test_names_pointer_escaping_and_technology_coverage_fail_closed
    cases = [
      ['duplicate supply port', 'power_intent.duplicate_supply_port', lambda do |doc|
        supply(doc, 'vdd_in')['port'] = supply(doc, 'vdd_a').fetch('port')
      end],
      ['duplicate supply net', 'power_intent.duplicate_supply_net', lambda do |doc|
        supply(doc, 'vdd_b')['net'] = supply(doc, 'vdd_a').fetch('net')
      end],
      ['duplicate control signal', 'power_intent.duplicate_control_signal', lambda do |doc|
        control(doc, 'save')['signal'] = control(doc, 'restore').fetch('signal')
      end],
      ['invalid port identifier', 'power_intent.invalid_hdl_identifier', lambda do |doc|
        supply(doc, 'vdd_a')['port'] = 'top.vdd'
      end],
      ['control character in opaque ID', 'power_intent.expected_string', lambda do |doc|
        supply(doc, 'vdd_a')['id'] = "bad\nid"
      end],
      ['missing isolation cells', 'power_intent.missing_technology_mapping', lambda do |doc|
        remove_interface_kind(doc, 'isolation')
      end],
      ['missing up cells', 'power_intent.missing_technology_mapping', lambda do |doc|
        remove_interface_kind(doc, 'level-shifter', 'up')
      end],
      ['missing down cells', 'power_intent.missing_technology_mapping', lambda do |doc|
        remove_interface_kind(doc, 'level-shifter', 'down')
      end],
      ['missing retention cells', 'power_intent.missing_technology_mapping', lambda do |doc|
        remove_interface_kind(doc, 'retention')
      end],
      ['missing power-switch cells', 'power_intent.missing_technology_mapping', lambda do |doc|
        remove_interface_kind(doc, 'power-switch')
      end],
      ['duplicate isolation mapping', 'power_intent.duplicate_technology_mapping', lambda do |doc|
        duplicate = deep_copy(doc.dig('technology', 'interfaceCells').find do |entry|
          entry.fetch('kind') == 'isolation'
        end)
        duplicate['id'] = 'iso_duplicate'
        doc.dig('technology', 'interfaceCells') << duplicate
      end],
      ['missing isolation configuration', 'power_intent.missing_isolation_configuration', lambda do |doc|
        domain(doc, 'power-switch').delete('isolation')
      end],
      ['isolation on always-on Domain', 'power_intent.unexpected_isolation_configuration', lambda do |doc|
        domain(doc, 'power-always')['isolation'] =
          deep_copy(domain(doc, 'power-switch').fetch('isolation'))
      end],
      ['missing level-shifter placement', 'power_intent.missing_level_shifter_configuration', lambda do |doc|
        domain(doc, 'power-always').delete('levelShifter')
      end]
    ]
    assert_failures(cases)

    document = deep_copy(@document)
    document['technology'] = {'profile' => 'abstract', 'interfaceCells' => []}
    assert_equal 'abstract', compile(document: document).dig('technology', 'profile')

    document = deep_copy(@document)
    document['trick/~field'] = true
    error = assert_raises(FinepaperNoc::PowerIntent::Error) do
      compile(document: document)
    end
    assert_equal 'power_intent.unknown_field', error.code
    assert_equal '/trick~1~0field', error.path
  end

  private

  def compile(context: @context, document: @document)
    FinepaperNoc::PowerIntent::Compiler.compile(
      context: context, document: document
    )
  end

  def assert_failures(cases)
    cases.each do |label, code, mutation|
      document = deep_copy(@document)
      mutation.call(document)
      error = assert_raises(FinepaperNoc::PowerIntent::Error, label) do
        compile(document: document)
      end
      assert_equal code, error.code, label
      assert_match(%r{\A/}, error.path, label)
    end
  end

  def implementation_plan
    left = reference('router', 'r0')
    right = reference('router', 'r1')
    timing = binding('timing-domain', 'clock', 'clock-main')
    supply_left = binding('supply-domain', 'power', 'power-always')
    supply_right = binding('supply-domain', 'power', 'power-switch')
    left_bindings = [supply_left, timing]
    right_bindings = [supply_right, timing]
    {
      'format' => 'finepaper.noc-domain-implementation-plan',
      'formatVersion' => 1,
      'design' => 'power_fixture',
      'source' => {
        'format' => 'finepaper.noc-domain-constraints', 'formatVersion' => 1
      },
      'realization' => {
        'format' => 'finepaper.noc-domain-realization', 'formatVersion' => 1
      },
      'domainBindings' => [
        plan_domain('clock-main', 'clock', 'timing-domain', [left, right], {}),
        plan_power_domain('power-always', [left], false, 900),
        plan_power_domain('power-switch', [right], true, 750),
        plan_power_domain('power-unused', [], false, 900)
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
        'stages' => power_crossing_stages
      }]
    }
  end

  def plan_power_domain(id, members, retains_state, voltage)
    plan_domain(
      id, 'power', 'supply-domain', members,
      {
        'nominal-voltage-mv' => parameter('integer', voltage, 'voltageMv'),
        'retains-state' => parameter('boolean', retains_state, 'retention')
      }
    )
  end

  def power_crossing_stages
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
          level_shifter_direction('from-to', 'down'),
          level_shifter_direction('to-from', 'up')
        ]
      )
    ]
  end

  def level_shifter_direction(orientation, direction)
    {
      'orientation' => orientation,
      'recipe' => 'power-level-shifter',
      'recipeKind' => 'directional-stage',
      'parameters' => {
        'translation-direction' => parameter(
          'enum', direction, 'levelShift'
        )
      }
    }
  end

  def plan_domain(id, type, role, members, parameters)
    {
      'domain' => id,
      'domainType' => type,
      'role' => role,
      'name' => id,
      'parameters' => parameters,
      'members' => deep_copy(members)
    }
  end

  def power_intent
    {
      'format' => 'finepaper.noc-power-intent',
      'formatVersion' => 1,
      'supplies' => [
        supply_entry('vret', 'power', 900, off: false),
        supply_entry('vss', 'ground', 0, off: false),
        supply_entry(
          'vdd_b', 'power', 750, exposure: 'internal-switched'
        ),
        supply_entry('vdd_a', 'power', 900),
        supply_entry('vdd_in', 'power', 900, off: false)
      ],
      'controls' => [
        control_entry('restore', 'restore_req'),
        control_entry('switch_b', 'power_enable'),
        control_entry('isolate_b', 'isolate_req'),
        control_entry('save', 'save_req')
      ],
      'domains' => [
        switchable_domain,
        always_on_domain('power-unused', 'vdd_a'),
        always_on_domain('power-always', 'vdd_a')
      ],
      'defaultSystemState' => 'run',
      'systemStates' => [
        {
          'id' => 'standby',
          'domainStates' => [
            {'domain' => 'power-switch', 'state' => 'sleep'},
            {'domain' => 'power-always', 'state' => 'on'}
          ]
        },
        {
          'id' => 'run',
          'domainStates' => [
            {'domain' => 'power-switch', 'state' => 'on'},
            {'domain' => 'power-always', 'state' => 'on'}
          ]
        }
      ],
      'technology' => {
        'profile' => 'upf-interface-cells',
        'interfaceCells' => [
          {
            'id' => 'ls_down',
            'kind' => 'level-shifter',
            'direction' => 'down',
            'cells' => %w[LS_DOWN_Z LS_DOWN_A]
          },
          {
            'id' => 'iso_generic',
            'kind' => 'isolation',
            'cells' => %w[ISO_B ISO_A]
          },
          {
            'id' => 'ls_up',
            'kind' => 'level-shifter',
            'direction' => 'up',
            'cells' => %w[LS_UP_A]
          },
          {
            'id' => 'retention_generic',
            'kind' => 'retention',
            'cells' => %w[RET_A]
          },
          {
            'id' => 'power_switch_generic',
            'kind' => 'power-switch',
            'cells' => %w[SWITCH_A]
          }
        ]
      }
    }
  end

  def supply_entry(id, kind, voltage, off: true,
                   exposure: 'external-port')
    states = [{'id' => 'ON', 'condition' => 'full-on', 'voltageMv' => voltage}]
    states.unshift({'id' => 'OFF', 'condition' => 'off'}) if off
    supply = {
      'id' => id,
      'kind' => kind,
      'exposure' => exposure,
      'net' => id,
      'states' => states
    }
    supply['port'] = "#{id}_port" if exposure == 'external-port'
    supply
  end

  def control_entry(id, signal)
    {
      'id' => id,
      'signal' => signal,
      'source' => 'upf-port',
      'activeSense' => 'high',
      'ownerDomain' => 'power-always'
    }
  end

  def always_on_domain(id, primary_power)
    result = {
      'domain' => id,
      'primaryPower' => primary_power,
      'primaryGround' => 'vss',
      'mode' => 'always-on',
      'defaultState' => 'on',
      'states' => [
        {'id' => 'on', 'powerState' => 'ON', 'groundState' => 'ON',
         'behavior' => 'operational'}
      ]
    }
    result['levelShifter'] = {'location' => 'automatic'} if id == 'power-always'
    result
  end

  def switchable_domain
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
        'inputSupply' => 'vdd_in',
        'outputSupply' => 'vdd_b',
        'control' => 'switch_b',
        'onSense' => 'high'
      },
      'retention' => {
        'supply' => 'vret',
        'saveControl' => 'save',
        'restoreControl' => 'restore',
        'saveEdge' => 'posedge',
        'restoreEdge' => 'negedge',
        'location' => 'self'
      },
      'isolation' => {
        'control' => 'isolate_b',
        'supply' => 'vret',
        'clampValue' => 0,
        'location' => 'self'
      },
      'levelShifter' => {'location' => 'automatic'}
    }
  end

  def domain(document, id)
    document.fetch('domains').find { |entry| entry.fetch('domain') == id }
  end

  def domain_state(document, domain_id, state_id)
    domain(document, domain_id).fetch('states').find do |state|
      state.fetch('id') == state_id
    end
  end

  def supply(document, id)
    document.fetch('supplies').find { |entry| entry.fetch('id') == id }
  end

  def full_on_supply_state(document, id)
    supply(document, id).fetch('states').find do |state|
      state.fetch('condition') == 'full-on'
    end
  end

  def control(document, id)
    document.fetch('controls').find { |entry| entry.fetch('id') == id }
  end

  def power_plan_domain(plan, id)
    plan.fetch('domainBindings').find { |entry| entry.fetch('domain') == id }
  end

  def remove_interface_kind(document, kind, direction = nil)
    document.dig('technology', 'interfaceCells').reject! do |entry|
      entry.fetch('kind') == kind &&
        (!direction || entry['direction'] == direction)
    end
  end

  def binding(role, type, id)
    {'role' => role, 'domainType' => type, 'domain' => id}
  end

  def parameter(type, value, source_id)
    {
      'type' => type,
      'value' => value,
      'source' => {'kind' => 'domain-property', 'id' => source_id}
    }
  end

  def reference(kind, id)
    {'kind' => kind, 'id' => id}
  end

  def deep_copy(value)
    Marshal.load(Marshal.dump(value))
  end
end
