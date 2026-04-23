$LOAD_PATH.unshift File.join(__dir__, '..', 'src', 'ruby')

require 'minitest/autorun'
require 'tempfile'
require 'fileutils'
require 'parser/json_parser'
require 'parser/verilog_parser'
require 'topology/topology_expander'
require 'drc/drc_runner'
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

  def test_raises_on_invalid_routing_algorithm
    xp = Xp.new('xp1', 0, 0, [], { routing_algorithm: 'invalid' })
    noc = NocConfig.new('t', '1', {}, [xp], [], [])
    assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
  end

  def test_raises_on_invalid_vc_count
    xp = Xp.new('xp1', 0, 0, [], { vc_count: 10 })
    noc = NocConfig.new('t', '1', {}, [xp], [], [])
    assert_raises(RuntimeError) { DrcRunner.new.run(noc) }
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

  def test_partitioned_generation_writes_direction_folders
    noc = JsonParser.parse(EXAMPLE)
    out = Dir.mktmpdir
    RtlGenerator.new(noc, File.join(__dir__, '..', 'template')).generate_partitioned(out)

    assert_empty Dir[File.join(out, 'xp_*', 'XP.v')]
    %w[xp_e00s xp_0w0s xp_e0n0 xp_0wn0].each do |dir|
      assert File.exist?(File.join(out, dir, "my_noc_#{dir}.v")), "missing #{dir}/my_noc_#{dir}.v"
    end

    xp = File.read(File.join(out, 'xp_e00s', 'my_noc_xp_e00s.v'))
    assert_match(/module xp_router_e00s/, xp)
    assert_match(/flit_in_e/, xp)
    assert_match(/flit_out_s/, xp)
    assert_match(/local0_flit_in/, xp)

    top = File.read(File.join(out, 'my_noc_top.v'))
    assert_match(/xp_router_e00s #\(/, top)
    assert_match(/\.flit_out_e\(link_xp_0_0_to_xp_1_0_flit\)/, top)
    assert_match(/\.flit_in_s\(link_xp_0_1_to_xp_0_0_flit\)/, top)
    assert_match(/\.local0_flit_in\(ni_ep_cpu0_to_router_flit\)/, top)
  ensure
    FileUtils.rm_rf(out)
  end

  def test_partitioned_generation_reuses_3x3_variants
    noc = TopologyExpander.expand(JsonParser.parse(MESH_3X3))
    out = Dir.mktmpdir
    RtlGenerator.new(noc, File.join(__dir__, '..', 'template')).generate_partitioned(out)

    assert_equal 9, Dir[File.join(out, 'xp_*', 'mesh_3x3_xp_*.v')].size
    assert File.exist?(File.join(out, 'xp_ewns', 'mesh_3x3_xp_ewns.v'))

    top = File.read(File.join(out, 'mesh_3x3_top.v'))
    assert_match(/xp_router_ewns #\(/, top)
    assert_match(/u_xp_1_1/, top)
  ensure
    FileUtils.rm_rf(out)
  end

  def test_partitioned_generation_keeps_endpoint_count_in_variant_key
    noc = JsonParser.parse(MULTI_EP)
    out = Dir.mktmpdir
    RtlGenerator.new(noc, File.join(__dir__, '..', 'template')).generate_partitioned(out)

    assert File.exist?(File.join(out, 'xp_e000_ep3', 'multi_ep_noc_xp_e000_ep3.v'))
    assert File.exist?(File.join(out, 'xp_0w00_ep2', 'multi_ep_noc_xp_0w00_ep2.v'))
    xp = File.read(File.join(out, 'xp_e000_ep3', 'multi_ep_noc_xp_e000_ep3.v'))
    assert_match(/module xp_router_e000_ep3/, xp)
    assert_match(/local2_flit_out/, xp)
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
