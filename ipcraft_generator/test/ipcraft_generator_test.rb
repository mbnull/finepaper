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
      input_path = File.join(dir, 'input.json')
      output = File.join(dir, 'out')
      File.write(manifest_path, JSON.pretty_generate(minimal_manifest))
      File.write(input_path, JSON.pretty_generate(minimal_project))

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
      input_path = File.join(dir, 'input.json')
      output = File.join(dir, 'out')
      File.write(manifest_path, JSON.pretty_generate(minimal_manifest))
      File.write(input_path, JSON.pretty_generate(minimal_project))

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

  def test_generates_finepaper_noc_structural_outputs
    Dir.mktmpdir do |dir|
      input_path = File.join(dir, 'input.json')
      output = File.join(dir, 'out')
      File.write(input_path, JSON.pretty_generate(finepaper_noc_project))

      IpcraftGenerator::Generator.new(
        manifest: File.join(PROJECT_ROOT, 'ipcores/finepaper-noc/ipcraft.json'),
        input: input_path,
        output: output
      ).generate

      assert_path_exists File.join(output, 'manifest.json')
      assert_path_exists File.join(output, 'filelist.f')
      assert_path_exists File.join(output, 'rtl/top.v')

      output_manifest = JSON.parse(File.read(File.join(output, 'manifest.json')))
      assert_equal 'finepaper.noc', output_manifest.fetch('ipcore')
      assert_equal 4, output_manifest.fetch('routers')
    end
  end

  private

  def assert_cli_error(message, *args)
    stdout, stderr, status = Open3.capture3(RbConfig.ruby, CLI, *args)

    refute status.success?
    assert_empty stdout
    assert_includes stderr, message
  end

  def minimal_manifest
    {
      'schema' => 'ipcraft.manifest.v1',
      'id' => 'org.example.noc',
      'name' => 'Example',
      'generation' => {
        'engine' => 'ipcraft.common.v1',
        'outputs' => [
          { 'id' => 'manifest', 'kind' => 'json', 'path' => 'manifest.json' }
        ]
      }
    }
  end

  def minimal_project
    {
      'schema' => 'ipcraft.noc.project.v1',
      'package' => 'org.example.noc',
      'instances' => [
        { 'id' => 'router' },
        { 'id' => 'endpoint' }
      ],
      'connections' => [
        { 'id' => 'link' }
      ]
    }
  end

  def expected_output_manifest
    {
      'ipcore' => 'org.example.noc',
      'schema' => 'ipcraft.noc.project.v1',
      'instance_count' => 2,
      'connection_count' => 1
    }
  end

  def finepaper_noc_project
    {
      'schema' => 'ipcraft.noc.project.v1',
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
end
