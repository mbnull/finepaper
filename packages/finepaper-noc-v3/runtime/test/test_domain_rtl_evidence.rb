# frozen_string_literal: true

require 'digest'
require 'json'
require 'minitest/autorun'
require_relative 'domain_rtl_fixture'
require_relative '../lib/domain_rtl_context'
require_relative '../lib/domain_rtl_evidence'

class TestV3DomainRtlEvidence < Minitest::Test
  def test_builds_deterministic_complete_cdc_evidence
    plan, context, rendering = crossing_fixture
    contents = JSON.pretty_generate(plan) + "\n"
    evidence = build_evidence(context, rendering, contents)

    assert_equal 'finepaper.noc-domain-implementation-evidence',
                 evidence.fetch('format')
    assert_equal Digest::SHA256.hexdigest(contents),
                 evidence.dig('sourcePlan', 'sha256')
    assert_equal true, evidence.dig('claims', 'timingDomainPorts')
    assert_equal true, evidence.dig('claims', 'localResetRelease')
    assert_equal true, evidence.dig('claims', 'clockAsyncFifo')
    assert_equal true, evidence.dig('claims', 'completePlan')
    assert_empty evidence.fetch('deferredPlanItems')
    assert_equal({
      'asyncBoundaries' => 1,
      'deferredPlanItems' => 0,
      'directEdges' => 2,
      'directionalFifos' => 2,
      'physicalEdges' => 3,
      'resetSynchronizers' => 2,
      'usedTimingDomains' => 2
    }, evidence.fetch('summary'))

    infrastructure = evidence.fetch('domainInfrastructure')
    assert_equal %w[clock-left clock-right],
                 infrastructure.map { |entry| entry.fetch('domain') }
    assert_equal [2, 5], infrastructure.map { |entry|
      entry.dig('parameters', 'STAGES', 'value')
    }
    infrastructure.each do |entry|
      assert_equal 'fp_reset_synchronizer', entry.fetch('module')
      assert_match(/\Arendered_top\.u_reset_[A-Za-z_][A-Za-z0-9_]*\z/,
                   entry.fetch('instance'))
      refute entry.fetch('clockPort').start_with?('/')
    end

    edges = evidence.fetch('edgeRealizations')
    assert_equal [
      ['router-link', 'link-r-0-0--r-1-0'],
      ['endpoint-attachment', 'ep_left'],
      ['endpoint-attachment', 'ep_right']
    ], edges.map { |entry| entry.fetch('edge').values_at('kind', 'id') }
    crossing = edges.fetch(0)
    assert_equal 'realized', crossing.fetch('status')
    assert_equal '/edgeBindings/0/stages/0',
                 crossing.dig('planStage', 'path')
    assert_equal %w[from-to to-from], crossing.fetch('directions').map { |entry|
      entry.fetch('orientation')
    }
    forward, reverse = crossing.fetch('directions')
    assert_equal 'clock-left', forward.dig('sourceClock', 'domain')
    assert_equal 'clock-right', forward.dig('destinationClock', 'domain')
    assert_equal 'clock-right', reverse.dig('sourceClock', 'domain')
    assert_equal 'clock-left', reverse.dig('destinationClock', 'domain')
    crossing.fetch('directions').each do |direction|
      assert_equal 'fp_async_ready_valid_fifo', direction.fetch('module')
      assert_equal 8, direction.dig('parameters', 'DEPTH', 'value')
      assert_equal 4, direction.dig('parameters', 'SYNC_STAGES', 'value')
      assert_equal 'FLIT_WIDTH',
                   direction.dig('parameters', 'PAYLOAD_WIDTH', 'name')
    end
    assert edges.drop(1).all? { |entry| entry.fetch('status') == 'direct' }
    assert edges.drop(1).all? { |entry| entry.fetch('directions').empty? }

    reordered = DomainRtlFixture.deep_copy(rendering)
    reordered.fetch('clockDomains').reverse!
    reordered['routerTraffic'] = reordered.fetch('routerTraffic').to_a.reverse.to_h
    reordered['endpointAttachments'] =
      reordered.fetch('endpointAttachments').to_a.reverse.to_h
    assert_equal evidence, build_evidence(context, reordered, contents)

    serialized = JSON.generate(evidence)
    refute_match(%r{/home/|/tmp/}, serialized)
    refute_match(/generatedAt|createdAt|timestamp/i, serialized)
    assert_equal evidence.keys.sort, evidence.keys
  end

  def test_defers_power_and_derived_clock_without_claiming_complete_plan
    plan, = crossing_fixture
    plan.fetch('relationBindings') << derived_clock_relation
    crossing_edge = plan.fetch('edgeBindings').find do |edge|
      edge.dig('edge', 'kind') == 'router-link'
    end
    crossing_edge.fetch('stages') << power_isolation_stage
    context = FinepaperNoc::DomainRtlContext.new(
      DomainRtlFixture.deep_copy(plan)
    )
    rendering = rendering_for(
      context,
      router_rtl_ids: {'r-0-0' => 'xp_left', 'r-1-0' => 'xp_right'}
    )
    evidence = build_evidence(
      context, rendering, JSON.pretty_generate(plan) + "\n"
    )

    assert_equal false, evidence.dig('claims', 'completePlan')
    assert_equal true, evidence.dig('claims', 'clockAsyncFifo')
    deferred = evidence.fetch('deferredPlanItems')
    assert_equal 2, deferred.size
    assert_equal %w[relation edge-stage],
                 deferred.map { |entry| entry.fetch('kind') }
    assert_equal 'renderer.derived_clock_not_materialized',
                 deferred.fetch(0).fetch('reasonCode')
    assert_equal ['derived-clock-divider'],
                 deferred.fetch(0).fetch('recipes')
    assert_equal 'renderer.power_stage_not_materialized',
                 deferred.fetch(1).fetch('reasonCode')
    assert_equal ['power-isolation'], deferred.fetch(1).fetch('recipes')
    assert_equal '/edgeBindings/0/stages/1',
                 deferred.fetch(1).fetch('planPath')
    assert_equal 2, evidence.dig('summary', 'deferredPlanItems')
    assert_equal 2, evidence.dig('summary', 'directionalFifos')
  end

  def test_rendering_must_be_a_bijection_with_the_plan
    plan, context, rendering = crossing_fixture
    contents = JSON.pretty_generate(plan) + "\n"

    missing = DomainRtlFixture.deep_copy(rendering)
    reverse_key = missing.fetch('routerTraffic').keys.find do |key|
      key == %w[xp_right xp_left]
    end
    missing.fetch('routerTraffic').delete(reverse_key)
    error = assert_raises(FinepaperNoc::DomainRtlEvidenceError) do
      build_evidence(context, missing, contents)
    end
    assert_equal 'rtl_evidence.missing_bridge', error.code

    mismatched = DomainRtlFixture.deep_copy(rendering)
    mismatched.fetch('routerTraffic').values.find do |bridge|
      bridge.fetch('crossing')
    end['fifoDepth'] = 16
    error = assert_raises(FinepaperNoc::DomainRtlEvidenceError) do
      build_evidence(context, mismatched, contents)
    end
    assert_equal 'rtl_evidence.fifo_depth_mismatch', error.code

    error = assert_raises(FinepaperNoc::DomainRtlEvidenceError) do
      build_evidence(
        context, rendering, contents,
        top_artifact: '/tmp/rendered_top.v'
      )
    end
    assert_equal 'rtl_evidence.invalid_artifact_path', error.code

    changed_plan = DomainRtlFixture.deep_copy(plan)
    changed_plan['design'] = 'different-design'
    error = assert_raises(FinepaperNoc::DomainRtlEvidenceError) do
      build_evidence(
        context, rendering, JSON.pretty_generate(changed_plan) + "\n"
      )
    end
    assert_equal 'rtl_evidence.plan_mismatch', error.code
  end

  private

  def crossing_fixture
    timing = {
      ['router', 'r-0-0'] => 'clock-left',
      ['endpoint', 'ep_left'] => 'clock-left',
      ['router', 'r-1-0'] => 'clock-right',
      ['endpoint', 'ep_right'] => 'clock-right'
    }
    plan = DomainRtlFixture.implementation_plan(
      design: 'rendered',
      router_ids: %w[r-0-0 r-1-0],
      endpoint_routers: {
        'ep_left' => 'r-0-0', 'ep_right' => 'r-1-0'
      },
      router_links: [{
        id: 'link-r-0-0--r-1-0', from: 'r-0-0', to: 'r-1-0'
      }],
      timing_by_element: timing,
      reset_stages: {'clock-left' => 2, 'clock-right' => 5},
      fifo_depth: 8,
      synchronizer_stages: 4
    )
    context = FinepaperNoc::DomainRtlContext.new(
      DomainRtlFixture.deep_copy(plan)
    )
    rendering = rendering_for(
      context,
      router_rtl_ids: {'r-0-0' => 'xp_left', 'r-1-0' => 'xp_right'}
    )
    [plan, context, rendering]
  end

  def rendering_for(context, router_rtl_ids:)
    active_domains = context.domains_for_role('timing-domain').select do |domain|
      !domain.fetch('members').empty?
    end
    multiple_clocks = active_domains.size > 1
    clock_domains = active_domains.map do |domain|
      token = domain.fetch('token')
      {
        'domain' => domain.fetch('domain'),
        'name' => domain.fetch('name'),
        'token' => token,
        'clockSignal' => multiple_clocks ? "clk_#{token}" : 'clk',
        'resetSignal' => "rst_n_#{token}",
        'resetReleaseStages' => context.parameter_value(
          domain, 'reset-release-stages', expected_type: 'integer'
        )
      }
    end
    clock_by_domain = clock_domains.to_h do |domain|
      [domain.fetch('domain'), domain]
    end
    entity_signals = context.entities.values.to_h do |entry|
      element = entry.fetch('element')
      timing = context.entity_domain(
        element.fetch('kind'), element.fetch('id'), 'timing-domain'
      )
      [[element.fetch('kind'), element.fetch('id')],
       clock_by_domain.fetch(timing.fetch('domain'))]
    end

    router_traffic = {}
    endpoint_attachments = {}
    context.edges.each_value do |edge|
      reference = edge.fetch('edge')
      if reference.fetch('kind') == 'router-link'
        FinepaperNoc::DomainRtlContext::ORIENTATIONS.each do |orientation|
          traffic = context.traffic('router-link', reference.fetch('id'),
                                    orientation)
          producer = router_rtl_ids.fetch(traffic.dig('producer', 'id'))
          consumer = router_rtl_ids.fetch(traffic.dig('consumer', 'id'))
          router_traffic[[producer, consumer]] = bridge_for(
            context, entity_signals, reference, orientation,
            "link_#{producer}_to_#{consumer}"
          )
        end
      else
        endpoint_id = reference.fetch('id')
        endpoint_attachments[endpoint_id] = {
          'routerToEndpoint' => bridge_for(
            context, entity_signals, reference, 'from-to',
            "router_to_ni_#{endpoint_id}"
          ),
          'endpointToRouter' => bridge_for(
            context, entity_signals, reference, 'to-from',
            "ni_#{endpoint_id}_to_router"
          )
        }
      end
    end

    {
      'clockDomains' => clock_domains,
      'routerSignals' => {},
      'endpointSignals' => {},
      'routerTraffic' => router_traffic,
      'endpointAttachments' => endpoint_attachments
    }
  end

  def bridge_for(context, entity_signals, edge, orientation, base)
    traffic = context.traffic(edge.fetch('kind'), edge.fetch('id'), orientation)
    producer = traffic.fetch('producer')
    consumer = traffic.fetch('consumer')
    fifo = context.edge_stage(
      edge.fetch('kind'), edge.fetch('id'), 'clock-async-fifo'
    )
    crossing = !fifo.nil?
    bridge = {
      'edge' => DomainRtlFixture.deep_copy(edge),
      'orientation' => orientation,
      'producer' => DomainRtlFixture.deep_copy(producer),
      'consumer' => DomainRtlFixture.deep_copy(consumer),
      'baseSignal' => base,
      'sourceSignal' => crossing ? "#{base}_src" : base,
      'destinationSignal' => crossing ? "#{base}_dst" : base,
      'sourceDomain' => entity_signals.fetch(
        [producer.fetch('kind'), producer.fetch('id')]
      ),
      'destinationDomain' => entity_signals.fetch(
        [consumer.fetch('kind'), consumer.fetch('id')]
      ),
      'crossing' => crossing
    }
    return bridge unless crossing

    bridge.merge(
      'instance' => "u_cdc_#{base}",
      'fifoDepth' => context.parameter_value(
        fifo, 'fifo-depth', expected_type: 'integer'
      ),
      'synchronizerStages' => context.parameter_value(
        fifo, 'metastability-stages', expected_type: 'integer'
      )
    )
  end

  def derived_clock_relation
    {
      'relationType' => 'derived-from',
      'domainType' => 'clock',
      'role' => 'derived-timing-source',
      'recipe' => 'derived-clock-divider',
      'recipeKind' => 'divide-source-binding',
      'fromDomain' => 'clock-right',
      'toDomain' => 'clock-left',
      'sourceDomain' => 'clock-left',
      'targetDomain' => 'clock-right',
      'parameters' => {
        'division-ratio' => DomainRtlFixture.parameter(
          'integer', 2, 'relation-property', 'divider'
        )
      },
      'resolved' => {
        'sourceBindingName' => 'nominal-frequency-mhz',
        'targetBindingName' => 'nominal-frequency-mhz',
        'sourceBinding' => DomainRtlFixture.parameter(
          'number', 1000, 'domain-property', 'frequencyMHz'
        ),
        'targetBinding' => DomainRtlFixture.parameter(
          'number', 500, 'domain-property', 'frequencyMHz'
        ),
        'calculatedTarget' => {'type' => 'number', 'value' => 500.0}
      }
    }
  end

  def power_isolation_stage
    {
      'order' => 200,
      'role' => 'power-isolation-boundary',
      'domainType' => 'power',
      'fromDomain' => 'power-test',
      'toDomain' => 'power-test',
      'policy' => {'source' => 'policy', 'id' => 'power-policy'},
      'parameters' => {},
      'recipe' => 'power-isolation',
      'recipeKind' => 'bidirectional-stage'
    }
  end

  def build_evidence(context, rendering, contents, **overrides)
    defaults = {
      context: context,
      domain_rendering: rendering,
      top_module: 'rendered_top',
      top_artifact: 'rendered_top.v',
      filelist_artifact: 'filelist.f',
      source_plan_artifact: 'rendered_domain_implementation.json',
      source_plan_contents: contents,
      async_reset_port: 'rst_n',
      payload_width_parameter: 'FLIT_WIDTH'
    }
    FinepaperNoc::DomainRtlEvidenceBuilder.build(**defaults.merge(overrides))
  end
end
