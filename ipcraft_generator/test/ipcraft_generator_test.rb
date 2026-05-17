$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'

class IpcraftGeneratorTest < Minitest::Test
  ROOT = File.expand_path('..', __dir__)
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
end
