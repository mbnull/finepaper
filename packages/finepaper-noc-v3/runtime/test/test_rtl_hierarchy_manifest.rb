# frozen_string_literal: true

$LOAD_PATH.unshift File.expand_path('../legacy-generator/src/ruby', __dir__)

require 'json'
require 'minitest/autorun'
require 'open3'
require 'tmpdir'
require 'generator/rtl_generator'
require 'model/connection'
require 'model/endpoint'
require 'model/noc_config'
require 'model/xp'
require 'parser/json_parser'
require 'topology/topology_expander'
require_relative 'domain_rtl_fixture'
require_relative '../lib/domain_rtl_context'
require_relative '../lib/rtl_hierarchy_manifest'

class TestV3RtlHierarchyManifest < Minitest::Test
  LEGACY_ROOT = File.expand_path('../legacy-generator', __dir__)
  TEMPLATE_DIR = File.join(LEGACY_ROOT, 'template')
  ADAPTER = File.expand_path('../bin/generate', __dir__)
  PARAMETERS = {
    'data_width' => 64,
    'flit_width' => 128,
    'addr_width' => 32
  }.freeze

  def test_builder_is_deterministic_and_keeps_instances_top_scope_relative
    context = split_power_context
    first = populated_builder(context).build
    second = populated_builder(context, reverse: true).build

    assert_equal first, second
    assert_equal 'finepaper.noc-rtl-hierarchy', first.fetch('format')
    assert_equal 1, first.fetch('formatVersion')
    assert_equal 'hierarchy_fixture_top', first.fetch('topModule')
    assert_equal 'hierarchy_fixture_top.v', first.fetch('topArtifact')
    relative_instances = first.fetch('elements').all? do |element|
      !element.fetch('instance').start_with?('hierarchy_fixture_top')
    end
    assert relative_instances
    element_references = first.fetch('elements').map do |element|
      element.fetch('element').values_at('kind', 'id')
    end
    assert_equal [
      %w[endpoint ep_left], %w[endpoint ep_right],
      %w[router r-0-0], %w[router r-1-0]
    ], element_references

    crossing = first.fetch('edgeDirections').select do |direction|
      direction.dig('edge', 'kind') == 'router-link'
    end
    crossing_orientations = crossing.map do |direction|
      direction.fetch('orientation')
    end
    assert_equal %w[from-to to-from], crossing_orientations
    forward, reverse = crossing
    assert_equal 'u_router_left', forward.fetch('producerInstance')
    assert_equal 'u_router_right', forward.fetch('consumerInstance')
    assert_equal 'power-left', forward.fetch('sourceSupplyDomain')
    assert_equal 'power-right', forward.fetch('destinationSupplyDomain')
    assert_equal 'power-right', reverse.fetch('sourceSupplyDomain')
    assert_equal 'power-left', reverse.fetch('destinationSupplyDomain')
    crossing.each do |direction|
      assert_equal 'infrastructure', direction.dig('bridge', 'placement')
      refute direction.dig('bridge', 'instance').start_with?(
        'hierarchy_fixture_top'
      )
      assert_equal %w[name payload ready valid],
                   direction.fetch('sourceBundle').keys.sort
      assert_equal 'deferred', direction.dig('powerBoundary', 'status')
      assert_equal(
        'rtl_hierarchy.infrastructure_bridge_supply_unowned',
        direction.dig('powerBoundary', 'reasonCode')
      )
    end
    forward_source_ready = signal_flow(forward, 'source', 'ready')
    assert_equal 'consumer-to-producer',
                 forward_source_ready.fetch('direction')
    assert_equal({
      'instance' => forward.dig('bridge', 'instance'),
      'pin' => 'src_ready_o'
    }, forward_source_ready.fetch('driver'))
    assert_equal({
      'instance' => 'u_router_left',
      'pin' => forward.dig('producerPins', 'ready')
    }, forward_source_ready.fetch('receiver'))
    forward_destination_ready = signal_flow(forward, 'destination', 'ready')
    assert_equal({
      'instance' => 'u_router_right',
      'pin' => forward.dig('consumerPins', 'ready')
    }, forward_destination_ready.fetch('driver'))
    assert_equal({
      'instance' => forward.dig('bridge', 'instance'),
      'pin' => 'dst_ready_i'
    }, forward_destination_ready.fetch('receiver'))
    direct = first.fetch('edgeDirections') - crossing
    assert direct.all? { |direction| direction.fetch('bridge').nil? }
    shared_direct_bundles = direct.all? do |direction|
      direction.fetch('sourceBundle') == direction.fetch('destinationBundle')
    end
    assert shared_direct_bundles
    resolvable = direct.select do |direction|
      direction.fetch('sourceSupplyDomain') !=
        direction.fetch('destinationSupplyDomain')
    end
    assert_equal 2, resolvable.size
    all_resolvable = resolvable.all? do |direction|
      direction.dig('powerBoundary', 'status') == 'resolvable'
    end
    assert all_resolvable
    same_power = direct - resolvable
    all_same_power = same_power.all? do |direction|
      direction.dig('powerBoundary', 'status') == 'none'
    end
    assert all_same_power
    assert_canonical_hashes(first)
  end

  def test_builder_rejects_prefixed_instances_and_incomplete_emission
    context = split_power_context
    builder = FinepaperNoc::RtlHierarchyManifestBuilder.new(
      context: context,
      design: 'hierarchy_fixture',
      top_module: 'hierarchy_fixture_top',
      top_artifact: 'hierarchy_fixture_top.v'
    )
    error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
      builder.register_element(
        element: {'kind' => 'router', 'id' => 'r-0-0'},
        module_name: 'xp_router_e',
        instance: 'hierarchy_fixture_top/u_router_left'
      )
    end
    assert_equal 'rtl_hierarchy.invalid_top_scope_path', error.code

    error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
      populated_builder(context, omit_last_direction: true).build
    end
    assert_equal 'rtl_hierarchy.incomplete_edge_directions', error.code

    error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
      populated_builder(context, bridge_placement: 'power-left')
    end
    assert_equal 'rtl_hierarchy.invalid_bridge_placement', error.code

    error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
      populated_builder(context, duplicate_signals: true)
    end
    assert_equal 'rtl_hierarchy.duplicate_signal_path', error.code
  end

  def test_builder_requires_an_exact_control_port_bijection_without_aliasing
    context = split_power_context
    expected = JSON.parse(JSON.generate([{
      'id' => 'isolate',
      'signal' => 'isolate_req',
      'source' => 'top-port',
      'direction' => 'input'
    }]))
    builder = populated_builder(
      context,
      expected_logic_control_ports: expected,
      register_logic_control_ports: false
    )
    expected.fetch(0).fetch('signal') << '_caller_mutation'
    refute expected.frozen?
    refute expected.fetch(0).fetch('signal').frozen?

    error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
      builder.build
    end
    assert_equal 'rtl_hierarchy.incomplete_logic_control_ports', error.code

    builder.register_logic_control_port(
      control_id: 'isolate', signal: 'isolate_req',
      source: 'top-port', direction: 'input'
    )
    assert_equal [{
      'direction' => 'input',
      'id' => 'isolate',
      'signal' => 'isolate_req',
      'source' => 'top-port'
    }], builder.build.fetch('logicControlPorts')

    error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
      populated_builder(context).register_logic_control_port(
        control_id: 'unexpected', signal: 'unexpected_req',
        source: 'top-port', direction: 'input'
      )
    end
    assert_equal 'rtl_hierarchy.unknown_logic_control_port', error.code
  end

  def test_generator_manifest_names_are_present_in_the_emitted_top
    plan = split_power_plan
    xps = [
      Xp.new('xp_left', 0, 0, ['ep_left']),
      Xp.new('xp_right', 1, 0, ['ep_right'])
    ]
    endpoints = [
      Endpoint.new('ep_left', 'master', 'axi4', 64),
      Endpoint.new('ep_right', 'slave', 'axi4', 64)
    ]
    connections = [Connection.new('xp_left', 'xp_right', 'east')]
    noc = NocConfig.new(
      'hierarchy_fixture', '3.0', PARAMETERS, xps, connections, endpoints, plan
    )

    Dir.mktmpdir('finepaper-v3-hierarchy-') do |directory|
      generator = RtlGenerator.new(noc, TEMPLATE_DIR)
      generator.generate_partitioned(directory)
      top = File.read(File.join(directory, 'hierarchy_fixture_top.v'))
      hierarchy_path = File.join(
        directory, 'hierarchy_fixture_rtl_hierarchy.json'
      )
      manifest_text = File.read(hierarchy_path)
      manifest = JSON.parse(manifest_text)

      manifest.fetch('elements').each do |element|
        assert_includes top, " #{element.fetch('instance')} ("
        assert_includes top, "#{element.fetch('module')} #("
      end
      manifest.fetch('resetSynchronizers').each do |reset|
        assert_includes top, " #{reset.fetch('instance')} ("
        assert_includes top, ".clk(#{reset.fetch('clockSignal')})"
      end
      manifest.fetch('edgeDirections').each do |direction|
        [direction.fetch('sourceBundle'),
         direction.fetch('destinationBundle')].each do |bundle|
          assert_includes top, bundle.fetch('payload')
          assert_includes top, bundle.fetch('valid')
          assert_includes top, bundle.fetch('ready')
        end
        bridge = direction.fetch('bridge')
        assert_includes top, " #{bridge.fetch('instance')} (" if bridge
        direction.fetch('signalFlows').each do |flow|
          signal = flow.fetch('signal')
          %w[driver receiver].each do |role|
            terminal = flow.fetch(role)
            assert_includes top,
                            ".#{terminal.fetch('pin')}(#{signal})"
          end
        end
      end
      bridge_count = manifest.fetch('edgeDirections').count do |direction|
        !direction.fetch('bridge').nil?
      end
      assert_equal 2, bridge_count

      forward = manifest.fetch('edgeDirections').find do |direction|
        direction.dig('edge', 'kind') == 'router-link' &&
          direction.fetch('orientation') == 'from-to'
      end
      refute_nil forward
      assert_equal 'xp_router_e000', module_for_instance(
        manifest, forward.fetch('producerInstance')
      )
      assert_equal 'xp_router_0w00', module_for_instance(
        manifest, forward.fetch('consumerInstance')
      )
      assert_equal 'fp_async_ready_valid_fifo',
                   forward.dig('bridge', 'module')
      assert_equal({
        'instance' => 'u_xp_left', 'pin' => 'flit_out_e'
      }, signal_flow(forward, 'source', 'payload').fetch('driver'))
      assert_equal({
        'instance' => forward.dig('bridge', 'instance'),
        'pin' => 'src_payload_i'
      }, signal_flow(forward, 'source', 'payload').fetch('receiver'))
      assert_equal({
        'instance' => forward.dig('bridge', 'instance'),
        'pin' => 'dst_payload_o'
      }, signal_flow(forward, 'destination', 'payload').fetch('driver'))
      assert_equal({
        'instance' => 'u_xp_right', 'pin' => 'flit_in_w'
      }, signal_flow(forward, 'destination', 'payload').fetch('receiver'))
      assert_equal({
        'instance' => forward.dig('bridge', 'instance'),
        'pin' => 'src_ready_o'
      }, signal_flow(forward, 'source', 'ready').fetch('driver'))
      assert_equal({
        'instance' => 'u_xp_left', 'pin' => 'flit_out_e_ready'
      }, signal_flow(forward, 'source', 'ready').fetch('receiver'))
      assert_equal({
        'instance' => 'u_xp_right', 'pin' => 'flit_in_w_ready'
      }, signal_flow(forward, 'destination', 'ready').fetch('driver'))
      assert_equal({
        'instance' => forward.dig('bridge', 'instance'),
        'pin' => 'dst_ready_i'
      }, signal_flow(forward, 'destination', 'ready').fetch('receiver'))

      left_attachment = manifest.fetch('edgeDirections').find do |direction|
        direction.dig('edge', 'id') == 'ep_left' &&
          direction.fetch('orientation') == 'from-to'
      end
      assert_equal({
        'instance' => 'u_xp_left', 'pin' => 'local0_flit_out'
      }, signal_flow(left_attachment, 'direct', 'payload').fetch('driver'))
      assert_equal({
        'instance' => 'u_ni_ep_left', 'pin' => 'ep0_router_flit_out'
      }, signal_flow(left_attachment, 'direct', 'payload').fetch('receiver'))
      assert_equal({
        'instance' => 'u_ni_ep_left',
        'pin' => 'ep0_router_flit_out_ready'
      }, signal_flow(left_attachment, 'direct', 'ready').fetch('driver'))
      assert_equal({
        'instance' => 'u_xp_left', 'pin' => 'local0_flit_out_ready'
      }, signal_flow(left_attachment, 'direct', 'ready').fetch('receiver'))

      second_directory = File.join(directory, 'second')
      generator.generate_partitioned(second_directory)
      assert_equal manifest_text, File.read(File.join(
        second_directory, 'hierarchy_fixture_rtl_hierarchy.json'
      ))
    end
  end

  def test_generator_materializes_only_top_port_power_controls_deterministically
    controls = [
      power_control('save', 'save_req', 'upf-port'),
      power_control('switch', 'power_enable', 'top-port'),
      power_control('isolate', 'isolate_req', 'top-port')
    ]
    first_noc = hierarchy_noc(power_intent_plan(controls))
    second_noc = hierarchy_noc(power_intent_plan(controls.reverse))

    Dir.mktmpdir('finepaper-v3-power-controls-') do |directory|
      first_dir = File.join(directory, 'first')
      second_dir = File.join(directory, 'second')
      RtlGenerator.new(first_noc, TEMPLATE_DIR).generate_partitioned(first_dir)
      RtlGenerator.new(second_noc, TEMPLATE_DIR).generate_partitioned(second_dir)

      first_top = File.read(File.join(first_dir, 'hierarchy_fixture_top.v'))
      second_top = File.read(File.join(second_dir, 'hierarchy_fixture_top.v'))
      first_manifest_text = File.read(File.join(
        first_dir, 'hierarchy_fixture_rtl_hierarchy.json'
      ))
      second_manifest_text = File.read(File.join(
        second_dir, 'hierarchy_fixture_rtl_hierarchy.json'
      ))
      assert_equal first_top, second_top
      assert_equal first_manifest_text, second_manifest_text
      assert_equal 1, first_top.scan(/input\s+logic\s+isolate_req\b/).size
      assert_equal 1, first_top.scan(/input\s+logic\s+power_enable\b/).size
      refute_match(/input\s+logic\s+save_req\b/, first_top)

      manifest = JSON.parse(first_manifest_text)
      assert_equal [
        {
          'direction' => 'input', 'id' => 'isolate',
          'signal' => 'isolate_req', 'source' => 'top-port'
        },
        {
          'direction' => 'input', 'id' => 'switch',
          'signal' => 'power_enable', 'source' => 'top-port'
        }
      ], manifest.fetch('logicControlPorts')
    end
  end

  def test_generator_rejects_control_collisions_before_writing_rtl
    baseline = RtlGenerator.new(hierarchy_noc, TEMPLATE_DIR)
    context = baseline.build_domain_rtl_context
    baseline.validate_domain_rtl_context!(context)
    rendering = baseline.build_domain_rtl_rendering(context)
    clock_signals = rendering.fetch('clockDomains').map do |domain|
      domain.fetch('clockSignal')
    end
    assert_equal 2, clock_signals.size
    collisions = ['rst_n', *clock_signals, 'ep_left_flit_in']

    Dir.mktmpdir('finepaper-v3-power-control-collision-') do |directory|
      collisions.each_with_index do |signal, index|
        noc = hierarchy_noc(power_intent_plan([
          power_control('conflict', signal, 'top-port')
        ]))
        output = File.join(directory, index.to_s)
        error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
          RtlGenerator.new(noc, TEMPLATE_DIR).generate_partitioned(output)
        end
        assert_equal 'rtl_hierarchy.logic_control_port_collision', error.code
        refute Dir.exist?(output)
      end
    end
  end

  def test_generator_validates_power_plan_identity_and_control_shape
    valid_plan = power_intent_plan([
      power_control('isolate', 'isolate_req', 'top-port')
    ])
    cases = [
      ['format', 'unexpected', 'rtl_hierarchy.invalid_power_intent_plan_format'],
      ['formatVersion', 2, 'rtl_hierarchy.invalid_power_intent_plan_version'],
      ['design', 'another_design', 'rtl_hierarchy.power_intent_design_mismatch']
    ]
    cases.each do |key, value, code|
      plan = JSON.parse(JSON.generate(valid_plan))
      plan[key] = value
      generator = RtlGenerator.new(hierarchy_noc(plan), TEMPLATE_DIR)
      context = generator.build_domain_rtl_context
      rendering = generator.build_domain_rtl_rendering(context)
      error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
        generator.send(:build_logic_control_ports, rendering)
      end
      assert_equal code, error.code
    end

    invalid_signal = JSON.parse(JSON.generate(valid_plan))
    invalid_signal.dig('controls', 0)['signal'] = 'bad.signal'
    generator = RtlGenerator.new(hierarchy_noc(invalid_signal), TEMPLATE_DIR)
    context = generator.build_domain_rtl_context
    rendering = generator.build_domain_rtl_rendering(context)
    error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
      generator.send(:build_logic_control_ports, rendering)
    end
    assert_equal 'rtl_hierarchy.invalid_logic_control_identifier', error.code
  end

  def test_parser_and_topology_expander_copy_the_optional_power_plan
    plan = JSON.parse(JSON.generate(power_intent_plan([
      power_control('isolate', 'isolate_req', 'top-port')
    ])))
    graph = {
      'schema' => 'finepaper-ipcore-graph-v1',
      'name' => 'hierarchy_fixture',
      'version' => '3.0',
      'ipcore_state' => [{
        'ipcore' => 'finepaper.noc',
        'state' => {
          'global_parameters' => PARAMETERS.merge(
            'mesh' => {'width' => 1, 'height' => 1}
          ),
          'power_intent_plan' => plan
        }
      }],
      'modules' => [],
      'connections' => []
    }

    Dir.mktmpdir('finepaper-v3-power-plan-parser-') do |directory|
      path = File.join(directory, 'graph.json')
      File.write(path, JSON.pretty_generate(graph))
      parsed = JsonParser.parse(path)
      assert_equal plan, parsed.power_intent_plan
      refute_same plan, parsed.power_intent_plan
      refute_same plan.dig('controls', 0, 'signal'),
                  parsed.power_intent_plan.dig('controls', 0, 'signal')
      plan.dig('controls', 0, 'signal').replace('caller_mutation')
      assert_equal 'isolate_req',
                   parsed.power_intent_plan.dig('controls', 0, 'signal')

      expanded = TopologyExpander.expand(parsed)
      assert_equal 1, expanded.xps.size
      refute_same parsed.power_intent_plan, expanded.power_intent_plan
      parsed.power_intent_plan.dig('controls', 0, 'signal')
            .replace('parsed_mutation')
      assert_equal 'isolate_req',
                   expanded.power_intent_plan.dig('controls', 0, 'signal')
      refute expanded.power_intent_plan.frozen?
      refute expanded.power_intent_plan.dig('controls', 0, 'signal').frozen?
    end
  end

  def test_adapter_classifies_hierarchy_as_a_first_class_artifact
    Dir.mktmpdir('finepaper-v3-hierarchy-artifact-') do |directory|
      design_path = File.join(directory, 'design.json')
      output_path = File.join(directory, 'output')
      result_path = File.join(directory, 'result.json')
      File.write(design_path, JSON.pretty_generate(minimal_design) + "\n")
      stdout, stderr, status = Open3.capture3(
        'ruby', ADAPTER, 'generate',
        '--design', design_path,
        '--output', output_path,
        '--result', result_path
      )
      assert status.success?, "adapter failed:\n#{stdout}#{stderr}"
      result = JSON.parse(File.read(result_path))
      hierarchy = result.fetch('artifacts').find do |artifact|
        artifact.fetch('type') == 'rtl-hierarchy'
      end
      refute_nil hierarchy
      assert_equal 'artifact_mesh_rtl_hierarchy.json',
                   hierarchy.fetch('path')
      assert_equal false, hierarchy.fetch('primary')
      assert File.file?(File.join(output_path, hierarchy.fetch('path')))
    end
  end

  def test_emitter_fails_closed_when_a_router_port_is_not_unique
    left = Xp.new('xp_left', 0, 0, ['ep_left'])
    right = Xp.new('xp_right', 1, 0, ['ep_right'])
    noc = NocConfig.new(
      'hierarchy_fixture', '3.0', PARAMETERS, [left, right],
      [Connection.new('xp_left', 'xp_right', 'east')],
      [
        Endpoint.new('ep_left', 'master', 'axi4', 64),
        Endpoint.new('ep_right', 'slave', 'axi4', 64)
      ],
      split_power_plan
    )
    generator = RtlGenerator.new(noc, TEMPLATE_DIR)
    directions = generator.send(:xp_link_directions_by_id)
    directions.fetch(left.id) << directions.fetch(left.id).first.dup

    error = assert_raises(FinepaperNoc::RtlHierarchyManifestError) do
      generator.send(
        :emitted_router_port!, left, right,
        'link-r-0-0--r-1-0', directions
      )
    end
    assert_equal 'rtl_hierarchy.ambiguous_emitted_router_port', error.code
  end

  private

  def split_power_context
    FinepaperNoc::DomainRtlContext.new(split_power_plan)
  end

  def split_power_plan
    timing = {
      ['router', 'r-0-0'] => 'clock-left',
      ['endpoint', 'ep_left'] => 'clock-left',
      ['router', 'r-1-0'] => 'clock-right',
      ['endpoint', 'ep_right'] => 'clock-right'
    }
    plan = DomainRtlFixture.implementation_plan(
      design: 'hierarchy_fixture',
      router_ids: %w[r-0-0 r-1-0],
      endpoint_routers: {
        'ep_left' => 'r-0-0', 'ep_right' => 'r-1-0'
      },
      router_links: [{
        id: 'link-r-0-0--r-1-0', from: 'r-0-0', to: 'r-1-0'
      }],
      timing_by_element: timing
    )
    plan.fetch('domainBindings').reject! do |domain|
      domain.fetch('domain') == 'power-test'
    end
    left = [
      DomainRtlFixture.reference('router', 'r-0-0'),
      DomainRtlFixture.reference('endpoint', 'ep_left'),
      DomainRtlFixture.reference('endpoint', 'ep_right')
    ]
    right = [DomainRtlFixture.reference('router', 'r-1-0')]
    plan.fetch('domainBindings') << DomainRtlFixture.domain(
      'power-left', 'power', 'supply-domain', left, {}
    )
    plan.fetch('domainBindings') << DomainRtlFixture.domain(
      'power-right', 'power', 'supply-domain', right, {}
    )
    entity_by_key = plan.fetch('entityBindings').to_h do |entry|
      element = entry.fetch('element')
      key = element.values_at('kind', 'id')
      supply = key.last.start_with?('ep_') || key.last == 'r-0-0' ?
        'power-left' : 'power-right'
      entry.fetch('bindings').find do |binding|
        binding.fetch('role') == 'supply-domain'
      end['domain'] = supply
      [key, entry]
    end
    plan.fetch('edgeBindings').each do |edge|
      from = edge.fetch('fromElement').values_at('kind', 'id')
      to = edge.fetch('toElement').values_at('kind', 'id')
      edge['fromBindings'] = DomainRtlFixture.deep_copy(
        entity_by_key.fetch(from).fetch('bindings')
      )
      edge['toBindings'] = DomainRtlFixture.deep_copy(
        entity_by_key.fetch(to).fetch('bindings')
      )
    end
    router_edge = plan.fetch('edgeBindings').find do |edge|
      edge.dig('edge', 'kind') == 'router-link'
    end
    router_edge.fetch('stages') << {
      'order' => 200,
      'role' => 'power-isolation-boundary',
      'domainType' => 'power',
      'fromDomain' => 'power-left',
      'toDomain' => 'power-right',
      'policy' => {'source' => 'policy', 'id' => 'power-left-to-right'},
      'parameters' => {},
      'recipe' => 'power-isolation',
      'recipeKind' => 'bidirectional-stage'
    }
    right_attachment = plan.fetch('edgeBindings').find do |edge|
      edge.dig('edge', 'kind') == 'endpoint-attachment' &&
        edge.dig('edge', 'id') == 'ep_right'
    end
    right_attachment.fetch('stages') << {
      'order' => 200,
      'role' => 'power-isolation-boundary',
      'domainType' => 'power',
      'fromDomain' => 'power-right',
      'toDomain' => 'power-left',
      'policy' => {'source' => 'policy', 'id' => 'power-right-to-left'},
      'parameters' => {},
      'recipe' => 'power-isolation',
      'recipeKind' => 'bidirectional-stage'
    }
    plan
  end

  def hierarchy_noc(power_plan = nil)
    xps = [
      Xp.new('xp_left', 0, 0, ['ep_left']),
      Xp.new('xp_right', 1, 0, ['ep_right'])
    ]
    NocConfig.new(
      'hierarchy_fixture', '3.0', PARAMETERS, xps,
      [Connection.new('xp_left', 'xp_right', 'east')],
      [
        Endpoint.new('ep_left', 'master', 'axi4', 64),
        Endpoint.new('ep_right', 'slave', 'axi4', 64)
      ],
      split_power_plan,
      power_plan
    )
  end

  def power_intent_plan(controls)
    {
      'format' => 'finepaper.noc-power-intent-plan',
      'formatVersion' => 1,
      'design' => 'hierarchy_fixture',
      'controls' => controls
    }
  end

  def power_control(id, signal, source)
    {
      'id' => id,
      'signal' => signal,
      'source' => source,
      'activeSense' => 'high',
      'ownerDomain' => 'power-left'
    }
  end

  def populated_builder(context, reverse: false, omit_last_direction: false,
                        bridge_placement: 'infrastructure',
                        duplicate_signals: false,
                        expected_logic_control_ports: [],
                        register_logic_control_ports: true)
    builder = FinepaperNoc::RtlHierarchyManifestBuilder.new(
      context: context,
      design: 'hierarchy_fixture',
      top_module: 'hierarchy_fixture_top',
      top_artifact: 'hierarchy_fixture_top.v',
      expected_logic_control_ports: expected_logic_control_ports
    )
    if register_logic_control_ports
      controls = expected_logic_control_ports.dup
      controls.reverse! if reverse
      controls.each do |control|
        builder.register_logic_control_port(
          control_id: control.fetch('id'),
          signal: control.fetch('signal'),
          source: control.fetch('source'),
          direction: control.fetch('direction')
        )
      end
    end
    instances = {
      %w[router r-0-0] => ['xp_router_e', 'u_router_left'],
      %w[router r-1-0] => ['xp_router_w', 'u_router_right'],
      %w[endpoint ep_left] => ['ni_bridge_left', 'u_ni_ep_left'],
      %w[endpoint ep_right] => ['ni_bridge_right', 'u_ni_ep_right']
    }
    registrations = instances.to_a
    registrations.reverse! if reverse
    registrations.each do |reference, names|
      builder.register_element(
        element: {'kind' => reference.fetch(0), 'id' => reference.fetch(1)},
        module_name: names.fetch(0), instance: names.fetch(1)
      )
    end
    timing_domains = context.domains_for_role('timing-domain').reject do |domain|
      domain.fetch('members').empty?
    end
    timing_domains.reverse! if reverse
    timing_domains.each do |domain|
      token = domain.fetch('token')
      builder.register_reset_synchronizer(
        timing_domain: domain.fetch('domain'),
        module_name: 'fp_reset_synchronizer',
        instance: "u_reset_#{token}",
        clock_signal: "clk_#{token}",
        async_reset_signal: 'rst_n',
        local_reset_signal: "rst_n_#{token}"
      )
    end
    directions = context.edges.keys.flat_map do |kind, id|
      FinepaperNoc::DomainRtlContext::ORIENTATIONS.map do |orientation|
        [kind, id, orientation]
      end
    end
    directions.reverse! if reverse
    directions.pop if omit_last_direction
    directions.each do |kind, id, orientation|
      traffic = context.traffic(kind, id, orientation)
      crossing = !context.edge_stage(kind, id, 'clock-async-fifo').nil?
      base = if duplicate_signals
               'duplicate_bundle'
             else
               "bundle_#{kind == 'router-link' ? 'router' : id}_#{orientation.tr('-', '_')}"
             end
      source = crossing ? "#{base}_src" : base
      destination = crossing ? "#{base}_dst" : base
      bridge = if crossing
                 token = id.gsub(/[^A-Za-z0-9_]/, '_')
                 {
                   'module' => 'fp_async_ready_valid_fifo',
                   'instance' => "u_fifo_#{token}_#{orientation.tr('-', '_')}",
                   'placement' => bridge_placement,
                   'sourcePins' => {
                     'payload' => 'src_payload_i',
                     'valid' => 'src_valid_i',
                     'ready' => 'src_ready_o'
                   },
                   'destinationPins' => {
                     'payload' => 'dst_payload_o',
                     'valid' => 'dst_valid_o',
                     'ready' => 'dst_ready_i'
                   }
                 }
               end
      builder.register_edge_direction(
        edge: {'kind' => kind, 'id' => id},
        orientation: orientation,
        producer: traffic.fetch('producer'),
        consumer: traffic.fetch('consumer'),
        producer_pins: test_pin_bundle(traffic.fetch('producer'), 'out'),
        consumer_pins: test_pin_bundle(traffic.fetch('consumer'), 'in'),
        source_bundle: bundle(source),
        destination_bundle: bundle(destination),
        bridge: bridge
      )
    end
    builder
  end

  def bundle(name)
    {
      'name' => name,
      'payload' => "#{name}_flit",
      'valid' => "#{name}_valid",
      'ready' => "#{name}_ready"
    }
  end

  def test_pin_bundle(reference, direction)
    token = reference.values_at('kind', 'id').join('_')
                     .gsub(/[^A-Za-z0-9_]/, '_')
    {
      'payload' => "#{direction}_#{token}_payload",
      'valid' => "#{direction}_#{token}_valid",
      'ready' => "#{direction}_#{token}_ready"
    }
  end

  def signal_flow(direction, side, type)
    direction.fetch('signalFlows').find do |flow|
      flow.fetch('side') == side && flow.fetch('type') == type
    end
  end

  def module_for_instance(manifest, instance)
    element = manifest.fetch('elements').find do |record|
      record.fetch('instance') == instance
    end
    return element.fetch('module') if element

    bridge = manifest.fetch('edgeDirections').filter_map do |direction|
      direction.fetch('bridge')
    end.find { |record| record.fetch('instance') == instance }
    bridge&.fetch('module')
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

  def minimal_design
    elements = [
      {'kind' => 'router', 'id' => 'r-0-0'},
      {'kind' => 'endpoint', 'id' => 'ep0'}
    ]
    {
      'format' => 'finepaper.noc-design',
      'formatVersion' => 3,
      'id' => 'artifact_mesh',
      'name' => 'Artifact Mesh',
      'package' => {'id' => 'finepaper.noc', 'version' => '3.0.0'},
      'topology' => {'type' => 'mesh', 'rows' => 1, 'columns' => 1},
      'parameters' => {'dataWidth' => 64, 'flitWidth' => 128, 'addrWidth' => 32},
      'endpoints' => [{
        'id' => 'ep0',
        'type' => 'master',
        'attachment' => {'router' => {'x' => 0, 'y' => 0}},
        'parameters' => {
          'protocol' => 'axi4', 'dataWidth' => 64,
          'bufferDepth' => 16, 'qosEnabled' => false
        }
      }],
      'domains' => [
        {
          'id' => 'clock-main', 'type' => 'clock', 'name' => 'Clock',
          'properties' => {'frequencyMHz' => 1000, 'resetReleaseStages' => 2}
        },
        {
          'id' => 'power-main', 'type' => 'power', 'name' => 'Power',
          'properties' => {'voltageMv' => 900, 'retention' => false}
        }
      ],
      'domainMemberships' => elements.map do |element|
        {
          'element' => element,
          'assignments' => {
            'clock' => ['clock-main'], 'power' => ['power-main']
          }
        }
      end,
      'domainRelations' => [],
      'crossingPolicies' => [],
      'edgeOverrides' => [],
      'elementConfigurations' => []
    }
  end
end
