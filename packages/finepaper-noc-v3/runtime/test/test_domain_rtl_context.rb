# frozen_string_literal: true

require 'json'
require 'minitest/autorun'
require_relative '../lib/domain_rtl_context'

class DomainRtlContextTest < Minitest::Test
  def setup
    @plan = implementation_plan
    @context = FinepaperNoc::DomainRtlContext.new(@plan)
  end

  def test_exposes_role_bindings_edges_and_oriented_traffic
    timing = @context.domains_for_role('timing-role')
    assert_equal ['timing a', 'timing_a'], timing.map { |entry| entry.fetch('domain') }
    timing.each do |domain|
      assert_match(/\A[A-Za-z_][A-Za-z0-9_]*\z/, domain.fetch('token'))
    end
    refute_equal timing.fetch(0).fetch('token'), timing.fetch(1).fetch('token')

    assert_equal 'timing a',
                 @context.entity_domain('router', 'r-0-0', 'timing-role')
                         .fetch('domain')
    assert_equal 'timing_a',
                 @context.entity_domain('router', 'r-1-0', 'timing-role')
                         .fetch('domain')
    assert_equal 1, @context.relations.size

    stage = @context.edge_stage('router-link', 'edge 0', 'async-queue')
    assert_equal 4,
                 @context.parameter_value(stage, 'queue-depth',
                                          expected_type: 'integer')

    forward = @context.traffic('router-link', 'edge 0', 'from-to')
    reverse = @context.traffic('router-link', 'edge 0', 'to-from')
    assert_equal 'r-0-0', forward.dig('producer', 'id')
    assert_equal 'r-1-0', forward.dig('consumer', 'id')
    assert_equal 'r-1-0', reverse.dig('producer', 'id')
    assert_equal 'r-0-0', reverse.dig('consumer', 'id')
    assert_equal 'raise', forward.fetch('stages').fetch(1)
                                 .dig('directionParameters', 'mode', 'value')
    assert_equal 'lower', reverse.fetch('stages').fetch(1)
                                 .dig('directionParameters', 'mode', 'value')
  end

  def test_tokens_are_stable_for_unicode_and_input_reordering
    first_tokens = @context.domains.values.to_h do |domain|
      [domain.fetch('domain'), domain.fetch('token')]
    end
    reordered = deep_copy(@plan)
    reordered.fetch('domainBindings').reverse!
    reordered.fetch('entityBindings').reverse!
    reordered.fetch('edgeBindings').reverse!
    second = FinepaperNoc::DomainRtlContext.new(reordered)
    second_tokens = second.domains.values.to_h do |domain|
      [domain.fetch('domain'), domain.fetch('token')]
    end

    assert_equal first_tokens, second_tokens
    assert_match(/_[0-9a-f]{64}\z/, first_tokens.fetch('温度 域'))
  end

  def test_context_is_deeply_immutable
    assert @context.plan.frozen?
    assert @context.domains.frozen?
    assert @context.domains.fetch('timing a').fetch('parameters').frozen?
    assert_raises(FrozenError) do
      @context.domains.fetch('timing a')['token'] = 'changed'
    end
  end

  def test_unknown_fields_and_inconsistent_members_fail_closed
    invalid = deep_copy(@plan)
    invalid['hardcodedTrick'] = true
    error = assert_raises(FinepaperNoc::DomainRtlContextError) do
      FinepaperNoc::DomainRtlContext.new(invalid)
    end
    assert_equal 'rtl_context.unknown_field', error.code

    invalid = deep_copy(@plan)
    invalid.fetch('domainBindings').fetch(0).fetch('members').clear
    error = assert_raises(FinepaperNoc::DomainRtlContextError) do
      FinepaperNoc::DomainRtlContext.new(invalid)
    end
    assert_equal 'rtl_context.member_mismatch', error.code
  end

  def test_missing_or_ambiguous_entity_role_fails_closed
    error = assert_raises(FinepaperNoc::DomainRtlContextError) do
      @context.entity_domain('router', 'r-0-0', 'missing-role')
    end
    assert_equal 'rtl_context.non_singleton_role_binding', error.code

    invalid = deep_copy(@plan)
    extra = binding('timing-role', 'timing-type', 'timing_a')
    invalid.fetch('entityBindings').fetch(0).fetch('bindings') << extra
    invalid.fetch('edgeBindings').fetch(0).fetch('fromBindings') << deep_copy(extra)
    invalid.fetch('domainBindings').find do |domain|
      domain.fetch('domain') == 'timing_a'
    end.fetch('members') << reference('router', 'r-0-0')
    context = FinepaperNoc::DomainRtlContext.new(invalid)
    error = assert_raises(FinepaperNoc::DomainRtlContextError) do
      context.entity_domain('router', 'r-0-0', 'timing-role')
    end
    assert_equal 'rtl_context.non_singleton_role_binding', error.code
  end

  def test_headers_relations_directions_and_parameter_values_are_strict
    invalid = deep_copy(@plan)
    invalid.fetch('source')['formatVersion'] = 2
    error = assert_raises(FinepaperNoc::DomainRtlContextError) do
      FinepaperNoc::DomainRtlContext.new(invalid)
    end
    assert_equal 'rtl_context.invalid_header_version', error.code

    invalid = deep_copy(@plan)
    invalid['relationBindings'] = 'ignored-before-rendering'
    error = assert_raises(FinepaperNoc::DomainRtlContextError) do
      FinepaperNoc::DomainRtlContext.new(invalid)
    end
    assert_equal 'rtl_context.expected_array', error.code

    invalid = deep_copy(@plan)
    directions = invalid.fetch('edgeBindings').fetch(0)
                        .fetch('stages').fetch(1).fetch('directions')
    directions.fetch(1)['orientation'] = 'from-to'
    error = assert_raises(FinepaperNoc::DomainRtlContextError) do
      FinepaperNoc::DomainRtlContext.new(invalid)
    end
    assert_equal 'rtl_context.incomplete_directions', error.code

    invalid = deep_copy(@plan)
    invalid.fetch('edgeBindings').fetch(0).fetch('stages').fetch(0)
           .dig('parameters', 'queue-depth')['value'] = 'four'
    error = assert_raises(FinepaperNoc::DomainRtlContextError) do
      FinepaperNoc::DomainRtlContext.new(invalid)
    end
    assert_equal 'rtl_context.invalid_parameter_value', error.code
  end

  private

  def implementation_plan
    timing_a = binding('timing-role', 'timing-type', 'timing a')
    timing_b = binding('timing-role', 'timing-type', 'timing_a')
    supply_a = binding('supply-role', 'supply-type', '温度 域')
    supply_b = binding('supply-role', 'supply-type', 'supply_b')
    left = reference('router', 'r-0-0')
    right = reference('router', 'r-1-0')
    {
      'format' => 'finepaper.noc-domain-implementation-plan',
      'formatVersion' => 1,
      'design' => 'context_fixture',
      'source' => {
        'format' => 'finepaper.noc-domain-constraints', 'formatVersion' => 1
      },
      'realization' => {
        'format' => 'finepaper.noc-domain-realization', 'formatVersion' => 1
      },
      'domainBindings' => [
        domain('timing a', 'timing-type', 'timing-role', [left]),
        domain('timing_a', 'timing-type', 'timing-role', [right]),
        domain('温度 域', 'supply-type', 'supply-role', [left]),
        domain('supply_b', 'supply-type', 'supply-role', [right])
      ],
      'relationBindings' => [derived_relation],
      'entityBindings' => [
        {'element' => left, 'bindings' => [supply_a, timing_a]},
        {'element' => right, 'bindings' => [supply_b, timing_b]}
      ],
      'edgeBindings' => [{
        'edge' => reference('router-link', 'edge 0'),
        'fromElement' => left,
        'toElement' => right,
        'fromBindings' => [supply_a, timing_a],
        'toBindings' => [supply_b, timing_b],
        'stages' => [
          bidirectional_stage,
          directional_stage
        ]
      }]
    }
  end

  def domain(id, type, role, members)
    {
      'domain' => id,
      'domainType' => type,
      'role' => role,
      'name' => id,
      'parameters' => {},
      'members' => members
    }
  end

  def binding(role, type, id)
    {'role' => role, 'domainType' => type, 'domain' => id}
  end

  def reference(kind, id)
    {'kind' => kind, 'id' => id}
  end

  def bidirectional_stage
    {
      'order' => 100,
      'role' => 'timing-boundary',
      'domainType' => 'timing-type',
      'fromDomain' => 'timing a',
      'toDomain' => 'timing_a',
      'policy' => {'source' => 'policy', 'id' => 'timing-policy'},
      'parameters' => {
        'queue-depth' => parameter('integer', 4, 'queueDepth')
      },
      'recipe' => 'async-queue',
      'recipeKind' => 'bidirectional-stage'
    }
  end

  def directional_stage
    {
      'order' => 200,
      'role' => 'translation-boundary',
      'domainType' => 'supply-type',
      'fromDomain' => '温度 域',
      'toDomain' => 'supply_b',
      'policy' => {'source' => 'policy', 'id' => 'supply-policy'},
      'parameters' => {},
      'directions' => [
        {
          'orientation' => 'from-to',
          'recipe' => 'translator',
          'recipeKind' => 'directional-stage',
          'parameters' => {'mode' => parameter('enum', 'raise', 'mode')}
        },
        {
          'orientation' => 'to-from',
          'recipe' => 'translator',
          'recipeKind' => 'directional-stage',
          'parameters' => {'mode' => parameter('enum', 'lower', 'mode')}
        }
      ]
    }
  end

  def derived_relation
    {
      'relationType' => 'derived-from',
      'domainType' => 'timing-type',
      'role' => 'derived-timing-source',
      'recipe' => 'divider',
      'recipeKind' => 'divide-source-binding',
      'fromDomain' => 'timing_a',
      'toDomain' => 'timing a',
      'sourceDomain' => 'timing a',
      'targetDomain' => 'timing_a',
      'parameters' => {'ratio' => parameter('integer', 2, 'divider')},
      'resolved' => {
        'sourceBindingName' => 'frequency',
        'targetBindingName' => 'frequency',
        'sourceBinding' => parameter('number', 1000, 'frequencyMHz'),
        'targetBinding' => parameter('number', 500, 'frequencyMHz'),
        'calculatedTarget' => {'type' => 'number', 'value' => 500.0}
      }
    }
  end

  def parameter(type, value, source_id)
    {
      'type' => type,
      'value' => value,
      'source' => {'kind' => 'fixture-property', 'id' => source_id}
    }
  end

  def deep_copy(value)
    JSON.parse(JSON.generate(value))
  end
end
