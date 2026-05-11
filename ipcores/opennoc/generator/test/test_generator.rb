$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'fileutils'
require 'json'
require 'minitest/autorun'
require 'open3'
require 'tmpdir'
require 'rbconfig'

class OpenNoCGeneratorTest < Minitest::Test
  GENERATOR = File.expand_path('../bin/generate', __dir__)
  DRC = File.expand_path('../bin/drc', __dir__)

  def test_drc_accepts_valid_mesh
    stdout, stderr, status = run_drc(File.expand_path('../examples/mesh_2x2.json', __dir__))
    assert status.success?, stderr
    assert_includes stdout, 'OpenNoC DRC passed'
  end

  def test_generator_writes_mesh_json_filelist_verify_and_manifest
    Dir.mktmpdir do |dir|
      vendor = File.join(dir, 'vendor/OpenNoC')
      make_fake_vendor(vendor)
      output = File.join(dir, 'out')

      stdout, stderr, status = run_generator(File.expand_path('../examples/mesh_2x2.json', __dir__), output, vendor)

      assert status.success?, stderr
      assert_includes stdout, 'Generated OpenNoC mesh integration'
      mesh = JSON.parse(File.read(File.join(output, 'opennoc_mesh.json')))
      assert_equal({ 'X' => 0, 'Y' => 0, 'P0' => 'RNF', 'P1' => 'RNI' }, mesh.fetch('XP0_0'))
      assert_equal({ 'X' => 1, 'Y' => 0, 'P0' => 'HNF', 'P1' => 'NONE' }, mesh.fetch('XP1_0'))
      assert_equal({ 'X' => 0, 'Y' => 1, 'P0' => 'HNI', 'P1' => 'NONE' }, mesh.fetch('XP0_1'))
      assert_equal({ 'X' => 1, 'Y' => 1, 'P0' => 'SNF', 'P1' => 'NONE' }, mesh.fetch('XP1_1'))
      assert File.file?(File.join(output, 'mesh_wrapper_2x2.sv'))
      assert File.file?(File.join(output, 'tools/mesh_generator/chi_xp_node.sv'))
      assert File.file?(File.join(output, 'rtl/misc/chi_xp_channel.v'))
      assert File.file?(File.join(output, 'rtl/src/rni/rni.v'))
      assert File.file?(File.join(output, 'rtl/src/hnf/hnf.v'))
      assert File.file?(File.join(output, 'rtl/src/hni/hni.v'))
      assert File.file?(File.join(output, 'rtl/src/snf/snf.v'))
      refute File.directory?(File.join(output, 'rtl/src/rnf'))
      assert File.file?(File.join(output, 'LICENSE'))
      assert_includes File.read(File.join(output, 'opennoc_filelist.f')), File.join(output, 'mesh_wrapper_2x2.sv')
      assert_includes File.read(File.join(output, 'verify.sh')), '-GREQ_FLIT_WIDTH=128'
      manifest = JSON.parse(File.read(File.join(output, 'manifest.json')))
      assert_equal 'finepaper.opennoc', manifest.fetch('ipcore')
      assert_equal 'mesh', manifest.fetch('topology')
      assert_equal 2, manifest.fetch('rows')
      assert_equal 2, manifest.fetch('cols')
      assert_equal 'mesh_wrapper_2x2.sv', manifest.fetch('wrapper')
      assert_equal 'external', manifest.fetch('agents').find { |agent| agent.fetch('type') == 'OpenNoCRNF' }.fetch('rtl')
    end
  end

  def test_drc_rejects_unconnected_agent
    Dir.mktmpdir do |dir|
      graph = JSON.parse(File.read(File.expand_path('../examples/mesh_2x2.json', __dir__)))
      graph['connections'].reject! { |connection| connection.fetch('source').fetch('module') == 'snf_0' }
      input = write_json(dir, 'unconnected.json', graph)

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'OpenNoCSNF snf_0 must connect to exactly one XP slot'
    end
  end

  def test_drc_rejects_missing_mesh_link
    Dir.mktmpdir do |dir|
      graph = JSON.parse(File.read(File.expand_path('../examples/mesh_2x2.json', __dir__)))
      graph['connections'].reject! { |connection| connection.fetch('id') == 'XP0_0_east_to_XP1_0_west' }
      input = write_json(dir, 'missing_link.json', graph)

      _stdout, stderr, status = run_drc(input)

      refute status.success?
      assert_includes stderr, 'missing mesh link XP0_0 east XP1_0'
    end
  end

  def test_generator_rejects_missing_vendor
    Dir.mktmpdir do |dir|
      output = File.join(dir, 'out')

      _stdout, stderr, status = run_generator(File.expand_path('../examples/mesh_2x2.json', __dir__), output, File.join(dir, 'missing'))

      refute status.success?
      assert_includes stderr, 'OpenNoC vendor source is missing or incomplete'
    end
  end

  private

  def run_generator(input, output, vendor)
    Open3.capture3(RbConfig.ruby, GENERATOR, '-i', input, '-o', output, '--vendor', vendor)
  end

  def run_drc(input)
    Open3.capture3(RbConfig.ruby, DRC, '-i', input)
  end

  def write_json(dir, name, data)
    path = File.join(dir, name)
    File.write(path, JSON.pretty_generate(data))
    path
  end

  def make_fake_vendor(root)
    required_files.each do |relative|
      path = File.join(root, relative)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, fake_file_content(relative))
    end
    mesh_dir = File.join(root, 'tools/mesh_generator')
    FileUtils.mkdir_p(File.join(mesh_dir, 'template'))
    File.write(File.join(mesh_dir, 'mesh_gen.py'), fake_mesh_generator_script)
    File.write(File.join(mesh_dir, 'template/mesh_wrapper.j2'), 'fake template')
    FileUtils.chmod(0o755, File.join(mesh_dir, 'mesh_gen.py'))
  end

  def required_files
    [
      'LICENSE',
      'tools/mesh_generator/chi_xp_node.sv',
      'rtl/misc/chi_xp_channel.v',
      'rtl/misc/sync_fifo.v',
      'rtl/include/chie_defines.v',
      'rtl/include/rni_param.v',
      'rtl/include/hnf_param.v',
      'rtl/include/hni_param.v',
      'rtl/include/snf_param.v',
      'rtl/src/rni/rni.v',
      'rtl/src/hnf/hnf.v',
      'rtl/src/hni/hni.v',
      'rtl/src/snf/snf.v'
    ]
  end

  def fake_file_content(relative)
    relative == 'LICENSE' ? "Mulan PSL v2\n" : "// #{relative}\n"
  end

  def fake_mesh_generator_script
    <<~PY
      #!/usr/bin/env python3
      import json
      import sys
      args = sys.argv
      config = args[args.index('-f') + 1]
      data = json.load(open(config))
      xmax = max(node['X'] for node in data.values())
      ymax = max(node['Y'] for node in data.values())
      open(f"mesh_wrapper_{xmax + 1}x{ymax + 1}.sv", "w").write(f"module mesh_wrapper_{xmax + 1}x{ymax + 1}; endmodule\\n")
    PY
  end
end
