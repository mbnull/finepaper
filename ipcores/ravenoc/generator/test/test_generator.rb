# Migration-only legacy schema handling. Not used by normal runtime loading.
$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'fileutils'
require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'

IPCRAFT_PROJECT_SCHEMA = 'ipcraft.noc.project.v1'.freeze

class RaveNoCGeneratorTest < Minitest::Test
  GENERATOR = File.expand_path('../bin/generate', __dir__)
  DRC = File.expand_path('../bin/drc', __dir__)

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
      assert_includes File.read(File.join(out, 'ravenoc_top.sv')), 'module ravenoc_top'
      assert_includes File.read(File.join(out, 'ravenoc_top.sv')), 'ravenoc #('
      refute File.exist?(File.join(out, 'ravenoc_demo_top.sv')),
             'demo-only top should not be generated'
      filelist = File.read(File.join(out, 'ravenoc_filelist.f'))
      assert_includes filelist, '+define+NOC_CFG_SZ_ROWS=2'
      assert_includes filelist, File.join(out, 'src/ravenoc.sv')
      assert_includes filelist, File.join(out, 'ravenoc_top.sv')
      refute_includes filelist, 'ravenoc_demo_top.sv'
      assert_includes filelist, "+incdir+#{out}"
      assert_includes filelist, "+incdir+#{File.join(out, 'src/include')}"
      refute_includes filelist, 'ravenoc_axi_fnc.svh'
      refute_includes filelist, 'ravenoc_defines.svh'
      refute_includes filelist, 'ravenoc_structs.svh'
      refute_includes filelist, vendor
      assert File.exist?(File.join(out, 'src/ravenoc.sv')),
             'vendor source should be copied under output src/'
      assert File.exist?(File.join(out, 'src/include/ravenoc_pkg.sv')),
             'vendor include should be copied under output src/include/'
      assert File.exist?(File.join(out, 'bus_arch_sv_pkg/amba_axi_pkg.sv')),
             'vendor bus package should be copied under output bus_arch_sv_pkg/'
      assert File.executable?(File.join(out, 'verify.sh')), 'verify.sh should be executable'
      assert_includes File.read(File.join(out, 'verify.sh')), '--lint-only'

      manifest = JSON.parse(File.read(File.join(out, 'manifest.json')))
      assert_equal 'finepaper.ravenoc', manifest.fetch('ipcore')
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

  def test_generates_endpoint_dummy_top_from_internal_endpoint_connection
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'endpoint_graph.json', internal_graph_with_endpoint)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)
      out = File.join(dir, 'out')

      _stdout, stderr, status = run_generator(input, out, vendor)

      assert status.success?, stderr
      top = File.read(File.join(out, 'ravenoc_top.sv'))
      assert_includes top, 'module ravenoc_top'
      assert_includes top, 'ravenoc_endpoint_dummy'
      assert_includes top, '.RAVENOC_SLOT(0)'
      assert_includes top, 'u_ep0_host_0_dummy'
      assert_includes top, 'assign axi_mosi_if[0] = ep0_axi_mosi;'
      assert_includes top, 'ravenoc #('
      refute_includes top, 'ENDPOINT_INDEX'
      refute_includes top, '9ed21db3_a343_4420_afcb_d6b19cb997fe'

      dummy = File.read(File.join(out, 'ravenoc_endpoint_dummy.sv'))
      assert_includes dummy, 'module ravenoc_endpoint_dummy'
      refute_includes dummy, 'ENDPOINT_INDEX'

      filelist = File.read(File.join(out, 'ravenoc_filelist.f'))
      assert_includes filelist, File.join(out, 'ravenoc_endpoint_dummy.sv')
      assert_includes filelist, File.join(out, 'ravenoc_top.sv')

      manifest = JSON.parse(File.read(File.join(out, 'manifest.json')))
      endpoint = manifest.fetch('module').fetch('endpoints').first
      assert_equal 'host_0', endpoint.fetch('id')
      assert_equal 'rave_00', endpoint.fetch('tile')
      assert_equal 0, endpoint.fetch('slot')
      refute endpoint.key?('endpoint_index')
    end
  end

  def test_generates_from_ipcraft_project_schema
    Dir.mktmpdir do |dir|
      graph = ipcraft_project_from_ipcore_graph(internal_graph_with_endpoint)
      assert_equal package_command_schema('generate'), graph.fetch('schema')
      input = write_json(dir, 'ipcraft_project.json', graph)
      vendor = File.join(dir, 'vendor/ravenoc')
      make_fake_vendor(vendor)
      out = File.join(dir, 'out')

      stdout, stderr, status = run_generator(input, out, vendor)

      assert status.success?, stderr
      assert_includes stdout, 'Generated RaveNoC integration'
      manifest = JSON.parse(File.read(File.join(out, 'manifest.json')))
      assert_equal 'internal_graph', manifest.fetch('module').fetch('type')
      endpoint = manifest.fetch('module').fetch('endpoints').first
      assert_equal 'host_0', endpoint.fetch('id')
      assert_equal 'rave_00', endpoint.fetch('tile')
    end
  end

  def test_drc_validates_internal_graph_without_vendor_source
    Dir.mktmpdir do |_dir|
      input = File.expand_path('../examples/internal_mesh_2x2.json', __dir__)

      stdout, stderr, status = run_drc(input)

      assert status.success?, stderr
      assert_includes stdout, 'RaveNoC DRC passed'
    end
  end

  def test_drc_accepts_manually_placed_tiles_with_default_mesh_coordinates
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'manual_tiles.json', manually_placed_tile_graph(include_mesh_connections: true))

      stdout, stderr, status = run_drc(input)

      assert status.success?, stderr
      assert_includes stdout, 'RaveNoC DRC passed'
    end
  end

  def test_drc_accepts_ipcraft_project_schema
    Dir.mktmpdir do |dir|
      graph = ipcraft_project_from_ipcore_graph(internal_graph_with_endpoint)
      assert_equal package_command_schema('validate'), graph.fetch('schema')
      input = write_json(dir, 'ipcraft_project.json', graph)

      stdout, stderr, status = run_drc(input)

      assert status.success?, stderr
      assert_includes stdout, 'RaveNoC DRC passed'
    end
  end

  def test_drc_rejects_manual_tiles_without_mesh_connections
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'manual_tiles_without_links.json', manually_placed_tile_graph)

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'missing mesh link'
    end
  end

  def test_drc_accepts_one_dimensional_internal_mesh_allowed_by_upstream
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'one_by_two.json', one_by_two_tile_graph)

      stdout, stderr, status = run_drc(input)

      assert status.success?, stderr
      assert_includes stdout, 'RaveNoC DRC passed'
    end
  end

  def test_rejects_flit_type_width_other_than_two
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph_global_parameters(graph)['flit_type_width'] = 3
      input = write_json(dir, 'graph.json', graph)

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'flit_type_width must be 2'
    end
  end

  def test_rejects_unsupported_flit_data_width
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph_global_parameters(graph)['flit_data_width'] = 128
      graph_global_parameters(graph)['axi_data_width'] = 128
      input = write_json(dir, 'graph.json', graph)

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'flit_data_width must be 32 or 64'
    end
  end

  def test_rejects_virtual_channels_above_upstream_limit
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph_global_parameters(graph)['virtual_channels'] = 33
      input = write_json(dir, 'graph.json', graph)

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'virtual_channels must be 1-32'
    end
  end

  def test_rejects_axi_data_width_that_differs_from_flit_data_width
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph_global_parameters(graph)['flit_data_width'] = 32
      graph_global_parameters(graph)['axi_data_width'] = 64
      input = write_json(dir, 'graph.json', graph)

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'axi_data_width must equal flit_data_width'
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
      graph_global_parameters(graph)['flit_buffer_depth'] = 3
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

  def test_rejects_missing_global_parameter
    Dir.mktmpdir do |dir|
      graph = valid_graph
      graph_global_parameters(graph).delete('flit_data_width')
      input = write_json(dir, 'missing_global_param.json', graph)

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'missing RaveNoC global parameter flit_data_width'
    end
  end

  def test_drc_schema_error_mentions_current_schema
    Dir.mktmpdir do |dir|
      input = write_json(dir, 'bad_schema.json', { 'schema' => 'unexpected.schema.v1' })

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'ipcraft.noc.project.v1'
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

  def run_drc(input)
    Open3.capture3(
      RbConfig.ruby,
      DRC,
      '-i', input
    )
  end

  def write_json(root, relative, data)
    path = File.join(root, relative)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, JSON.pretty_generate(data))
    path
  end

  def package_manifest
    @package_manifest ||= JSON.parse(File.read(File.expand_path('../../ipcraft.json', __dir__)))
  end

  def package_id
    package_manifest.fetch('id')
  end

  def package_command_schema(command)
    IPCRAFT_PROJECT_SCHEMA
  end

  def ipcraft_project_from_ipcore_graph(graph)
    legacy = JSON.parse(JSON.generate(graph))
    state = legacy.fetch('ipcore_state', []).find { |record| record['ipcore'] == package_id } || {}
    port_ids_by_module = Hash.new { |hash, key| hash[key] = [] }

    legacy.fetch('connections', []).each do |connection|
      %w[source target].each do |endpoint|
        ref = connection.fetch(endpoint)
        port_ids_by_module[ref.fetch('module')] << ref.fetch('port')
      end
    end

    {
      'schema' => package_command_schema('generate'),
      'package' => package_id,
      'graph' => { 'name' => legacy.fetch('name') },
      'project' => {
        'name' => legacy.fetch('name'),
        'instance' => {
          'id' => state.fetch('instance', 'ravenoc_0'),
          'package' => package_id,
          'schema' => state.fetch('schema', 'finepaper.ravenoc-project-state-v1'),
          'state' => state.fetch('state', {})
        }
      },
      'instances' => legacy.fetch('modules', []).map do |mod|
        {
          'id' => mod.fetch('id'),
          'module' => mod.fetch('type'),
          'parameters' => mod.fetch('parameters', {}),
          'interfaces' => port_ids_by_module[mod.fetch('id')].uniq.map do |port|
            { 'id' => "if_#{port}", 'port' => port }
          end
        }
      end,
      'connections' => legacy.fetch('connections', []).map do |connection|
        {
          'id' => connection.fetch('id'),
          'class' => 'router_link',
          'status' => 'valid',
          'interfaces' => %w[source target].map do |endpoint|
            ref = connection.fetch(endpoint)
            { 'instance' => ref.fetch('module'), 'interface' => "if_#{ref.fetch('port')}" }
          end
        }
      end
    }
  end

  def valid_graph
    {
      'schema' => 'finepaper-ipcore-graph-v1',
      'name' => 'demo',
      'ipcore_state' => [ravenoc_ipcore_state],
      'modules' => [
        {
          'id' => 'ravenoc_node',
          'ipcore' => 'finepaper.ravenoc',
          'type' => 'RaveNoC',
          'parameters' => {
            'rows' => 2,
            'cols' => 2
          }
        }
      ],
      'connections' => []
    }
  end

  def graph_global_parameters(graph)
    graph.fetch('ipcore_state')
         .find { |state| state.fetch('ipcore') == 'finepaper.ravenoc' }
         .fetch('state')
         .fetch('global_parameters')
  end

  def ravenoc_ipcore_state
    {
      'ipcore' => 'finepaper.ravenoc',
      'instance' => 'ravenoc_0',
      'schema' => 'finepaper.ravenoc-project-state-v1',
      'state' => {
        'kind' => 'noc',
        'type' => 'RaveNoC',
        'global_parameters' => {
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
    }
  end

  def manually_placed_tile_graph(include_mesh_connections: false)
    tile = lambda do |id, x, y|
      {
        'id' => id,
        'ipcore' => 'finepaper.ravenoc',
        'type' => 'RaveTile',
        'parameters' => {
          'x' => x,
          'y' => y,
          'mesh_col' => 0,
          'mesh_row' => 0
        }
      }
    end

    {
      'schema' => 'finepaper-ipcore-graph-v1',
      'name' => 'manual_tiles',
      'ipcore_state' => [ravenoc_ipcore_state],
      'modules' => [
        tile.call('rave_a', 100, 80),
        tile.call('rave_b', 320, 80),
        tile.call('rave_c', 100, 248),
        tile.call('rave_d', 320, 248)
      ],
      'connections' => include_mesh_connections ? manual_mesh_connections : []
    }
  end

  def manual_mesh_connections
    [
      {
        'id' => 'rave_a_east',
        'source' => { 'module' => 'rave_a', 'port' => 'east' },
        'target' => { 'module' => 'rave_b', 'port' => 'west' }
      },
      {
        'id' => 'rave_c_east',
        'source' => { 'module' => 'rave_c', 'port' => 'east' },
        'target' => { 'module' => 'rave_d', 'port' => 'west' }
      },
      {
        'id' => 'rave_a_south',
        'source' => { 'module' => 'rave_a', 'port' => 'south' },
        'target' => { 'module' => 'rave_c', 'port' => 'north' }
      },
      {
        'id' => 'rave_b_south',
        'source' => { 'module' => 'rave_b', 'port' => 'south' },
        'target' => { 'module' => 'rave_d', 'port' => 'north' }
      }
    ]
  end

  def one_by_two_tile_graph
    {
      'schema' => 'finepaper-ipcore-graph-v1',
      'name' => 'one_by_two',
      'ipcore_state' => [ravenoc_ipcore_state],
      'modules' => [
        {
          'id' => 'rave_a',
          'ipcore' => 'finepaper.ravenoc',
          'type' => 'RaveTile',
          'parameters' => { 'x' => 100, 'y' => 80, 'mesh_col' => 0, 'mesh_row' => 0 }
        },
        {
          'id' => 'rave_b',
          'ipcore' => 'finepaper.ravenoc',
          'type' => 'RaveTile',
          'parameters' => { 'x' => 320, 'y' => 80, 'mesh_col' => 1, 'mesh_row' => 0 }
        }
      ],
      'connections' => [
        {
          'id' => 'rave_a_east',
          'source' => { 'module' => 'rave_a', 'port' => 'east' },
          'target' => { 'module' => 'rave_b', 'port' => 'west' }
        }
      ]
    }
  end

  def internal_graph_with_endpoint
    graph = manually_placed_tile_graph(include_mesh_connections: true)
    graph.fetch('modules').find { |mod| mod.fetch('id') == 'rave_a' }
         .fetch('parameters')['external_id'] = 'rave_00'
    graph.fetch('modules') << {
      'id' => '9ed21db3_a343_4420_afcb_d6b19cb997fe',
      'ipcore' => 'finepaper.ravenoc',
      'type' => 'RaveEndpoint',
      'parameters' => {
        'external_id' => 'host_0',
        'endpoint_index' => 7
      }
    }
    graph.fetch('connections') << {
      'id' => 'host_0_to_rave_a',
      'source' => { 'module' => '9ed21db3_a343_4420_afcb_d6b19cb997fe', 'port' => 'noc' },
      'target' => { 'module' => 'rave_a', 'port' => 'local' }
    }
    graph
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
