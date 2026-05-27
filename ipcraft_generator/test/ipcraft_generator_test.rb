$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'ipcraft_generator'
require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'

class IpcraftGeneratorTest < Minitest::Test
  ROOT = File.expand_path('..', __dir__)
  PROJECT_ROOT = File.expand_path('..', ROOT)
  CLI = File.join(ROOT, 'bin/ipcraft-generate')

  def test_cli_requires_manifest
    assert_cli_error('error: --manifest is required')
  end

  def test_cli_rejects_invalid_option
    assert_cli_error('error: invalid option: --bogus', '--bogus')
  end

  def test_cli_rejects_missing_option_argument
    assert_cli_error('error: missing argument: --manifest', '--manifest')
  end

  def test_cli_rejects_unexpected_positional_argument
    Dir.mktmpdir do |dir|
      manifest_path = File.join(dir, 'ipcraft.json')
      output = File.join(dir, 'out')
      File.write(manifest_path, JSON.pretty_generate(minimal_manifest))
      input_path = write_emitted_inputs(dir, minimal_project)

      assert_cli_error(
        'error: unexpected argument: extra',
        '--manifest', manifest_path,
        '--input', input_path,
        '--output', output,
        'extra'
      )
    end
  end

  def test_loads_manifest_project_and_writes_output_manifest
    Dir.mktmpdir do |dir|
      manifest_path = File.join(dir, 'ipcraft.json')
      output = File.join(dir, 'out')
      File.write(manifest_path, JSON.pretty_generate(minimal_manifest))
      input_path = write_emitted_inputs(dir, minimal_project)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby, CLI,
        '--manifest', manifest_path,
        '--input', input_path,
        '--output', output
      )

      assert status.success?, stderr
      assert_includes stdout, "Generated ipcraft output in #{output}"
      assert_equal expected_output_manifest, JSON.parse(File.read(File.join(output, 'manifest.json')))
    end
  end

  def test_generic_generation_rejects_unknown_connection_instance
    project = generic_project
    project.fetch('connections').first.fetch('interfaces').last['instance'] = 'missing_router'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_generic_project(project)
    end
    assert_includes error.message, 'link'
    assert_includes error.message, 'unknown instance missing_router'
  end

  def test_generic_generation_rejects_unknown_module_id
    project = generic_project
    project.fetch('instances').first['module'] = 'MissingTile'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_generic_project(project)
    end
    assert_includes error.message, 'router'
    assert_includes error.message, 'unknown module MissingTile'
  end

  def test_generic_generation_rejects_unknown_connection_class
    project = generic_project
    project.fetch('connections').first['class'] = 'ghost_link'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_generic_project(project)
    end
    assert_includes error.message, 'link'
    assert_includes error.message, 'unknown connection class ghost_link'
  end

  def test_generic_generation_rejects_unknown_connection_interface
    project = generic_project
    project.fetch('connections').first.fetch('interfaces').first['interface'] = 'missing_interface'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_generic_project(project)
    end
    assert_includes error.message, 'link'
    assert_includes error.message, 'unknown interface router.missing_interface'
  end

  def test_generic_generation_rejects_invalid_graph_config_endpoint_shape
    project = generic_project
    project.fetch('connections').first.fetch('interfaces')[0] = 'router.fabric_out'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_generic_project(project)
    end
    assert_includes error.message, 'link'
    assert_includes error.message, 'endpoint must be an object'
  end

  def test_failed_generation_removes_prior_success_manifest
    Dir.mktmpdir do |dir|
      manifest_path = File.join(dir, 'ipcraft.json')
      valid_input_path = write_emitted_inputs(dir, generic_project, root_name: 'valid_inputs')
      invalid_input_path = File.join(dir, 'invalid.json')
      output = File.join(dir, 'out')
      File.write(manifest_path, JSON.pretty_generate(generic_manifest))
      File.write(invalid_input_path, JSON.pretty_generate({
        'schema' => 'ipcraft.emitted-inputs.v0',
        'package' => { 'id' => 'org.example.generic', 'version' => '1.0.0' },
        'files' => []
      }))

      IpcraftGenerator::Generator.new(
        manifest: manifest_path,
        input: valid_input_path,
        output: output
      ).generate
      assert_path_exists File.join(output, 'manifest.json')

      assert_raises(IpcraftGenerator::Error) do
        IpcraftGenerator::Generator.new(
          manifest: manifest_path,
          input: invalid_input_path,
          output: output
        ).generate
      end
      refute_path_exists File.join(output, 'manifest.json')
    end
  end

  def test_generates_finepaper_noc_structural_outputs
    Dir.mktmpdir do |dir|
      output = File.join(dir, 'out')
      input_path = write_emitted_inputs(dir, finepaper_noc_project)

      IpcraftGenerator::Generator.new(
        manifest: File.join(PROJECT_ROOT, 'ipcores/finepaper-noc/ipcraft.json'),
        input: input_path,
        output: output
      ).generate

      expected_common_outputs = ['filelist.f', 'manifest.json', 'rtl/top.v']
      expected_common_outputs.each do |path|
        assert_path_exists File.join(output, path)
      end

      manifest = JSON.parse(File.read(File.join(PROJECT_ROOT, 'ipcores/finepaper-noc/ipcraft.json')))
      declared_outputs = editor_manifest(manifest).fetch('generation').fetch('outputs').map { |entry| entry.fetch('path') }
      assert_empty expected_common_outputs - declared_outputs
      declared_outputs.each do |path|
        assert_path_exists File.join(output, path)
      end

      output_manifest = JSON.parse(File.read(File.join(output, 'manifest.json')))
      assert_equal 'finepaper.noc', output_manifest.fetch('ipcore')
      assert_equal 4, output_manifest.fetch('routers')
    end
  end

  def test_generates_ravenoc_config_and_manifest
    Dir.mktmpdir do |dir|
      output = File.join(dir, 'out')
      input_path = write_emitted_inputs(dir, ravenoc_project)

      IpcraftGenerator::Generator.new(
        manifest: File.join(PROJECT_ROOT, 'ipcores/ravenoc/ipcraft.json'),
        input: input_path,
        output: output
      ).generate

      config = File.read(File.join(output, 'ravenoc_config.svh'))
      assert_includes config, '`define NOC_CFG_SZ_ROWS 2'
      assert_includes config, '`define NOC_CFG_SZ_COLS 2'

      filelist = File.read(File.join(output, 'ravenoc_filelist.f'))
      assert_includes filelist, 'ravenoc_top.sv'

      output_manifest = JSON.parse(File.read(File.join(output, 'manifest.json')))
      assert_equal 'finepaper.ravenoc', output_manifest.fetch('ipcore')
      assert_equal 4, output_manifest.fetch('tiles')
    end
  end

  def test_ravenoc_generation_rejects_project_without_tiles_or_wrapper
    project = ravenoc_project
    project['instances'] = []
    project['connections'] = []

    error = assert_raises(IpcraftGenerator::Error) do
      generate_ravenoc_project(project)
    end
    assert_includes error.message, 'expected RaveTile instances or one RaveNoC wrapper instance'
  end

  def test_ravenoc_generation_validates_global_parameters
    invalid_parameters = [
      ['flit_data_width', '32', 'flit_data_width must be a positive integer'],
      ['flit_data_width', 16, 'flit_data_width must be 32 or 64'],
      ['flit_type_width', 3, 'flit_type_width must be 2'],
      ['flit_buffer_depth', 3, 'flit_buffer_depth must be a power of two'],
      ['virtual_channels', 33, 'virtual_channels must be 1-32'],
      ['max_packet_flits', 0, 'max_packet_flits must be a positive integer'],
      ['axi_addr_width', 0, 'axi_addr_width must be a positive integer'],
      ['axi_data_width', 64, 'axi_data_width must equal flit_data_width'],
      ['routing_algorithm', 'odd_even', 'routing_algorithm must be xy or yx'],
      ['priority', 'middle', 'priority must be zero_high or zero_low'],
      ['axi_cdc_required', '101', 'axi_cdc_required must be all, none, or a 4-bit binary mask']
    ]

    invalid_parameters.each do |name, value, message|
      project = ravenoc_project
      ravenoc_global_parameters(project)[name] = value

      error = assert_raises(IpcraftGenerator::Error, "expected #{name}=#{value.inspect} to fail") do
        generate_ravenoc_project(project)
      end
      assert_includes error.message, message
    end
  end

  def test_ravenoc_generation_validates_wrapper_dimensions
    invalid_dimensions = [
      [{'rows' => 0, 'cols' => 2}, 'RaveNoC rows must be a positive integer'],
      [{'rows' => 2, 'cols' => '2'}, 'RaveNoC cols must be a positive integer'],
      [{'rows' => 1, 'cols' => 1}, '1x1 is not a legal RaveNoC mesh']
    ]

    invalid_dimensions.each do |parameters, message|
      project = ravenoc_wrapper_project(parameters)

      error = assert_raises(IpcraftGenerator::Error, "expected #{parameters.inspect} to fail") do
        generate_ravenoc_project(project)
      end
      assert_includes error.message, message
    end
  end

  def test_ravenoc_generation_rejects_missing_tile_mesh_link
    project = ravenoc_project
    project.fetch('connections').reject! do |connection|
      connection.fetch('id') == 'rave_0_0_east_to_rave_0_1_west'
    end

    error = assert_raises(IpcraftGenerator::Error) do
      generate_ravenoc_project(project)
    end
    assert_includes error.message, 'missing mesh link'
    assert_includes error.message, 'rave_0_0.east -> rave_0_1.west'
  end

  def test_ravenoc_filelist_uses_output_local_paths
    Dir.mktmpdir do |dir|
      output = File.join('out')
      input_path = write_emitted_inputs(dir, ravenoc_project)

      Dir.chdir(dir) do
        IpcraftGenerator::Generator.new(
          manifest: File.join(PROJECT_ROOT, 'ipcores/ravenoc/ipcraft.json'),
          input: input_path,
          output: output
        ).generate
      end

      filelist = File.read(File.join(dir, output, 'ravenoc_filelist.f'))
      assert_includes filelist, "+incdir+.\n"
      assert_includes filelist, "\nravenoc_top.sv\n"
      refute_includes filelist, '+incdir+out'
      refute_includes filelist, 'out/ravenoc_top.sv'
    end
  end

  def test_generates_opennoc_mesh_projection_without_vendor
    Dir.mktmpdir do |dir|
      output = File.join(dir, 'out')
      input_path = write_emitted_inputs(dir, opennoc_mesh_project)

      IpcraftGenerator::Generator.new(
        manifest: File.join(PROJECT_ROOT, 'ipcores/opennoc/ipcraft.json'),
        input: input_path,
        output: output
      ).generate

      mesh_path = File.join(output, 'opennoc_mesh.json')
      assert_path_exists mesh_path

      mesh = JSON.parse(File.read(mesh_path))
      assert_equal({ 'X' => 0, 'Y' => 0, 'P0' => 'RNF', 'P1' => 'RNI' }, mesh.fetch('XP0_0'))
      assert_equal({ 'X' => 1, 'Y' => 0, 'P0' => 'HNF', 'P1' => 'NONE' }, mesh.fetch('XP1_0'))
      assert_equal({ 'X' => 0, 'Y' => 1, 'P0' => 'HNI', 'P1' => 'NONE' }, mesh.fetch('XP0_1'))
      assert_equal({ 'X' => 1, 'Y' => 1, 'P0' => 'SNF', 'P1' => 'NONE' }, mesh.fetch('XP1_1'))

      output_manifest = JSON.parse(File.read(File.join(output, 'manifest.json')))
      assert_equal 'finepaper.opennoc', output_manifest.fetch('ipcore')
      assert_equal 2, output_manifest.fetch('rows')
      assert_equal 2, output_manifest.fetch('cols')
    end
  end

  def test_opennoc_projection_supports_source_target_endpoint_aliases
    Dir.mktmpdir do |dir|
      output = File.join(dir, 'out')
      project = opennoc_mesh_project
      project['connections'] = project.fetch('connections').map { |connection| source_target_connection(connection) }
      input_path = write_emitted_inputs(dir, project)

      IpcraftGenerator::Generator.new(
        manifest: File.join(PROJECT_ROOT, 'ipcores/opennoc/ipcraft.json'),
        input: input_path,
        output: output
      ).generate

      mesh = JSON.parse(File.read(File.join(output, 'opennoc_mesh.json')))
      assert_equal({ 'X' => 0, 'Y' => 0, 'P0' => 'RNF', 'P1' => 'RNI' }, mesh.fetch('XP0_0'))
      assert_equal({ 'X' => 1, 'Y' => 1, 'P0' => 'SNF', 'P1' => 'NONE' }, mesh.fetch('XP1_1'))
    end
  end

  def test_opennoc_projection_supports_from_to_endpoint_aliases
    Dir.mktmpdir do |dir|
      output = File.join(dir, 'out')
      project = opennoc_mesh_project
      project['connections'] = project.fetch('connections').map { |connection| from_to_connection(connection) }
      input_path = write_emitted_inputs(dir, project)

      IpcraftGenerator::Generator.new(
        manifest: File.join(PROJECT_ROOT, 'ipcores/opennoc/ipcraft.json'),
        input: input_path,
        output: output
      ).generate

      mesh = JSON.parse(File.read(File.join(output, 'opennoc_mesh.json')))
      assert_equal({ 'X' => 0, 'Y' => 1, 'P0' => 'HNI', 'P1' => 'NONE' }, mesh.fetch('XP0_1'))
      assert_equal({ 'X' => 1, 'Y' => 1, 'P0' => 'SNF', 'P1' => 'NONE' }, mesh.fetch('XP1_1'))
    end
  end

  def test_opennoc_projection_rejects_unknown_interface_reference
    project = opennoc_mesh_project
    project.fetch('connections').find do |connection|
      connection.fetch('id') == 'rnf_0_to_XP0_0_p0'
    end.fetch('interfaces').first['interface'] = 'if_missing'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_opennoc_project(project)
    end
    assert_includes error.message, 'rnf_0_to_XP0_0_p0'
    assert_includes error.message, 'unknown interface rnf_0.if_missing'
  end

  def test_opennoc_projection_rejects_unknown_interface_instance
    project = opennoc_mesh_project
    project.fetch('connections').find do |connection|
      connection.fetch('id') == 'rnf_0_to_XP0_0_p0'
    end.fetch('interfaces').first['instance'] = 'missing_agent'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_opennoc_project(project)
    end
    assert_includes error.message, 'rnf_0_to_XP0_0_p0'
    assert_includes error.message, 'unknown instance missing_agent'
  end

  def test_opennoc_projection_rejects_invalid_endpoint_alias_shape
    project = opennoc_mesh_project
    project.fetch('connections').map! { |connection| source_target_connection(connection) }
    project.fetch('connections').find do |connection|
      connection.fetch('id') == 'rnf_0_to_XP0_0_p0'
    end['source'] = 'rnf_0.chi'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_opennoc_project(project)
    end
    assert_includes error.message, 'rnf_0_to_XP0_0_p0'
    assert_includes error.message, 'endpoint must be an object'
  end

  def test_opennoc_projection_rejects_invalid_xp_mesh_connection
    project = opennoc_mesh_project
    project.fetch('connections').find do |connection|
      connection.fetch('id') == 'XP0_0_east_to_XP1_0_west'
    end.fetch('interfaces').last['interface'] = 'if_north'

    error = assert_raises(IpcraftGenerator::Error) do
      generate_opennoc_project(project)
    end
    assert_includes error.message, 'XP0_0_east_to_XP1_0_west'
    assert_includes error.message, 'invalid OpenNoC XP mesh connection'
  end

  def test_opennoc_projection_reports_duplicate_coordinate
    project = opennoc_mesh_project
    project.fetch('instances').find { |instance| instance.fetch('id') == 'XP1_0' }.fetch('parameters')['mesh_col'] = 0

    error = assert_raises(IpcraftGenerator::Error) do
      generate_opennoc_project(project)
    end
    assert_includes error.message, 'duplicate coordinate 0,0'
  end

  def test_opennoc_projection_reports_negative_coordinate
    project = opennoc_mesh_project
    project.fetch('instances').find { |instance| instance.fetch('id') == 'XP0_0' }.fetch('parameters')['mesh_col'] = -1

    error = assert_raises(IpcraftGenerator::Error) do
      generate_opennoc_project(project)
    end
    assert_includes error.message, 'negative coordinate -1,0'
  end

  def test_opennoc_projection_reports_missing_coordinate
    project = opennoc_mesh_project
    project.fetch('instances').find { |instance| instance.fetch('id') == 'XP1_1' }.fetch('parameters')['mesh_row'] = 2

    error = assert_raises(IpcraftGenerator::Error) do
      generate_opennoc_project(project)
    end
    assert_includes error.message, 'missing coordinate 1,1'
  end

  def test_opennoc_projection_reports_missing_xp_mapping
    manifest = opennoc_manifest
    editor_manifest(manifest).fetch('generation').fetch('module_mappings').delete('OpenNoCXP')

    error = assert_raises(IpcraftGenerator::Error) do
      generate_opennoc_project(opennoc_mesh_project, manifest: manifest)
    end
    assert_includes error.message, 'OpenNoC generation.module_mappings must include an XP mapping'
  end

  private

  def assert_cli_error(message, *args)
    stdout, stderr, status = Open3.capture3(RbConfig.ruby, CLI, *args)

    refute status.success?
    assert_empty stdout
    assert_includes stderr, message
  end

  def write_emitted_inputs(dir, project, root_name: 'inputs')
    input_root = File.join(dir, root_name)
    FileUtils.mkdir_p(input_root)
    graph_config_path = File.join(input_root, 'graph_config.json')
    parameters_path = File.join(input_root, 'parameters.json')
    manifest_path = File.join(input_root, 'manifest.json')

    File.write(graph_config_path, JSON.pretty_generate(graph_config_for_project(project)))
    File.write(parameters_path, JSON.pretty_generate(parameters_for_project(project)))
    File.write(manifest_path, JSON.pretty_generate(emitted_inputs_manifest(project)))
    manifest_path
  end

  def emitted_inputs_manifest(project)
    package_id = project.fetch('package')
    {
      'schema' => 'ipcraft.emitted-inputs.v1',
      'project' => project.dig('project', 'name') || 'test_project',
      'instance' => project.dig('project', 'instance', 'id') || 'ip0',
      'package' => {
        'id' => package_id,
        'version' => '1.0'
      },
      'files' => [
        {
          'id' => 'graph_config',
          'kind' => 'graph_config',
          'path' => 'graph_config.json',
          'source' => { 'graph_config' => true }
        },
        {
          'id' => 'parameters',
          'kind' => 'parameters',
          'path' => 'parameters.json',
          'source' => { 'parameters' => true }
        }
      ],
      'diagnostics' => {
        'schema' => 'ipcraft.diagnostics.v1',
        'records' => []
      }
    }
  end

  def parameters_for_project(project)
    [
      project.dig('project', 'global_parameters'),
      project.dig('project', 'instance', 'state', 'global_parameters'),
      project.dig('project', 'instance', 'parameters'),
      project['parameters']
    ].find { |parameters| parameters.is_a?(Hash) } || {}
  end

  def graph_config_for_project(project)
    {
      'schema' => 'ipcraft.graph-config.v1',
      'objects' => project.fetch('instances', []).map { |instance| graph_config_object(instance) },
      'relationships' => project.fetch('connections', []).map { |connection| graph_config_relationship(project, connection) },
      'properties' => {},
      'native' => {}
    }
  end

  def graph_config_object(instance)
    {
      'id' => instance.fetch('id'),
      'type' => instance['module'] || instance['module_id'] || instance['type'],
      'properties' => instance.fetch('parameters', {})
    }
  end

  def graph_config_relationship(project, connection)
    {
      'id' => connection.fetch('id'),
      'type' => connection['class'],
      'endpoints' => graph_config_relationship_endpoints(project, connection),
      'properties' => {}
    }
  end

  def graph_config_relationship_endpoints(project, connection)
    if connection.key?('interfaces')
      return connection.fetch('interfaces').map { |endpoint| graph_config_endpoint(project, endpoint) }
    end
    if connection.key?('source') || connection.key?('target')
      return %w[source target].map { |key| graph_config_endpoint(project, connection.fetch(key)) }
    end
    if connection.key?('from') || connection.key?('to')
      return %w[from to].map { |key| graph_config_endpoint(project, connection.fetch(key)) }
    end
    []
  end

  def graph_config_endpoint(project, endpoint)
    unless endpoint.is_a?(Hash)
      return endpoint
    end

    instance = endpoint['instance'] || endpoint['module']
    role = endpoint['interface'] || endpoint['port']
    {
      'object' => instance,
      'role' => port_for_project_interface(project, instance, role)
    }
  end

  def port_for_project_interface(project, instance_id, interface_id)
    instance = project.fetch('instances', []).find { |candidate| candidate['id'] == instance_id }
    return interface_id unless instance

    interfaces = instance.fetch('interfaces', [])
    return interface_id unless interfaces.is_a?(Array)

    interfaces.each do |interface|
      return interface if interface.is_a?(String) && interface == interface_id
      next unless interface.is_a?(Hash)

      id = interface['id'] || interface['interface'] || interface['port']
      port = interface['port'] || id
      return port if id == interface_id || port == interface_id
    end
    interface_id
  end

  def minimal_manifest
    package_manifest(
      'id' => 'org.example.noc',
      'name' => 'Example',
      'generation' => {
        'engine' => 'ipcraft.common.v1',
        'outputs' => [
          { 'id' => 'manifest', 'kind' => 'json', 'path' => 'manifest.json' }
        ]
      }
    )
  end

  def minimal_project
    {
      'package' => 'org.example.noc',
      'instances' => [
        { 'id' => 'router', 'module' => 'Tile' },
        { 'id' => 'endpoint', 'module' => 'Tile' }
      ]
    }
  end

  def expected_output_manifest
    {
      'ipcore' => 'org.example.noc',
      'schema' => 'ipcraft.emitted-inputs.v1',
      'instance_count' => 2,
      'connection_count' => 0
    }
  end

  def generic_manifest
    package_manifest(
      'id' => 'org.example.generic',
      'name' => 'Generic',
      'connection_classes' => [
        { 'id' => 'fabric_link', 'roles' => %w[initiator target], 'symmetric' => false }
      ],
      'modules' => [
        {
          'id' => 'Tile',
          'interfaces' => [
            { 'id' => 'fabric_out' },
            { 'id' => 'fabric_in' }
          ]
        }
      ],
      'generation' => {
        'engine' => 'ipcraft.common.v1',
        'outputs' => [
          { 'id' => 'manifest', 'kind' => 'json', 'path' => 'manifest.json' }
        ]
      }
    )
  end

  def package_manifest(editor_fields)
    {
      'schema' => 'ipcraft.package.v1',
      'id' => editor_fields.fetch('id'),
      'name' => editor_fields.fetch('name'),
      'version' => editor_fields.fetch('version', '1.0.0'),
      'native' => {
        'ipcraft' => {
          'editor' => editor_fields
        }
      }
    }
  end

  def editor_manifest(manifest)
    manifest.fetch('native').fetch('ipcraft').fetch('editor')
  end

  def generic_project
    {
      'package' => 'org.example.generic',
      'instances' => [
        {
          'id' => 'router',
          'module' => 'Tile',
          'interfaces' => %w[fabric_out fabric_in]
        },
        {
          'id' => 'endpoint',
          'module' => 'Tile',
          'interfaces' => %w[fabric_out fabric_in]
        }
      ],
      'connections' => [
        {
          'id' => 'link',
          'class' => 'fabric_link',
          'source' => { 'instance' => 'router', 'interface' => 'fabric_out' },
          'target' => { 'instance' => 'endpoint', 'interface' => 'fabric_in' },
          'interfaces' => [
            { 'instance' => 'router', 'interface' => 'fabric_out' },
            { 'instance' => 'endpoint', 'interface' => 'fabric_in' }
          ]
        }
      ]
    }
  end

  def finepaper_noc_project
    {
      'package' => 'finepaper.noc',
      'instances' => [
        {
          'id' => 'xp_0_0',
          'module' => 'XP',
          'parameters' => { 'mesh_col' => 0, 'mesh_row' => 0 }
        },
        {
          'id' => 'xp_0_1',
          'module' => 'XP',
          'parameters' => { 'mesh_col' => 1, 'mesh_row' => 0 }
        },
        {
          'id' => 'xp_1_0',
          'module' => 'XP',
          'parameters' => { 'mesh_col' => 0, 'mesh_row' => 1 }
        },
        {
          'id' => 'xp_1_1',
          'module' => 'XP',
          'parameters' => { 'mesh_col' => 1, 'mesh_row' => 1 }
        },
        {
          'id' => 'endpoint_0',
          'module' => 'Endpoint',
          'parameters' => { 'type' => 'master', 'protocol' => 'axi4' }
        },
        {
          'id' => 'endpoint_1',
          'module' => 'Endpoint',
          'parameters' => { 'type' => 'slave', 'protocol' => 'axi4' }
        }
      ],
      'connections' => [
        {
          'id' => 'endpoint_0_to_xp_0_0',
          'class' => 'ni_link',
          'from' => { 'instance' => 'endpoint_0', 'interface' => 'noc' },
          'to' => { 'instance' => 'xp_0_0', 'interface' => 'local0' }
        },
        {
          'id' => 'endpoint_1_to_xp_1_1',
          'class' => 'ni_link',
          'from' => { 'instance' => 'endpoint_1', 'interface' => 'noc' },
          'to' => { 'instance' => 'xp_1_1', 'interface' => 'local0' }
        }
      ]
    }
  end

  def ravenoc_project
    {
      'package' => 'finepaper.ravenoc',
      'project' => {
        'name' => 'ravenoc_common_2x2',
        'instance' => {
          'id' => 'ravenoc_0',
          'package' => 'finepaper.ravenoc',
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
      },
      'instances' => [
        ravenoc_tile('rave_0_0', 0, 0),
        ravenoc_tile('rave_0_1', 1, 0),
        ravenoc_tile('rave_1_0', 0, 1),
        ravenoc_tile('rave_1_1', 1, 1)
      ],
      'connections' => [
        ravenoc_mesh_connection('rave_0_0_east_to_rave_0_1_west', ['rave_0_0', 'east'], ['rave_0_1', 'west']),
        ravenoc_mesh_connection('rave_1_0_east_to_rave_1_1_west', ['rave_1_0', 'east'], ['rave_1_1', 'west']),
        ravenoc_mesh_connection('rave_0_0_south_to_rave_1_0_north', ['rave_0_0', 'south'], ['rave_1_0', 'north']),
        ravenoc_mesh_connection('rave_0_1_south_to_rave_1_1_north', ['rave_0_1', 'south'], ['rave_1_1', 'north'])
      ]
    }
  end

  def ravenoc_wrapper_project(parameters)
    project = ravenoc_project
    project['instances'] = [
      {
        'id' => 'ravenoc_wrapper_0',
        'module' => 'RaveNoC',
        'parameters' => parameters
      }
    ]
    project['connections'] = []
    project
  end

  def ravenoc_tile(id, col, row)
    {
      'id' => id,
      'module' => 'RaveTile',
      'parameters' => { 'external_id' => id, 'mesh_col' => col, 'mesh_row' => row },
      'interfaces' => %w[east west north south local].map { |port| { 'id' => "if_#{port}", 'port' => port } }
    }
  end

  def ravenoc_mesh_connection(id, left, right)
    {
      'id' => id,
      'class' => 'ravenoc_router_link',
      'interfaces' => [
        { 'instance' => left.fetch(0), 'interface' => "if_#{left.fetch(1)}" },
        { 'instance' => right.fetch(0), 'interface' => "if_#{right.fetch(1)}" }
      ]
    }
  end

  def opennoc_mesh_project
    {
      'package' => 'finepaper.opennoc',
      'instances' => [
        opennoc_xp('XP0_0', 0, 0),
        opennoc_xp('XP1_0', 1, 0),
        opennoc_xp('XP0_1', 0, 1),
        opennoc_xp('XP1_1', 1, 1),
        opennoc_agent('rnf_0', 'OpenNoCRNF'),
        opennoc_agent('rni_0', 'OpenNoCRNI'),
        opennoc_agent('hnf_0', 'OpenNoCHNF'),
        opennoc_agent('hni_0', 'OpenNoCHNI'),
        opennoc_agent('snf_0', 'OpenNoCSNF')
      ],
      'connections' => [
        opennoc_connection('XP0_0_east_to_XP1_0_west', 'opennoc_mesh_link', ['XP0_0', 'east'], ['XP1_0', 'west']),
        opennoc_connection('XP0_1_east_to_XP1_1_west', 'opennoc_mesh_link', ['XP0_1', 'east'], ['XP1_1', 'west']),
        opennoc_connection('XP0_0_south_to_XP0_1_north', 'opennoc_mesh_link', ['XP0_0', 'south'], ['XP0_1', 'north']),
        opennoc_connection('XP1_0_south_to_XP1_1_north', 'opennoc_mesh_link', ['XP1_0', 'south'], ['XP1_1', 'north']),
        opennoc_connection('rnf_0_to_XP0_0_p0', 'chi_node_interface', ['rnf_0', 'chi'], ['XP0_0', 'p0']),
        opennoc_connection('rni_0_to_XP0_0_p1', 'chi_node_interface', ['rni_0', 'chi'], ['XP0_0', 'p1']),
        opennoc_connection('hnf_0_to_XP1_0_p0', 'chi_node_interface', ['hnf_0', 'chi'], ['XP1_0', 'p0']),
        opennoc_connection('hni_0_to_XP0_1_p0', 'chi_node_interface', ['hni_0', 'chi'], ['XP0_1', 'p0']),
        opennoc_connection('snf_0_to_XP1_1_p0', 'chi_node_interface', ['snf_0', 'chi'], ['XP1_1', 'p0'])
      ]
    }
  end

  def opennoc_xp(id, col, row)
    {
      'id' => id,
      'module' => 'OpenNoCXP',
      'parameters' => { 'external_id' => id, 'mesh_col' => col, 'mesh_row' => row },
      'interfaces' => %w[east west north south p0 p1].map { |port| { 'id' => "if_#{port}", 'port' => port } }
    }
  end

  def opennoc_agent(id, mod)
    {
      'id' => id,
      'module' => mod,
      'parameters' => {},
      'interfaces' => [{ 'id' => 'if_chi', 'port' => 'chi' }]
    }
  end

  def opennoc_connection(id, connection_class, left, right)
    {
      'id' => id,
      'class' => connection_class,
      'interfaces' => [
        { 'instance' => left.fetch(0), 'interface' => "if_#{left.fetch(1)}" },
        { 'instance' => right.fetch(0), 'interface' => "if_#{right.fetch(1)}" }
      ]
    }
  end

  def source_target_connection(connection)
    refs = connection.fetch('interfaces')
    {
      'id' => connection.fetch('id'),
      'class' => connection.fetch('class'),
      'source' => endpoint_alias(refs.fetch(0)),
      'target' => endpoint_alias(refs.fetch(1))
    }
  end

  def from_to_connection(connection)
    refs = connection.fetch('interfaces')
    {
      'id' => connection.fetch('id'),
      'class' => connection.fetch('class'),
      'from' => endpoint_alias(refs.fetch(0)),
      'to' => endpoint_alias(refs.fetch(1))
    }
  end

  def endpoint_alias(ref)
    {
      'module' => ref.fetch('instance'),
      'port' => ref.fetch('interface').delete_prefix('if_')
    }
  end

  def generate_generic_project(project, manifest: generic_manifest)
    Dir.mktmpdir do |dir|
      manifest_path = File.join(dir, 'ipcraft.json')
      File.write(manifest_path, JSON.pretty_generate(manifest))
      input_path = write_emitted_inputs(dir, project)

      IpcraftGenerator::Generator.new(
        manifest: manifest_path,
        input: input_path,
        output: File.join(dir, 'out')
      ).generate
    end
  end

  def generate_opennoc_project(project, manifest: opennoc_manifest)
    Dir.mktmpdir do |dir|
      manifest_path = File.join(dir, 'ipcraft.json')
      File.write(manifest_path, JSON.pretty_generate(manifest))
      input_path = write_emitted_inputs(dir, project)

      IpcraftGenerator::Generator.new(
        manifest: manifest_path,
        input: input_path,
        output: File.join(dir, 'out')
      ).generate
    end
  end

  def generate_ravenoc_project(project, manifest: ravenoc_manifest)
    Dir.mktmpdir do |dir|
      manifest_path = File.join(dir, 'ipcraft.json')
      File.write(manifest_path, JSON.pretty_generate(manifest))
      input_path = write_emitted_inputs(dir, project)

      IpcraftGenerator::Generator.new(
        manifest: manifest_path,
        input: input_path,
        output: File.join(dir, 'out')
      ).generate
    end
  end

  def ravenoc_manifest
    JSON.parse(File.read(File.join(PROJECT_ROOT, 'ipcores/ravenoc/ipcraft.json')))
  end

  def ravenoc_global_parameters(project)
    project.fetch('project').fetch('instance').fetch('state').fetch('global_parameters')
  end

  def opennoc_manifest
    JSON.parse(File.read(File.join(PROJECT_ROOT, 'ipcores/opennoc/ipcraft.json')))
  end
end
