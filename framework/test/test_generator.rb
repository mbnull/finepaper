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

class TestJsonParser < Minitest::Test
  def test_parses_noc
    noc = JsonParser.parse(EXAMPLE)
    assert_equal 'my_noc', noc.name
    assert_equal 4, noc.xps.size
    assert_equal 4, noc.connections.size
    assert_equal 4, noc.endpoints.size
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
    noc.xps.each do |xp|
      assert_equal File.read(File.join(GOLDEN_DIR, "#{xp.id}.sv")),
                   File.read(File.join(out, "#{xp.id}.sv")),
                   "#{xp.id}.sv differs from golden"
    end
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
end
