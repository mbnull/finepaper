$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'fileutils'
require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'

class RaveNoCGeneratorTest < Minitest::Test
  GENERATOR = File.expand_path('../bin/generate', __dir__)

  def test_generates_config_filelist_wrapper_verify_and_manifest
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'graph.json', valid_graph)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)
      out = File.join(dir, 'out')

      stdout, stderr, status = run_generator(input, out, vendor)

      assert status.success?, stderr
      assert_includes stdout, 'Generated RaveNoC integration'
      assert_includes File.read(File.join(out, 'ravenoc_config.svh')), '`define NOC_CFG_SZ_ROWS 2'
      assert_includes File.read(File.join(out, 'ravenoc_config.svh')), '`define ROUTING_ALG XYAlg'
      assert_includes File.read(File.join(out, 'ravenoc_demo_top.sv')), 'module ravenoc_demo_top'
      assert_includes File.read(File.join(out, 'ravenoc_demo_top.sv')), 'ravenoc #('
      filelist = File.read(File.join(out, 'ravenoc_filelist.f'))
      assert_includes filelist, '+define+NOC_CFG_SZ_ROWS=2'
      assert_includes filelist, 'src/ravenoc.sv'
      assert_includes filelist, 'ravenoc_demo_top.sv'
      refute_includes filelist, 'ravenoc_axi_fnc.svh'
      refute_includes filelist, 'ravenoc_defines.svh'
      refute_includes filelist, 'ravenoc_structs.svh'
      assert File.executable?(File.join(out, 'verify.sh')), 'verify.sh should be executable'
      assert_includes File.read(File.join(out, 'verify.sh')), '--lint-only'

      manifest = JSON.parse(File.read(File.join(out, 'manifest.json')))
      assert_equal 'finepaper.ravenoc', manifest.fetch('plugin')
      assert_equal 'ravenoc_node', manifest.fetch('module').fetch('id')
      assert_equal 2, manifest.fetch('parameters').fetch('rows')
    end
  end

  def test_generates_from_internal_ravetile_graph
    Dir.mktmpdir do |dir|
      input = File.expand_path('../examples/internal_mesh_2x2.json', __dir__)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)
      out = File.join(dir, 'out')

      stdout, stderr, status = run_generator(input, out, vendor)

      assert status.success?, stderr
      assert_includes stdout, 'Generated RaveNoC integration'
      config = File.read(File.join(out, 'ravenoc_config.svh'))
      assert_includes config, '`define NOC_CFG_SZ_ROWS 2'
      assert_includes config, '`define NOC_CFG_SZ_COLS 2'

      manifest = JSON.parse(File.read(File.join(out, 'manifest.json')))
      assert_equal 'internal_graph', manifest.fetch('module').fetch('type')
      assert_equal 4, manifest.fetch('module').fetch('tiles')
    end
  end

  def test_rejects_illegal_single_node_mesh
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph.fetch('modules').first.fetch('parameters')['rows'] = 1
      graph.fetch('modules').first.fetch('parameters')['cols'] = 1
      input = write_json(dir, 'graph.json', graph)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)

      _stdout, stderr, status = run_generator(input, File.join(dir, 'out'), vendor)

      refute status.success?
      assert_includes stderr, '1x1 is not a legal RaveNoC mesh'
    end
  end

  def test_rejects_non_power_of_two_buffer_depth
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph.fetch('modules').first.fetch('parameters')['flit_buffer_depth'] = 3
      input = write_json(dir, 'graph.json', graph)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)

      _stdout, stderr, status = run_generator(input, File.join(dir, 'out'), vendor)

      refute status.success?
      assert_includes stderr, 'flit_buffer_depth must be a power of two'
    end
  end

  def test_reports_missing_vendor_source
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'graph.json', valid_graph)

      _stdout, stderr, status = run_generator(input, File.join(dir, 'out'), File.join(dir, 'missing'))

      refute status.success?
      assert_includes stderr, 'RaveNoC vendor source is missing'
    end
  end

  private

  def run_generator(input, output, vendor)
    Open3.capture3(
      RbConfig.ruby,
      GENERATOR,
      '-i', input,
      '-o', output,
      '-t', File.expand_path('../template', __dir__),
      '--vendor', vendor
    )
  end

  def write_json(root, relative, data)
    path = File.join(root, relative)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, JSON.pretty_generate(data))
    path
  end

  def valid_graph
    {
      'schema' => 'finepaper-plugin-graph-v1',
      'name' => 'demo',
      'modules' => [
        {
          'id' => 'ravenoc_node',
          'plugin' => 'finepaper.ravenoc',
          'type' => 'RaveNoC',
          'parameters' => {
            'rows' => 2,
            'cols' => 2,
            'flit_data_width' => 32,
            'flit_type_width' => 2,
            'flit_buffer_depth' => 2,
            'virtual_channels' => 3,
            'routing_algorithm' => 'xy',
            'priority' => 'zero_high',
            'max_packet_flits' => 256,
            'axi_addr_width' => 32,
            'axi_data_width' => 32,
            'axi_cdc_required' => 'all',
            'bypass_cdc' => false
          }
        }
      ],
      'connections' => []
    }
  end

  def make_fake_vendor(root)
    required_vendor_files.each do |relative|
      path = File.join(root, relative)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, "// fake vendor file\n")
    end
  end

  def required_vendor_files
    [
      'bus_arch_sv_pkg/amba_axi_pkg.sv',
      'src/include/ravenoc_axi_fnc.svh',
      'src/include/ravenoc_defines.svh',
      'src/include/ravenoc_structs.svh',
      'src/include/ravenoc_pkg.sv',
      'src/ni/axi_csr.sv',
      'src/ni/axi_slave_if.sv',
      'src/ni/router_wrapper.sv',
      'src/ni/async_gp_fifo.sv',
      'src/ni/cdc_pkt.sv',
      'src/ni/pkt_proc.sv',
      'src/router/fifo.sv',
      'src/router/output_module.sv',
      'src/router/router_if.sv',
      'src/router/router_ravenoc.sv',
      'src/router/rr_arbiter.sv',
      'src/router/vc_buffer.sv',
      'src/router/input_router.sv',
      'src/router/input_module.sv',
      'src/router/input_datapath.sv',
      'src/ravenoc.sv'
    ]
  end
end
