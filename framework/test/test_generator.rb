$LOAD_PATH.unshift File.join(__dir__, '..', 'src', 'ruby')

require 'minitest/autorun'
require 'tempfile'
require 'fileutils'
require 'open3'
require 'rbconfig'
require 'parser/json_parser'
require 'parser/verilog_parser'
require 'topology/topology_expander'
require 'drc/drc_runner'
require 'generator/frontend_bundle_exporter'
require 'generator/ipxact_exporter'
require 'model/module_catalog'
require 'model/noc_config'
require 'model/xp'
require 'generator/rtl_generator'

EXAMPLE = File.join(__dir__, '..', 'examples', 'simple_mesh.json')
MESH_3X3 = File.join(__dir__, '..', 'examples', 'mesh_3x3.json')
MULTI_EP = File.join(__dir__, '..', 'examples', 'multi_endpoint.json')

class TestJsonParser < Minitest::Test
  def test_parses_noc
    noc = JsonParser.parse(EXAMPLE)
    assert_equal 'my_noc', noc.name
    assert_equal 4, noc.xps.size
    assert_equal 4, noc.connections.size
    assert_equal 4, noc.endpoints.size
  end

  def test_parses_grouped_router_links
    f = Tempfile.new(['grouped_links', '.json'])
    f.write('{"name":"t","version":"1.0","xps":[{"id":"xp0","x":0,"y":0},{"id":"xp1","x":1,"y":0},{"id":"xp2","x":0,"y":1}],"connections":{"router_links":[{"from":"xp0","links":{"east":"xp1","south":"xp2"}}]},"endpoints":[]}')
    f.close
    noc = JsonParser.parse(f.path)
    assert_equal 2, noc.connections.size
    assert_equal ['east', 'south'], noc.connections.map(&:router_direction).sort
  ensure
    f&.unlink
  end

  def test_parses_explicit_port_router_links
    f = Tempfile.new(['explicit_links', '.json'])
    f.write('{"name":"t","version":"1.0","xps":[{"id":"xp0","x":0,"y":0},{"id":"xp1","x":1,"y":0}],"connections":{"explicit":[{"from":{"node":"xp0","port":"east_out"},"to":{"node":"xp1","port":"west_in"}}]},"endpoints":[]}')
    f.close
    noc = JsonParser.parse(f.path)
    assert_equal 1, noc.connections.size
    assert_equal 'east', noc.connections.first.router_direction
    assert_equal 'east_out', noc.connections.first.from_port
    assert_equal 'west_in', noc.connections.first.to_port
  ensure
    f&.unlink
  end

  def test_applies_parameter_defaults
    require 'tempfile'
    f = Tempfile.new(['minimal', '.json'])
    f.write('{"name":"test","version":"1.0"}')
    f.close
    noc = JsonParser.parse(f.path)
    assert_equal 64, noc.parameters['data_width']
    assert_equal 128, noc.parameters['flit_width']
    assert_equal 32, noc.parameters['addr_width']
  ensure
    f&.unlink
  end

  def test_validates_required_fields
    require 'tempfile'
    f = Tempfile.new(['invalid', '.json'])
    f.write('{"version":"1.0"}')
    f.close
    assert_raises(RuntimeError) { JsonParser.parse(f.path) }
  ensure
    f&.unlink
  end
end

class TestTopologyExpander < Minitest::Test
  def test_passthrough_when_xps_present
    noc = JsonParser.parse(EXAMPLE)
    assert_same noc, TopologyExpander.expand(noc)
  end

  def test_expands_mesh
    noc = NocConfig.new('t', '1', { 'mesh' => { 'width' => 2, 'height' => 2 } }, [], [], [])
    expanded = TopologyExpander.expand(noc)
    assert_equal 4, expanded.xps.size
    assert_equal 4, expanded.connections.size
  end

  def test_expands_3x3_mesh
    noc = JsonParser.parse(MESH_3X3)
    expanded = TopologyExpander.expand(noc)
    assert_equal 9, expanded.xps.size
    assert_equal 12, expanded.connections.size
    center = expanded.xps.find { |xp| xp.x == 1 && xp.y == 1 }
    assert_equal 4, center.neighbors(expanded).size
  end

  def test_raises_without_topology
    noc = NocConfig.new('t', '1', {}, [], [], [])
    assert_raises(RuntimeError) { TopologyExpander.expand(noc) }
  end
end

class TestDrcRunner < Minitest::Test
  def test_passes_unique_ids
    DrcRunner.new.run(JsonParser.parse(EXAMPLE))
  end

  def test_raises_on_duplicate_id
    xp = Xp.new('xp_0_0', 0, 0, [])
    noc = NocConfig.new('t', '1', {}, [xp, xp], [], [])
    assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
  end

  def test_raises_on_duplicate_endpoint_id
    require 'model/endpoint'
    ep = Endpoint.new('ep1', 'master', 'axi4', 64)
    noc = NocConfig.new('t', '1', {}, [], [], [ep, ep])
    assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
  end

  def test_raises_on_invalid_buffer_depth
    require 'model/endpoint'
    ep = Endpoint.new('ep1', 'master', 'axi4', 64, { buffer_depth: 0 })
    noc = NocConfig.new('t', '1', {}, [], [], [ep])
    assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
  end

  def test_raises_on_invalid_endpoint_type
    ep = Endpoint.new('ep1', 'bogus', 'axi4', 64)
    noc = NocConfig.new('t', '1', {}, [], [], [ep])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'type must be one of master, slave'
  end

  def test_raises_on_invalid_endpoint_data_width
    ep = Endpoint.new('ep1', 'master', 'axi4', 0)
    noc = NocConfig.new('t', '1', {}, [], [], [ep])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'data_width must be >= 1'
  end

  def test_raises_on_invalid_routing_algorithm
    xp = Xp.new('xp1', 0, 0, [], { routing_algorithm: 'invalid' })
    noc = NocConfig.new('t', '1', {}, [xp], [], [])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'routing_algorithm must be one of xy, yx'
  end

  def test_raises_on_invalid_vc_count
    xp = Xp.new('xp1', 0, 0, [], { vc_count: 10 })
    noc = NocConfig.new('t', '1', {}, [xp], [], [])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'vc_count must be <= 8'
  end

  def test_raises_on_unknown_router_connection_target
    xp = Xp.new('xp1', 0, 0, [])
    noc = NocConfig.new('t', '1', {}, [xp], [Connection.new('xp1', 'xp2', 'east')], [])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'Invalid connection target: xp2 not found'
  end

  def test_raises_on_invalid_router_connection_direction
    xp1 = Xp.new('xp1', 0, 0, [])
    xp2 = Xp.new('xp2', 1, 0, [])
    noc = NocConfig.new('t', '1', {}, [xp1, xp2], [Connection.new('xp1', 'xp2', 'sideways')], [])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, "XP xp1: invalid direction 'sideways' for connection to xp2"
  end

  def test_raises_on_missing_router_connection_direction
    xp1 = Xp.new('xp1', 0, 0, [])
    xp2 = Xp.new('xp2', 1, 0, [])
    noc = NocConfig.new('t', '1', {}, [xp1, xp2], [Connection.new('xp1', 'xp2', nil)], [])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'XP xp1: missing router direction for connection to xp2'
  end

  def test_accepts_router_direction_derived_from_ports
    xp1 = Xp.new('xp1', 0, 0, [])
    xp2 = Xp.new('xp2', 1, 0, [])
    noc = NocConfig.new('t', '1', {}, [xp1, xp2], [
      Connection.new('xp1', 'xp2', nil, from_port: 'east_out', to_port: 'west_in')
    ], [])
    DrcRunner.new.run(noc)
  end

  def test_rejects_explicit_router_port_with_wrong_direction
    xp1 = Xp.new('xp1', 0, 0, [])
    xp2 = Xp.new('xp2', 1, 0, [])
    noc = NocConfig.new('t', '1', {}, [xp1, xp2], [
      Connection.new('xp1', 'xp2', nil, from_port: 'east_in', to_port: 'west_in')
    ], [])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'port east_in must be output'
  end

  def test_rejects_explicit_router_ports_with_conflicting_sides
    xp1 = Xp.new('xp1', 0, 0, [])
    xp2 = Xp.new('xp2', 1, 0, [])
    noc = NocConfig.new('t', '1', {}, [xp1, xp2], [
      Connection.new('xp1', 'xp2', nil, from_port: 'east_out', to_port: 'south_in')
    ], [])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'router ports east_out and south_in imply conflicting directions'
  end

  def test_raises_on_missing_endpoint_attachment
    xp = Xp.new('xp1', 0, 0, ['ep_missing'])
    noc = NocConfig.new('t', '1', {}, [xp], [], [])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'XP xp1: endpoint ep_missing not found'
  end

  def test_raises_on_endpoint_attached_to_multiple_xps
    ep = Endpoint.new('ep1', 'master', 'axi4', 64)
    xp1 = Xp.new('xp1', 0, 0, ['ep1'])
    xp2 = Xp.new('xp2', 1, 0, ['ep1'])
    noc = NocConfig.new('t', '1', {}, [xp1, xp2], [], [ep])
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'Endpoint ep1: attached to 2 XPs, expected at most 1'
  end

  def test_raises_when_xp_exceeds_attachment_capacity
    endpoints = 5.times.map { |index| Endpoint.new("ep#{index}", 'master', 'axi4', 64) }
    xp = Xp.new('xp1', 0, 0, endpoints.map(&:id))
    noc = NocConfig.new('t', '1', {}, [xp], [], endpoints)
    error = assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
    assert_includes error.message, 'XP xp1: supports at most 4 endpoint attachments, got 5'
  end
end

class TestVerilogParser < Minitest::Test
  def test_parses_ports
    f = Tempfile.new(['test', '.sv'])
    f.write("module foo(input wire [7:0] a, output logic b);")
    f.close
    iface = VerilogParser.parse(f.path)
    assert_equal 'foo', iface.name
    assert_equal 2, iface.ports.size
    assert_equal 'input',  iface.ports[0].dir
    assert_equal '[7:0]',  iface.ports[0].width
    assert_equal 'a',      iface.ports[0].name
    assert_nil             iface.ports[1].width
  ensure
    f&.unlink
  end
end

class TestNocConfig < Minitest::Test
  def test_catalog
    cat = JsonParser.parse(EXAMPLE).catalog
    assert_equal 2, cat[:masters].size
    assert_equal 2, cat[:slaves].size
    assert cat[:by_protocol].key?('axi4')
  end
end

class TestXp < Minitest::Test
  def test_node_id
    noc = JsonParser.parse(EXAMPLE)
    xp  = noc.xps.find { |x| x.x == 1 && x.y == 1 }
    assert_equal 3, xp.node_id(noc)
  end
end

class TestConfigSchema < Minitest::Test
  def test_module_catalog_reflects_framework_models
    descriptors = ModuleCatalog.descriptors
    assert_equal ['Endpoint', 'XP'], descriptors.map { |descriptor| descriptor[:name] }.sort
    assert_equal 'router', ModuleCatalog.descriptor('XP').dig(:connectivity, :router_bus_type)
    assert_equal 'ni2router', ModuleCatalog.descriptor('Endpoint').dig(:connectivity, :attachment_bus_type)
  end

  def test_module_catalog_expands_compact_xp_port_patterns
    xp = ModuleCatalog.descriptor('XP')
    assert_equal 12, xp[:ports].size
    assert_equal 'ep0', xp[:ports][0][:id]
    assert_equal 'EP0', xp[:ports][0][:name]
    assert_equal 'north_in', xp[:ports][4][:id]
    assert_equal 'N', xp[:ports][4][:name]
    assert_equal 'West router egress', xp[:ports][11][:description]
  end

  def test_module_catalog_expands_parameter_templates
    xp = ModuleCatalog.descriptor('XP')
    endpoint = ModuleCatalog.descriptor('Endpoint')

    assert_equal 'display_name', xp[:parameters][2][:name]
    assert_equal 'Display name', xp[:parameters][2][:label]
    assert_equal 'external_id', endpoint[:parameters][1][:name]
    assert_equal 'Framework-facing identifier.', endpoint[:parameters][1][:description]
    assert_equal 8, xp[:config_parameters][2][:default]
    assert_equal 'Buffer depth', endpoint[:config_parameters][0][:label]
  end

  def test_module_catalog_computes_compatible_modules
    assert_equal ['Endpoint'], ModuleCatalog.compatible_module_names('XP', relation: :attachment)
    assert_equal ['XP'], ModuleCatalog.compatible_module_names('Endpoint', relation: :attachment)
    assert_equal ['XP'], ModuleCatalog.compatible_module_names('XP', relation: :router)
  end

  def test_module_catalog_supports_folder_inheritance_and_variant_parsing
    dir = Dir.mktmpdir
    source_dir = File.join(__dir__, '..', 'src', 'ruby', 'model', 'modules')
    FileUtils.cp_r(File.join(source_dir, '.'), dir)
    File.write(File.join(dir, 'EndpointQoS.json'), <<~JSON)
      {
        "name": "EndpointQoS",
        "extends": "Endpoint",
        "family": "endpoint",
        "palette_label": "Endpoint+QoS",
        "description": "Endpoint variant with QoS enabled by default.",
        "presentation": {
          "node_color": "#c7f1ff"
        },
        "parameters": {
          "config": [
            {
              "name": "buffer_depth",
              "use": "buffer_depth",
              "default": 32,
              "description": "Ingress buffer depth."
            },
            {
              "name": "qos_enabled",
              "type": "bool",
              "default": true,
              "label": "QoS enabled",
              "description": "Enable QoS tagging support."
            }
          ]
        }
      }
    JSON

    descriptor = nil
    noc = nil
    input = Tempfile.new(['catalog_variant', '.json'])
    input.write('{"name":"t","version":"1.0","xps":[{"id":"xp0","x":0,"y":0,"endpoints":["ep0"]}],"connections":[],"endpoints":[{"id":"ep0","module":"EndpointQoS","type":"master","protocol":"axi4","data_width":64}]}')
    input.close

    ModuleCatalog.with_catalog_dir(dir) do
      descriptor = ModuleCatalog.descriptor('EndpointQoS')
      noc = JsonParser.parse(input.path)
      DrcRunner.new.run(noc)
    end

    assert_equal 'endpoint', descriptor[:family]
    assert_equal '#c7f1ff', descriptor.dig(:presentation, :node_color)
    assert_equal 'EndpointQoS', noc.endpoints.first.module_name
    assert_equal true, noc.endpoints.first.config[:qos_enabled]
    assert_equal 32, noc.endpoints.first.config[:buffer_depth]
  ensure
    input&.unlink
    FileUtils.rm_rf(dir)
  end

  def test_module_catalog_merges_parent_descriptor_before_normalization
    dir = Dir.mktmpdir
    source_dir = File.join(__dir__, '..', 'src', 'ruby', 'model', 'modules')
    FileUtils.cp_r(File.join(source_dir, '.'), dir)
    File.write(File.join(dir, 'EndpointAlias.json'), <<~JSON)
      {
        "name": "EndpointAlias",
        "extends": "Endpoint",
        "presentation": {
          "node_color": "#eeeeee"
        }
      }
    JSON
    File.write(File.join(dir, 'EndpointPartial.json'), <<~JSON)
      {
        "name": "EndpointPartial",
        "extends": "Endpoint",
        "parameters": {
          "config": [
            {
              "name": "buffer_depth",
              "use": "buffer_depth",
              "default": 32,
              "description": "Ingress buffer depth."
            }
          ]
        }
      }
    JSON

    alias_descriptor = nil
    partial_descriptor = nil
    ModuleCatalog.with_catalog_dir(dir) do
      alias_descriptor = ModuleCatalog.descriptor('EndpointAlias')
      partial_descriptor = ModuleCatalog.descriptor('EndpointPartial')
    end

    assert_equal '#eeeeee', alias_descriptor.dig(:presentation, :node_color)
    assert_equal ['display_name', 'external_id', 'type', 'protocol', 'data_width'],
                 alias_descriptor[:parameters].first(5).map { |parameter| parameter[:name] }
    assert_equal ['display_name', 'external_id', 'type', 'protocol', 'data_width'],
                 partial_descriptor[:parameters].first(5).map { |parameter| parameter[:name] }
    assert_equal 32, partial_descriptor[:config_parameters][0][:default]
  ensure
    FileUtils.rm_rf(dir)
  end

  def test_endpoint_schema_reflection
    schema = Endpoint.config_schema
    assert_equal :integer, schema[:buffer_depth][:type]
    assert_equal 16, schema[:buffer_depth][:default]
    assert_equal :boolean, schema[:qos_enabled][:type]
    assert_equal false, schema[:qos_enabled][:default]
  end

  def test_xp_schema_reflection
    schema = Xp.config_schema
    assert_equal :string, schema[:routing_algorithm][:type]
    assert_equal 'xy', schema[:routing_algorithm][:default]
    assert_equal :integer, schema[:vc_count][:type]
    assert_equal 2, schema[:vc_count][:default]
  end

  def test_endpoint_applies_defaults
    ep = Endpoint.new('ep1', 'master', 'axi4', 64)
    assert_equal 16, ep.config[:buffer_depth]
    assert_equal false, ep.config[:qos_enabled]
  end

  def test_endpoint_merges_custom_config
    ep = Endpoint.new('ep1', 'master', 'axi4', 64, { buffer_depth: 32, qos_enabled: true })
    assert_equal 32, ep.config[:buffer_depth]
    assert_equal true, ep.config[:qos_enabled]
  end

  def test_xp_applies_defaults
    xp = Xp.new('xp1', 0, 0, [])
    assert_equal 'xy', xp.config[:routing_algorithm]
    assert_equal 2, xp.config[:vc_count]
    assert_equal 8, xp.config[:buffer_depth]
  end

  def test_xp_merges_custom_config
    xp = Xp.new('xp1', 0, 0, [], { routing_algorithm: 'yx', vc_count: 4 })
    assert_equal 'yx', xp.config[:routing_algorithm]
    assert_equal 4, xp.config[:vc_count]
    assert_equal 8, xp.config[:buffer_depth]
  end

  def test_parser_validates_type
    f = Tempfile.new(['type_err', '.json'])
    f.write('{"name":"t","version":"1.0","endpoints":[{"id":"ep1","type":"master","protocol":"axi4","data_width":64,"config":{"buffer_depth":"invalid"}}],"xps":[],"connections":[]}')
    f.close
    assert_raises(RuntimeError) { JsonParser.parse(f.path) }
  ensure
    f&.unlink
  end

  def test_parser_rejects_unknown_field
    f = Tempfile.new(['unknown', '.json'])
    f.write('{"name":"t","version":"1.0","endpoints":[{"id":"ep1","type":"master","protocol":"axi4","data_width":64,"config":{"unknown_field":123}}],"xps":[],"connections":[]}')
    f.close
    assert_raises(RuntimeError) { JsonParser.parse(f.path) }
  ensure
    f&.unlink
  end

  def test_parser_handles_missing_config
    f = Tempfile.new(['no_cfg', '.json'])
    f.write('{"name":"t","version":"1.0","endpoints":[{"id":"ep1","type":"master","protocol":"axi4","data_width":64}],"xps":[],"connections":[]}')
    f.close
    noc = JsonParser.parse(f.path)
    assert_equal 16, noc.endpoints[0].config[:buffer_depth]
  ensure
    f&.unlink
  end
end

class TestModuleExporters < Minitest::Test
  def test_export_modules_fails_when_all_outputs_disabled
    script = File.join(__dir__, '..', 'bin', 'export_modules')
    model_dir = File.join(__dir__, '..', 'src', 'ruby', 'model', 'modules')
    stdout, stderr, status = Open3.capture3(RbConfig.ruby,
                                            script,
                                            '--model-dir',
                                            model_dir,
                                            '--no-frontend-bundle',
                                            '--no-ipxact')

    assert_equal '', stdout
    refute status.success?
    assert_includes stderr, 'Nothing to export'
  end

  def test_frontend_bundle_exporter_writes_bundle_files
    out = Dir.mktmpdir
    FrontendBundleExporter.new.write(out)

    modules_xml = File.read(File.join(out, 'modules.xml'))
    xp_graphics = File.read(File.join(out, 'graphics', 'XP.xml'))

    assert_match(/module name='XP'|module name="XP"/, modules_xml)
    assert_match(/bus_type='router'|bus_type="router"/, modules_xml)
    assert_match(/choice value='xy'|choice value="xy"/, modules_xml)
    assert_match(/module-graphics type='XP'|module-graphics type="XP"/, xp_graphics)
    assert_match(/layout='mesh_router'|layout="mesh_router"/, xp_graphics)
  ensure
    FileUtils.rm_rf(out)
  end

  def test_ipxact_exporter_writes_component_files
    out = Dir.mktmpdir
    IpXactExporter.new.write(out)

    xp_component = File.read(File.join(out, 'XP.component.xml'))

    assert_match(/xmlns:ipxact="http:\/\/www\.accellera\.org\/XMLSchema\/IPXACT\/1685-2021\/"/, xp_component)
    assert_match(/<ipxact:component/, xp_component)
    assert_match(/<ipxact:ports>/, xp_component)
    assert_match(/<ipxact:parameters>/, xp_component)
    assert_match(/<ipxact:vendorExtensions>/, xp_component)
    assert_match(/<fp:module/, xp_component)
  ensure
    FileUtils.rm_rf(out)
  end
end

class TestRtlGenerator < Minitest::Test
  def test_renders_xp_template
    noc = JsonParser.parse(EXAMPLE)
    noc.instance_variable_set(:@xp, noc.xps.first)
    out = Dir.mktmpdir
    RtlGenerator.new(noc, File.join(__dir__, '..', 'template'))
                .render('xp.sv.erb', File.join(out, 'out.sv'))
    assert_match(/module xp_router_xp_0_0/, File.read(File.join(out, 'out.sv')))
  ensure
    FileUtils.rm_rf(out)
  end

  def test_renders_ni_template
    noc = JsonParser.parse(EXAMPLE)
    xp = noc.xps.find { |x| x.endpoints.any? }
    noc.instance_variable_set(:@xp, xp)
    out = Dir.mktmpdir
    RtlGenerator.new(noc, File.join(__dir__, '..', 'template'))
                .render('ni.sv.erb', File.join(out, "ni_xp_#{xp.id}.sv"))
    content = File.read(File.join(out, "ni_xp_#{xp.id}.sv"))
    assert_match(/module ni_xp_#{xp.id}/, content)
    assert_match(/NUM_ENDPOINTS/, content)
  ensure
    FileUtils.rm_rf(out)
  end

  def test_renders_top_template
    noc = JsonParser.parse(EXAMPLE)
    out = Dir.mktmpdir
    RtlGenerator.new(noc, File.join(__dir__, '..', 'template'))
                .render('top.v.erb', File.join(out, 'top.v'))
    content = File.read(File.join(out, 'top.v'))
    assert_match(/module my_noc_top/, content)
    assert_match(/xp_router_xp_0_0/, content)
    assert_match(/link_xp_0_0_to_xp_1_0_flit/, content)
    assert_match(/ni_xp_/, content)
  ensure
    FileUtils.rm_rf(out)
  end
end

# NI multi-endpoint structural tests
class TestNiMultiEndpoint < Minitest::Test
  def setup
    # Build a minimal NocConfig with one XP carrying 3 endpoints
    require 'model/endpoint'
    eps = [
      Endpoint.new('ep_a', 'master', 'axi4', 64),
      Endpoint.new('ep_b', 'slave',  'axi4', 128),
      Endpoint.new('ep_c', 'master', 'axi4', 64)
    ]
    xp = Xp.new('xp_0_0', 0, 0, ['ep_a', 'ep_b', 'ep_c'])
    params = { 'data_width' => 64, 'flit_width' => 128, 'addr_width' => 32 }
    @noc = NocConfig.new('multi_ep_test', '1.0', params, [xp], [], eps)
    @noc.instance_variable_set(:@xp, xp)
    @out  = Dir.mktmpdir
    @file = File.join(@out, 'ni_xp_0_0.sv')
    RtlGenerator.new(@noc, File.join(__dir__, '..', 'template'))
                .render('ni.sv.erb', @file)
    @content = File.read(@file)
  end

  def teardown = FileUtils.rm_rf(@out)

  def test_num_endpoints_parameter
    assert_match(/NUM_ENDPOINTS = 3/, @content)
  end

  def test_all_endpoint_flit_in_ports_present
    assert_match(/ep_a_flit_in/, @content)
    assert_match(/ep_b_flit_in/, @content)
    assert_match(/ep_c_flit_in/, @content)
  end

  def test_all_endpoint_flit_out_ports_present
    assert_match(/ep_a_flit_out/, @content)
    assert_match(/ep_b_flit_out/, @content)
    assert_match(/ep_c_flit_out/, @content)
  end

  def test_all_router_side_ports_present
    assert_match(/ep_a_router_flit_in/, @content)
    assert_match(/ep_b_router_flit_in/, @content)
    assert_match(/ep_c_router_flit_in/, @content)
  end

  def test_passthrough_assigns_for_each_endpoint
    assert_match(/assign ep_a_router_flit_in = ep_a_flit_in/, @content)
    assert_match(/assign ep_b_router_flit_in = ep_b_flit_in/, @content)
    assert_match(/assign ep_c_router_flit_in = ep_c_flit_in/, @content)
    assert_match(/assign ep_a_flit_out = ep_a_router_flit_out/, @content)
    assert_match(/assign ep_b_flit_out = ep_b_router_flit_out/, @content)
    assert_match(/assign ep_c_flit_out = ep_c_router_flit_out/, @content)
  end

  def test_verilator_lint_clean
    skip 'verilator not found' unless system('which verilator > /dev/null 2>&1')
    assert system("verilator --lint-only --sv #{@file} 2>/dev/null"), "lint failed on multi-endpoint NI"
  end
end

# Multi-endpoint example integration test
class TestMultiEndpointExample < Minitest::Test
  def test_generates_multi_endpoint_ni
    noc = JsonParser.parse(MULTI_EP)
    out = Dir.mktmpdir
    gen = RtlGenerator.new(noc, File.join(__dir__, '..', 'template'))
    noc.xps.each do |xp|
      noc.instance_variable_set(:@xp, xp)
      gen.render('xp.sv.erb', File.join(out, "xp_router_#{xp.id}.sv"))
    end
    noc.xps.each do |xp|
      next if xp.endpoints.empty?
      noc.instance_variable_set(:@xp, xp)
      gen.render('ni.sv.erb', File.join(out, "ni_xp_#{xp.id}.sv"))
    end
    gen.render('top.v.erb', File.join(out, "#{noc.name}_top.v"))

    ni_3ep = File.read(File.join(out, "ni_xp_xp_0_0.sv"))
    assert_match(/NUM_ENDPOINTS = 3/, ni_3ep)
    assert_match(/ep_cpu0_flit_in/, ni_3ep)
    assert_match(/ep_cpu1_flit_in/, ni_3ep)
    assert_match(/ep_dma0_flit_in/, ni_3ep)

    ni_2ep = File.read(File.join(out, "ni_xp_xp_1_0.sv"))
    assert_match(/NUM_ENDPOINTS = 2/, ni_2ep)
    assert_match(/ep_mem0_flit_in/, ni_2ep)
    assert_match(/ep_mem1_flit_in/, ni_2ep)
  ensure
    FileUtils.rm_rf(out)
  end
end

# Structural: generated ports match topology
class TestGeneratorStructure < Minitest::Test
  def setup
    noc = JsonParser.parse(EXAMPLE)
    noc.instance_variable_set(:@xp, noc.xps.first)
    @out = Dir.mktmpdir
    @sv  = File.join(@out, 'out.sv')
    RtlGenerator.new(noc, File.join(__dir__, '..', 'template')).render('xp.sv.erb', @sv)
    @content = File.read(@sv)
  end

  def teardown = FileUtils.rm_rf(@out)

  def test_flit_ports_match_neighbor_count
    # xp_0_0 has 2 neighbors (east, south) → 2 flit_in + 2 flit_out
    assert_equal 2, @content.scan(/flit_in_xp/).size
    assert_equal 2, @content.scan(/flit_out_xp/).size
  end

  def test_contains_parameters
    assert_match(/DATA_WIDTH/, @content)
    assert_match(/FLIT_WIDTH/, @content)
  end
end

# Regression: output matches golden file
class TestGoldenFile < Minitest::Test
  GOLDEN_DIR = File.join(__dir__, 'expected')

  def test_matches_golden
    noc = JsonParser.parse(EXAMPLE)
    out = Dir.mktmpdir
    gen = RtlGenerator.new(noc, File.join(__dir__, '..', 'template'))
    noc.xps.each do |xp|
      noc.instance_variable_set(:@xp, xp)
      gen.render('xp.sv.erb', File.join(out, "#{xp.id}.sv"))
    end
    gen.render('top.v.erb', File.join(out, "#{noc.name}_top.v"))
    noc.xps.each do |xp|
      assert_equal File.read(File.join(GOLDEN_DIR, "simple_mesh", "#{xp.id}.sv")),
                   File.read(File.join(out, "#{xp.id}.sv")),
                   "#{xp.id}.sv differs from golden"
    end
    assert_equal File.read(File.join(GOLDEN_DIR, "simple_mesh", "#{noc.name}_top.v")),
                 File.read(File.join(out, "#{noc.name}_top.v")),
                 "#{noc.name}_top.v differs from golden"
  ensure
    FileUtils.rm_rf(out)
  end

  def test_matches_golden_3x3
    noc = JsonParser.parse(MESH_3X3)
    noc = TopologyExpander.expand(noc)
    out = Dir.mktmpdir
    gen = RtlGenerator.new(noc, File.join(__dir__, '..', 'template'))
    noc.xps.each do |xp|
      noc.instance_variable_set(:@xp, xp)
      gen.render('xp.sv.erb', File.join(out, "xp_router_#{xp.id}.sv"))
    end
    noc.xps.each do |xp|
      next if xp.endpoints.empty?
      noc.instance_variable_set(:@xp, xp)
      gen.render('ni.sv.erb', File.join(out, "ni_xp_#{xp.id}.sv"))
    end
    gen.render('top.v.erb', File.join(out, "#{noc.name}_top.v"))
    assert_equal 9, Dir[File.join(out, "ni_xp_*.sv")].size, "Expected 9 NI modules"
    assert_equal File.read(File.join(GOLDEN_DIR, "mesh_3x3", "#{noc.name}_top.v")),
                 File.read(File.join(out, "#{noc.name}_top.v")),
                 "3x3 top differs from golden"
  ensure
    FileUtils.rm_rf(out)
  end
end

# Syntax: verilator lint on generated output
class TestVerilatorLint < Minitest::Test
  def test_lint_clean
    skip 'verilator not found' unless system('which verilator > /dev/null 2>&1')
    noc = JsonParser.parse(EXAMPLE)
    out = Dir.mktmpdir
    gen = RtlGenerator.new(noc, File.join(__dir__, '..', 'template'))
    noc.xps.each do |xp|
      noc.instance_variable_set(:@xp, xp)
      gen.render('xp.sv.erb', File.join(out, "#{xp.id}.sv"))
    end
    Dir[File.join(out, '*.sv')].each do |f|
      assert system("verilator --lint-only --sv #{f} 2>/dev/null"), "lint failed: #{File.basename(f)}"
    end
  ensure
    FileUtils.rm_rf(out)
  end

  def test_top_lint_clean
    skip 'verilator not found' unless system('which verilator > /dev/null 2>&1')
    noc = JsonParser.parse(EXAMPLE)
    out = Dir.mktmpdir
    gen = RtlGenerator.new(noc, File.join(__dir__, '..', 'template'))
    noc.xps.each do |xp|
      noc.instance_variable_set(:@xp, xp)
      gen.render('xp.sv.erb', File.join(out, "xp_router_#{xp.id}.sv"))
    end
    noc.xps.each do |xp|
      next if xp.endpoints.empty?
      noc.instance_variable_set(:@xp, xp)
      gen.render('ni.sv.erb', File.join(out, "ni_xp_#{xp.id}.sv"))
    end
    gen.render('top.v.erb', File.join(out, 'top.v'))
    files = Dir[File.join(out, '*.{sv,v}')].join(' ')
    assert system("verilator --lint-only -Wno-UNUSEDSIGNAL -Wno-UNUSEDPARAM #{files} 2>&1 | grep -v Warning"), "top lint failed"
  ensure
    FileUtils.rm_rf(out)
  end
end
