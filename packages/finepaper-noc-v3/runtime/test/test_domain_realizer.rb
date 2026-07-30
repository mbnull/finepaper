# frozen_string_literal: true

require 'json'
require 'minitest/autorun'
require_relative '../lib/domain_realizer'

class DomainRealizerTest < Minitest::Test
  RUNTIME_ROOT = File.expand_path('..', __dir__)
  MANIFEST_PATH = File.join(RUNTIME_ROOT, 'domain-realization.json')

  def setup
    @realizer = FinepaperNoc::DomainRealizer.load(MANIFEST_PATH)
    @design = normalized_design
    @constraints = compiled_constraints
  end

  def test_compiles_typed_bindings_and_stably_ordered_combined_stages
    plan = @realizer.realize(constraints: @constraints, design: @design)

    assert_equal 'finepaper.noc-domain-implementation-plan', plan.fetch('format')
    assert_equal 1, plan.fetch('formatVersion')
    assert_equal 'combined_domains', plan.fetch('design')
    assert_equal 'finepaper.noc-domain-realization',
                 plan.dig('realization', 'format')
    assert_equal 1, plan.dig('realization', 'formatVersion')

    timing = plan.fetch('domainBindings').find do |entry|
      entry.fetch('domain') == 'clock-io'
    end
    assert_equal 'timing-domain', timing.fetch('role')
    assert_equal 500, timing.dig('parameters', 'nominal-frequency-mhz', 'value')
    assert_equal 'number', timing.dig('parameters', 'nominal-frequency-mhz', 'type')
    assert_equal 'frequencyMHz',
                 timing.dig('parameters', 'nominal-frequency-mhz', 'source', 'id')

    relation = plan.fetch('relationBindings').fetch(0)
    assert_equal 'derived-clock-divider', relation.fetch('recipe')
    assert_equal 'clock-main', relation.fetch('sourceDomain')
    assert_equal 'clock-io', relation.fetch('targetDomain')
    assert_equal 2, relation.dig('parameters', 'division-ratio', 'value')
    assert_in_delta 500.0, relation.dig('resolved', 'calculatedTarget', 'value')

    router_link = edge(plan, 'router-link', 'link-r-0-0--r-1-0')
    assert_equal [100, 200, 300], router_link.fetch('stages').map { |stage| stage.fetch('order') }
    assert_equal %w[
      timing-boundary power-isolation-boundary voltage-translation-boundary
    ], router_link.fetch('stages').map { |stage| stage.fetch('role') }
    assert_equal 'clock-async-fifo', router_link.fetch('stages').fetch(0).fetch('recipe')
    assert_equal 3,
                 router_link.fetch('stages').fetch(0)
                            .dig('parameters', 'metastability-stages', 'value')

    translation = router_link.fetch('stages').fetch(2)
    assert_equal %w[from-to to-from],
                 translation.fetch('directions').map { |entry| entry.fetch('orientation') }
    directional_modes = translation.fetch('directions').map do |entry|
      entry.dig('parameters', 'translation-direction', 'value')
    end
    assert_equal %w[down up], directional_modes

    attachment = edge(plan, 'endpoint-attachment', 'ep0')
    assert_empty attachment.fetch('stages')
    assert_equal %w[supply-domain timing-domain],
                 attachment.fetch('toBindings').map { |entry| entry.fetch('role') }

    router = entity(plan, 'router', 'r-1-0')
    assert_equal %w[supply-domain timing-domain],
                 router.fetch('bindings').map { |entry| entry.fetch('role') }
  end

  def test_output_is_deterministic_for_equivalent_input_order
    expected = @realizer.realize(constraints: @constraints, design: @design)
    reordered_constraints = deep_copy(@constraints)
    %w[instances members relations policies overrides meshCrossings].each do |field|
      reordered_constraints[field]&.reverse!
    end
    reordered_constraints.fetch('instances').each { |instance| instance.fetch('members').reverse! }
    reordered_design = deep_copy(@design)
    %w[domains domainMemberships domainRelations crossingPolicies edgeOverrides].each do |field|
      reordered_design[field]&.reverse!
    end

    actual = @realizer.realize(
      constraints: reordered_constraints,
      design: reordered_design
    )
    assert_equal JSON.generate(expected), JSON.generate(actual)
  end

  def test_equal_voltage_auto_resolution_elides_level_translation
    constraints = deep_copy(@constraints)
    design = deep_copy(@design)
    domain(constraints.fetch('instances'), 'power-low').fetch('properties')['voltageMv'] = 900
    domain(design.fetch('domains'), 'power-low').fetch('properties')['voltageMv'] = 900

    plan = @realizer.realize(constraints: constraints, design: design)
    stages = edge(plan, 'router-link', 'link-r-0-0--r-1-0').fetch('stages')
    assert_equal [100, 200], stages.map { |stage| stage.fetch('order') }
  end

  def test_plain_multibit_synchronizer_is_explicitly_fail_closed
    constraints = deep_copy(@constraints)
    design = deep_copy(@design)
    clock_policy(constraints.fetch('policies')).fetch('properties')['implementation'] =
      'synchronizer'
    clock_policy(design.fetch('crossingPolicies')).fetch('properties')['implementation'] =
      'synchronizer'
    resolution = clock_crossing(constraints).fetch('resolution')
    resolution.fetch('policyProperties')['implementation'] = 'synchronizer'
    resolution.fetch('effectiveProperties')['implementation'] = 'synchronizer'

    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: design)
    end
    assert_equal 'realization.unsupported_recipe', error.code
    assert_match(/multi-bit flit bundle/, error.message)
  end

  def test_forged_effective_properties_are_fail_closed
    constraints = deep_copy(@constraints)
    power_crossing = constraints.fetch('meshCrossings').find do |entry|
      entry.fetch('domainType') == 'power'
    end
    power_crossing.dig('resolution', 'effectiveProperties')['isolation'] = false

    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: @design)
    end
    assert_equal 'realization.resolution_mismatch', error.code
  end

  def test_same_domain_crossing_is_fail_closed
    constraints = deep_copy(@constraints)
    extra = crossing(
      'clock', 'clock-io', 'clock-io', 'clock-main-to-io',
      {'implementation' => 'async-fifo', 'synchronizerStages' => 3}
    )
    extra['edge'] = element('endpoint-attachment', 'ep0')
    extra['fromElement'] = element('router', 'r-1-0')
    extra['toElement'] = element('endpoint', 'ep0')
    constraints.fetch('meshCrossings') << extra

    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: @design)
    end
    assert_equal 'realization.unexpected_crossing', error.code
  end

  def test_derived_binding_mismatch_is_fail_closed
    constraints = deep_copy(@constraints)
    design = deep_copy(@design)
    domain(constraints.fetch('instances'), 'clock-io').fetch('properties')['frequencyMHz'] = 600
    domain(design.fetch('domains'), 'clock-io').fetch('properties')['frequencyMHz'] = 600

    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: design)
    end
    assert_equal 'realization.derived_binding_mismatch', error.code
  end

  def test_divide_source_relations_must_form_an_acyclic_graph
    constraints = deep_copy(@constraints)
    design = deep_copy(@design)
    domain(constraints.fetch('instances'), 'clock-io')
      .fetch('properties')['frequencyMHz'] = 1000
    domain(design.fetch('domains'), 'clock-io')
      .fetch('properties')['frequencyMHz'] = 1000
    cyclic_relations = [
      {
        'type' => 'derived-from', 'from' => 'clock-io', 'to' => 'clock-main',
        'properties' => {'divider' => 1}
      },
      {
        'type' => 'derived-from', 'from' => 'clock-main', 'to' => 'clock-io',
        'properties' => {'divider' => 1}
      }
    ]
    constraints['relations'] = deep_copy(cyclic_relations)
    design['domainRelations'] = deep_copy(cyclic_relations)

    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: design)
    end
    assert_equal 'realization.relation_cycle', error.code

    self_relation = {
      'type' => 'derived-from', 'from' => 'clock-main', 'to' => 'clock-main',
      'properties' => {'divider' => 1}
    }
    constraints['relations'] = [deep_copy(self_relation)]
    design['domainRelations'] = [deep_copy(self_relation)]
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: design)
    end
    assert_equal 'realization.self_relation', error.code
  end

  def test_boundary_semantics_and_nested_unknown_fields_are_fail_closed
    constraints = deep_copy(@constraints)
    constraints.fetch('boundarySemantics')['policyOrientation'] = 'reversed'
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: @design)
    end
    assert_equal 'realization.invalid_boundary_semantics', error.code

    constraints = deep_copy(@constraints)
    constraints.fetch('instances').fetch(0)['hardcodedTrick'] = true
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: @design)
    end
    assert_equal 'realization.unknown_field', error.code

    constraints = deep_copy(@constraints)
    constraints.fetch('meshCrossings').fetch(0)['hardcodedTrick'] = true
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: @design)
    end
    assert_equal 'realization.unknown_field', error.code
  end

  def test_design_version_gate_is_manifest_driven
    design = deep_copy(@design)
    design['formatVersion'] = 1

    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.validate_design!(design)
    end
    assert_equal 'realization.invalid_design_version', error.code
    assert_equal '/design/formatVersion', error.path
  end

  def test_manifest_rejects_non_numeric_bounds_and_automatic_bindings
    manifest = JSON.parse(File.read(MANIFEST_PATH))
    retains_state = manifest.fetch('domainTypes').fetch(1)
                            .fetch('bindings').fetch(1)
    retains_state['minimum'] = 0
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      FinepaperNoc::DomainRealizer.new(manifest)
    end
    assert_equal 'realization.invalid_value_bounds', error.code

    manifest = JSON.parse(File.read(MANIFEST_PATH))
    voltage = manifest.fetch('domainTypes').fetch(1).fetch('bindings').fetch(0)
    voltage['type'] = 'string'
    voltage.delete('minimum')
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      FinepaperNoc::DomainRealizer.new(manifest)
    end
    assert_equal 'realization.non_numeric_automatic_binding', error.code
  end

  def test_unmapped_type_and_property_are_fail_closed
    constraints = deep_copy(@constraints)
    design = deep_copy(@design)
    domain(constraints.fetch('instances'), 'clock-main')['type'] = 'unmapped-type'
    domain(design.fetch('domains'), 'clock-main')['type'] = 'unmapped-type'

    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: design)
    end
    assert_equal 'realization.unmapped_domain_type', error.code

    constraints = deep_copy(@constraints)
    design = deep_copy(@design)
    domain(constraints.fetch('instances'), 'power-main').fetch('properties')['unmapped'] = true
    domain(design.fetch('domains'), 'power-main').fetch('properties')['unmapped'] = true
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      @realizer.realize(constraints: constraints, design: design)
    end
    assert_equal 'realization.unconsumed_domain_property', error.code
  end

  def test_manifest_parser_rejects_unknown_fields_and_recipes
    manifest = JSON.parse(File.read(MANIFEST_PATH))
    manifest['hardcodedTrick'] = true
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      FinepaperNoc::DomainRealizer.new(manifest)
    end
    assert_equal 'realization.unknown_field', error.code

    manifest = JSON.parse(File.read(MANIFEST_PATH))
    manifest.fetch('domainTypes').fetch(0)
            .fetch('crossingStages').fetch(0)
            .fetch('selector').fetch('recipes')['async-fifo'] = 'missing-recipe'
    error = assert_raises(FinepaperNoc::DomainRealizationError) do
      FinepaperNoc::DomainRealizer.new(manifest)
    end
    assert_equal 'realization.unknown_recipe', error.code
  end

  def test_realization_manifest_covers_the_package_domain_contract
    package_path = File.expand_path('../package.json', RUNTIME_ROOT)
    package = JSON.parse(File.read(package_path))
    manifest = JSON.parse(File.read(MANIFEST_PATH))
    package_types = package.fetch('domainTypes').to_h do |domain_type|
      [domain_type.fetch('id'), domain_type]
    end
    mappings = manifest.fetch('domainTypes').to_h do |mapping|
      [mapping.fetch('type'), mapping]
    end
    assert_equal package_types.keys.sort, mappings.keys.sort

    package_types.each do |type, package_type|
      mapping = mappings.fetch(type)
      assert_property_contract(
        package_type.fetch('properties'), mapping.fetch('bindings'), type
      )

      package_relations = package_type.fetch('relations').to_h do |relation|
        [relation.fetch('id'), relation]
      end
      mapped_relations = mapping.fetch('relations').to_h do |relation|
        [relation.fetch('type'), relation]
      end
      assert_equal package_relations.keys.sort, mapped_relations.keys.sort
      package_relations.each do |relation_type, package_relation|
        assert_equal [type], package_relation.fetch('targetTypes')
        assert_equal 'single', package_relation.fetch('cardinality')
        assert_property_contract(
          package_relation.fetch('properties'),
          mapped_relations.fetch(relation_type).fetch('parameters'),
          "#{type}/#{relation_type}"
        )
      end

      crossing_declarations = mapping.fetch('crossingStages').flat_map do |stage|
        declarations = stage.fetch('parameters').dup
        declarations << stage.fetch('when') if stage.key?('when')
        declarations << stage.fetch('selector') if stage.key?('selector')
        declarations
      end
      assert_property_contract(
        package_type.fetch('crossingProperties'), crossing_declarations,
        "#{type}/crossing"
      )
      package_type.fetch('crossingProperties').each do |property|
        next unless property.fetch('type') == 'enum'

        selector = crossing_declarations.find do |declaration|
          declaration.key?('recipes') &&
            declaration.fetch('property') == property.fetch('id')
        end
        refute_nil selector, "#{type}/#{property.fetch('id')} needs a selector mapping"
        mapped_values = selector.fetch('recipes').keys
        if selector.key?('automatic')
          mapped_values += [selector.dig('automatic', 'value')]
        end
        assert_empty property.fetch('values') - mapped_values
      end
    end
  end

  def test_manifest_can_add_a_third_domain_type_without_compiler_changes
    manifest = JSON.parse(File.read(MANIFEST_PATH))
    manifest.fetch('recipes')['thermal-guard'] = {'kind' => 'bidirectional-stage'}
    manifest.fetch('domainTypes') << {
      'type' => 'thermal',
      'role' => 'thermal-zone',
      'bindings' => [
        {
          'name' => 'nominal-temperature-c', 'property' => 'temperatureC',
          'type' => 'number', 'minimum' => -273
        }
      ],
      'relations' => [],
      'crossingStages' => [
        {
          'order' => 400, 'role' => 'thermal-boundary',
          'recipe' => 'thermal-guard',
          'parameters' => [
            {
              'name' => 'guard-cycles', 'property' => 'guardCycles',
              'type' => 'integer', 'minimum' => 0
            }
          ]
        }
      ]
    }
    realizer = FinepaperNoc::DomainRealizer.new(manifest)
    constraints = deep_copy(@constraints)
    design = deep_copy(@design)
    thermal_instances = [
      {
        'id' => 'thermal-hot', 'type' => 'thermal', 'name' => 'Hot zone',
        'properties' => {'temperatureC' => 90},
        'members' => [element('router', 'r-0-0')]
      },
      {
        'id' => 'thermal-cool', 'type' => 'thermal', 'name' => 'Cool zone',
        'properties' => {'temperatureC' => 45},
        'members' => [element('router', 'r-1-0'), element('endpoint', 'ep0')]
      }
    ]
    constraints.fetch('instances').concat(deep_copy(thermal_instances))
    design.fetch('domains').concat(
      thermal_instances.map { |instance| instance.reject { |key, _| key == 'members' } }
    )
    constraints.fetch('members').each do |membership|
      id = membership.dig('element', 'id')
      membership.fetch('assignments')['thermal'] =
        [id == 'r-0-0' ? 'thermal-hot' : 'thermal-cool']
    end
    design.fetch('domainMemberships').each do |membership|
      id = membership.dig('element', 'id')
      membership.fetch('assignments')['thermal'] =
        [id == 'r-0-0' ? 'thermal-hot' : 'thermal-cool']
    end
    thermal_policy = {
      'id' => 'thermal-hot-to-cool', 'domainType' => 'thermal',
      'from' => 'thermal-hot', 'to' => 'thermal-cool',
      'properties' => {'guardCycles' => 4}
    }
    constraints.fetch('policies') << deep_copy(thermal_policy)
    design.fetch('crossingPolicies') << deep_copy(thermal_policy)
    constraints.fetch('meshCrossings') << crossing(
      'thermal', 'thermal-hot', 'thermal-cool', 'thermal-hot-to-cool',
      {'guardCycles' => 4}
    )

    plan = realizer.realize(constraints: constraints, design: design)
    binding = plan.fetch('domainBindings').find do |entry|
      entry.fetch('domain') == 'thermal-hot'
    end
    assert_equal 'thermal-zone', binding.fetch('role')
    assert_equal 90, binding.dig('parameters', 'nominal-temperature-c', 'value')
    thermal_stage = edge(plan, 'router-link', 'link-r-0-0--r-1-0')
                    .fetch('stages').find do |stage|
      stage.fetch('domainType') == 'thermal'
    end
    assert_equal 400, thermal_stage.fetch('order')
    assert_equal 'thermal-guard', thermal_stage.fetch('recipe')
    assert_equal 4, thermal_stage.dig('parameters', 'guard-cycles', 'value')
  end

  def test_compiler_source_contains_no_product_type_property_or_relation_ids
    source = File.read(File.join(RUNTIME_ROOT, 'lib', 'domain_realizer.rb'))
    %w[
      clock power frequencyMHz voltageMv levelShift derived-from
      synchronizerStages divider
    ].each do |product_id|
      refute_includes source, product_id
    end
  end

  private

  def normalized_design
    {
      'format' => 'finepaper.noc-design',
      'formatVersion' => 3,
      'id' => 'combined_domains',
      'topology' => {'type' => 'mesh', 'rows' => 1, 'columns' => 2},
      'endpoints' => [
        {
          'id' => 'ep0',
          'type' => 'slave',
          'parameters' => {},
          'attachment' => {'router' => {'x' => 1, 'y' => 0}, 'slot' => '0'}
        }
      ],
      'domains' => domain_instances.map { |entry| entry.reject { |key, _| key == 'members' } },
      'domainMemberships' => memberships,
      'domainRelations' => relations,
      'crossingPolicies' => policies,
      'edgeOverrides' => []
    }
  end

  def compiled_constraints
    {
      'format' => 'finepaper.noc-domain-constraints',
      'formatVersion' => 1,
      'design' => 'combined_domains',
      'boundarySemantics' => {
        'scope' => 'bidirectional-physical-edge',
        'policyOrientation' => 'canonical-edge-endpoints',
        'routerLinkOrientation' => 'west-to-east-or-north-to-south',
        'endpointAttachmentOrientation' => 'router-to-endpoint'
      },
      'topology' => {'type' => 'mesh', 'rows' => 1, 'columns' => 2},
      'instances' => domain_instances,
      'members' => memberships,
      'relations' => relations,
      'policies' => policies,
      'overrides' => [],
      'meshCrossings' => [clock_crossing_fixture, power_crossing_fixture]
    }
  end

  def domain_instances
    [
      {
        'id' => 'clock-main', 'type' => 'clock', 'name' => 'Main clock',
        'properties' => {'frequencyMHz' => 1000},
        'members' => [element('router', 'r-0-0')]
      },
      {
        'id' => 'clock-io', 'type' => 'clock', 'name' => 'I/O clock',
        'properties' => {'frequencyMHz' => 500},
        'members' => [element('router', 'r-1-0'), element('endpoint', 'ep0')]
      },
      {
        'id' => 'power-main', 'type' => 'power', 'name' => 'Main rail',
        'properties' => {'voltageMv' => 900, 'retention' => false},
        'members' => [element('router', 'r-0-0')]
      },
      {
        'id' => 'power-low', 'type' => 'power', 'name' => 'Low rail',
        'properties' => {'voltageMv' => 750, 'retention' => true},
        'members' => [element('router', 'r-1-0'), element('endpoint', 'ep0')]
      }
    ]
  end

  def memberships
    [
      {
        'element' => element('router', 'r-0-0'),
        'assignments' => {
          'clock' => ['clock-main'], 'power' => ['power-main']
        }
      },
      {
        'element' => element('router', 'r-1-0'),
        'assignments' => {
          'clock' => ['clock-io'], 'power' => ['power-low']
        }
      },
      {
        'element' => element('endpoint', 'ep0'),
        'assignments' => {
          'clock' => ['clock-io'], 'power' => ['power-low']
        }
      }
    ]
  end

  def relations
    [
      {
        'type' => 'derived-from', 'from' => 'clock-io', 'to' => 'clock-main',
        'properties' => {'divider' => 2}
      }
    ]
  end

  def policies
    [
      {
        'id' => 'clock-main-to-io', 'domainType' => 'clock',
        'from' => 'clock-main', 'to' => 'clock-io',
        'properties' => {'implementation' => 'async-fifo', 'synchronizerStages' => 3}
      },
      {
        'id' => 'power-main-to-low', 'domainType' => 'power',
        'from' => 'power-main', 'to' => 'power-low',
        'properties' => {'isolation' => true, 'levelShift' => 'auto'}
      }
    ]
  end

  def clock_crossing_fixture
    crossing(
      'clock', 'clock-main', 'clock-io', 'clock-main-to-io',
      {'implementation' => 'async-fifo', 'synchronizerStages' => 3}
    )
  end

  def power_crossing_fixture
    crossing(
      'power', 'power-main', 'power-low', 'power-main-to-low',
      {'isolation' => true, 'levelShift' => 'auto'}
    )
  end

  def crossing(type, from, to, policy, properties)
    {
      'edge' => element('router-link', 'link-r-0-0--r-1-0'),
      'fromElement' => element('router', 'r-0-0'),
      'toElement' => element('router', 'r-1-0'),
      'domainType' => type,
      'fromDomains' => [from],
      'toDomains' => [to],
      'resolution' => {
        'source' => 'policy',
        'policy' => policy,
        'policyProperties' => properties,
        'overrideProperties' => {},
        'effectiveProperties' => properties
      }
    }
  end

  def element(kind, id)
    {'kind' => kind, 'id' => id}
  end

  def edge(plan, kind, id)
    plan.fetch('edgeBindings').find do |entry|
      entry.dig('edge', 'kind') == kind && entry.dig('edge', 'id') == id
    end
  end

  def entity(plan, kind, id)
    plan.fetch('entityBindings').find do |entry|
      entry.dig('element', 'kind') == kind && entry.dig('element', 'id') == id
    end
  end

  def domain(values, id)
    values.find { |entry| entry.fetch('id') == id }
  end

  def clock_crossing(constraints)
    constraints.fetch('meshCrossings').find do |entry|
      entry.fetch('domainType') == 'clock'
    end
  end

  def clock_policy(values)
    values.find { |entry| entry.fetch('id') == 'clock-main-to-io' }
  end

  def deep_copy(value)
    JSON.parse(JSON.generate(value))
  end

  def assert_property_contract(package_properties, declarations, owner)
    package_by_id = package_properties.to_h do |property|
      [property.fetch('id'), property]
    end
    declarations_by_property = declarations.to_h do |declaration|
      [declaration.fetch('property'), declaration]
    end
    assert_equal declarations.length, declarations_by_property.length,
                 "#{owner} has duplicate realization property mappings"
    assert_equal package_by_id.keys.sort, declarations_by_property.keys.sort,
                 "#{owner} property mapping drift"
    package_by_id.each do |property_id, package_property|
      declaration = declarations_by_property.fetch(property_id)
      assert_equal package_property.fetch('type'), declaration.fetch('type'),
                   "#{owner}/#{property_id} type drift"
      %w[minimum maximum].each do |bound|
        assert_equal package_property.key?(bound), declaration.key?(bound),
                     "#{owner}/#{property_id} #{bound} presence drift"
        next unless package_property.key?(bound)

        assert_equal package_property.fetch(bound), declaration.fetch(bound),
                     "#{owner}/#{property_id} #{bound} drift"
      end
    end
  end
end
