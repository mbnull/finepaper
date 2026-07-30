# frozen_string_literal: true

module DomainRtlFixture
  module_function

  def implementation_plan(design:, router_ids:, endpoint_routers:, router_links:,
                          timing_by_element:, reset_stages: {}, fifo_depth: 4,
                          synchronizer_stages: 3)
    routers = router_ids.map { |id| reference('router', id) }
    endpoints = endpoint_routers.keys.map { |id| reference('endpoint', id) }
    elements = routers + endpoints
    timing_ids = timing_by_element.values.uniq.sort
    supply_id = 'power-test'

    entity_bindings = elements.map do |element|
      timing_id = timing_by_element.fetch(
        [element.fetch('kind'), element.fetch('id')]
      )
      {
        'element' => element,
        'bindings' => [
          binding('supply-domain', 'power', supply_id),
          binding('timing-domain', 'clock', timing_id)
        ]
      }
    end
    entity_by_key = entity_bindings.to_h do |entry|
      element = entry.fetch('element')
      [[element.fetch('kind'), element.fetch('id')], entry]
    end

    edges = router_links.map do |link|
      edge_binding(
        reference('router-link', link.fetch(:id)),
        reference('router', link.fetch(:from)),
        reference('router', link.fetch(:to)),
        entity_by_key,
        timing_by_element,
        fifo_depth,
        synchronizer_stages
      )
    end
    endpoint_routers.sort.each do |endpoint_id, router_id|
      edges << edge_binding(
        reference('endpoint-attachment', endpoint_id),
        reference('router', router_id),
        reference('endpoint', endpoint_id),
        entity_by_key,
        timing_by_element,
        fifo_depth,
        synchronizer_stages
      )
    end

    {
      'format' => 'finepaper.noc-domain-implementation-plan',
      'formatVersion' => 1,
      'design' => design,
      'source' => {
        'format' => 'finepaper.noc-domain-constraints', 'formatVersion' => 1
      },
      'realization' => {
        'format' => 'finepaper.noc-domain-realization', 'formatVersion' => 1
      },
      'domainBindings' => timing_ids.map do |id|
        members = elements.select do |element|
          timing_by_element.fetch(
            [element.fetch('kind'), element.fetch('id')]
          ) == id
        end
        domain(
          id, 'clock', 'timing-domain', members,
          {
            'reset-release-stages' => parameter(
              'integer', reset_stages.fetch(id, 2),
              'domain-property', 'resetReleaseStages'
            )
          }
        )
      end + [domain(supply_id, 'power', 'supply-domain', elements, {})],
      'relationBindings' => [],
      'entityBindings' => entity_bindings,
      'edgeBindings' => edges
    }
  end

  def domain(id, type, role, members, parameters)
    {
      'domain' => id,
      'domainType' => type,
      'role' => role,
      'name' => id,
      'parameters' => parameters,
      'members' => deep_copy(members)
    }
  end

  def edge_binding(edge, from, to, entity_by_key, timing_by_element,
                   fifo_depth, synchronizer_stages)
    from_key = [from.fetch('kind'), from.fetch('id')]
    to_key = [to.fetch('kind'), to.fetch('id')]
    from_timing = timing_by_element.fetch(from_key)
    to_timing = timing_by_element.fetch(to_key)
    stages = if from_timing == to_timing
               []
             else
               [async_fifo_stage(
                 from_timing, to_timing, fifo_depth, synchronizer_stages
               )]
             end
    {
      'edge' => edge,
      'fromElement' => from,
      'toElement' => to,
      'fromBindings' => deep_copy(entity_by_key.fetch(from_key).fetch('bindings')),
      'toBindings' => deep_copy(entity_by_key.fetch(to_key).fetch('bindings')),
      'stages' => stages
    }
  end

  def async_fifo_stage(from_domain, to_domain, fifo_depth, synchronizer_stages)
    {
      'order' => 100,
      'role' => 'timing-boundary',
      'domainType' => 'clock',
      'fromDomain' => from_domain,
      'toDomain' => to_domain,
      'policy' => {'source' => 'policy', 'id' => "#{from_domain}-to-#{to_domain}"},
      'parameters' => {
        'fifo-depth' => parameter(
          'integer', fifo_depth, 'crossing-property', 'fifoDepth'
        ),
        'metastability-stages' => parameter(
          'integer', synchronizer_stages,
          'crossing-property', 'synchronizerStages'
        )
      },
      'recipe' => 'clock-async-fifo',
      'recipeKind' => 'bidirectional-stage'
    }
  end

  def binding(role, type, id)
    {'role' => role, 'domainType' => type, 'domain' => id}
  end

  def parameter(type, value, source_kind, source_id)
    {
      'type' => type,
      'value' => value,
      'source' => {'kind' => source_kind, 'id' => source_id}
    }
  end

  def reference(kind, id)
    {'kind' => kind, 'id' => id}
  end

  def deep_copy(value)
    Marshal.load(Marshal.dump(value))
  end
end
