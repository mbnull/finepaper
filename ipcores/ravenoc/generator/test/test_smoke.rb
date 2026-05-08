require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'

class RaveNoCSmokeTest < Minitest::Test
  PLUGIN_ROOT = File.expand_path('../..', __dir__)
  GENERATOR = File.expand_path('../bin/generate', __dir__)
  EXAMPLE = File.expand_path('../examples/default_2x2.json', __dir__)
  VENDOR = File.join(PLUGIN_ROOT, 'vendor/ravenoc')

  def test_default_2x2_verilator_lint
    skip 'verilator is not installed' unless system('which verilator > /dev/null 2>&1')
    skip 'RaveNoC submodule is not initialized' unless File.file?(File.join(VENDOR, 'src/ravenoc.sv'))

    Dir.mktmpdir do |dir|
      out = File.join(dir, 'out')
      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        GENERATOR,
        '-i', EXAMPLE,
        '-o', out,
        '-t', File.expand_path('../template', __dir__)
      )
      assert status.success?, stderr
      assert_includes stdout, 'Generated RaveNoC integration'

      _verify_stdout, verify_stderr, verify_status = Open3.capture3('bash', 'verify.sh', chdir: out)
      assert verify_status.success?, verify_stderr
    end
  end
end
