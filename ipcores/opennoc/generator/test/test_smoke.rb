$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'minitest/autorun'
require 'open3'
require 'tmpdir'
require 'rbconfig'

class OpenNoCSmokeTest < Minitest::Test
  ROOT = File.expand_path('..', __dir__)
  GENERATOR = File.join(ROOT, 'bin/generate')
  EXAMPLE = File.join(ROOT, 'examples/mesh_2x2.json')
  VENDOR_MESH_GENERATOR = File.expand_path('../vendor/OpenNoC/tools/mesh_generator/mesh_gen.py', ROOT)

  def test_vendor_mesh_2x2_generation_and_optional_verilator_lint
    skip "OpenNoC vendor mesh_gen.py is missing at #{VENDOR_MESH_GENERATOR}" unless File.file?(VENDOR_MESH_GENERATOR)

    Dir.mktmpdir do |dir|
      output = File.join(dir, 'out')
      stdout, stderr, status = Open3.capture3(RbConfig.ruby, GENERATOR, '-i', EXAMPLE, '-o', output)

      assert status.success?, stderr
      assert_includes stdout, 'Generated OpenNoC mesh integration'
      assert File.file?(File.join(output, 'mesh_wrapper_2x2.sv')), 'expected generated mesh_wrapper_2x2.sv'

      skip 'verilator is not installed' unless verilator_installed?

      # verify.sh carries only the documented upstream Verilator suppressions
      # needed by the pinned OpenNoC vendor snapshot.
      _verify_stdout, verify_stderr, verify_status = Open3.capture3('./verify.sh', chdir: output)
      assert verify_status.success?, verify_stderr
    end
  end

  private

  def verilator_installed?
    _stdout, _stderr, status = Open3.capture3('verilator', '--version')
    status.success?
  rescue Errno::ENOENT
    false
  end
end
