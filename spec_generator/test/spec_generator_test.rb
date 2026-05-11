$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'fileutils'
require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'spec_generator'

class SpecGeneratorTest < Minitest::Test
  def test_generates_finepaper_noc_ipcore_runtime_bundle_and_ruby_models
    Dir.mktmpdir do |dir|
      write_finepaper_noc_source(dir)

      SpecGenerator.generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/finepaper-noc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/finepaper-noc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.noc'),
        ruby_model_dir: File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model')
      )

      runtime_json = JSON.parse(File.read(File.join(dir, 'generated/ipcores/finepaper.noc/ipcore-runtime.json')))
      assert_equal 'finepaper.noc', runtime_json.fetch('id')
      assert_equal 'NoC', runtime_json.fetch('name')
      assert_equal '1.0', runtime_json.fetch('version')
      assert_equal 'noc', runtime_json.fetch('kind')
      assert_equal '../../../ipcores/finepaper-noc', runtime_json.fetch('source_root')
      assert_equal 'modules.xml', runtime_json.fetch('modules')
      assert_equal 'graphics', runtime_json.fetch('graphics')
      assert_equal 'ruby', runtime_json.fetch('generator').fetch('command')
      assert_equal 'ipcore_graph_v1', runtime_json.fetch('generator').fetch('input_format')
      assert_equal 'generator/bin/generate', runtime_json.fetch('generator').fetch('args').first
      assert_equal 'ruby', runtime_json.fetch('drc').fetch('command')
      assert_equal 'generator/bin/drc', runtime_json.fetch('drc').fetch('args').first
      assert_equal 2, runtime_json.fetch('topology_presets').size
      refute runtime_json.key?('native')
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.noc/plugin.json'))

      modules_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.noc/modules.xml'))
      assert_includes modules_xml, '<bus name="ni_link"'
      assert_includes modules_xml, '<match field="protocol" />'
      assert_includes modules_xml, '<interface id="local0" label="Local 0" bus="ni_link" role="target" connects_to="initiator" match="protocol,data_width" cardinality="one" autocomplete_group="endpoint_attachment">'
      assert_includes modules_xml, '<parameter name="mesh_col" type="int" default="0" description="Logical mesh column." configurable="false" />'
      assert_includes modules_xml, '<choice value="chi" label="chi" />'
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.noc/graphics/XP.xml'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.noc/graphics/Endpoint.xml'))

      xp_model = File.read(File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model/xp.rb'))
      assert_includes xp_model, 'attr_reader :id, :x, :y, :endpoints, :config'
      assert_includes xp_model, "routing_algorithm: { type: :string, default: 'xy', enum: ['xy', 'yx'] }"

      endpoint_model = File.read(File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model/endpoint.rb'))
      assert_includes endpoint_model, 'attr_reader :id, :type, :protocol, :data_width, :config'
      assert_includes endpoint_model, 'buffer_depth: { type: :integer, default: 16 }'
    end
  end

  def test_generates_ravenoc_ipcore_runtime_bundle
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir)

      SpecGenerator.generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/ravenoc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/ravenoc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.ravenoc')
      )

      runtime_json = JSON.parse(File.read(File.join(dir, 'generated/ipcores/finepaper.ravenoc/ipcore-runtime.json')))
      assert_equal 'finepaper.ravenoc', runtime_json.fetch('id')
      assert_equal 'RaveNoC', runtime_json.fetch('name')
      assert_equal '1.0', runtime_json.fetch('version')
      assert_equal 'noc', runtime_json.fetch('kind')
      assert_equal '../../../ipcores/ravenoc', runtime_json.fetch('source_root')
      assert_equal 11, runtime_json.fetch('instance_parameters').size
      assert_equal 32, runtime_json.fetch('instance_parameters').fetch('flit_data_width').fetch('default')
      assert_equal({ 'xy' => 'XY', 'yx' => 'YX' }, runtime_json.fetch('instance_parameters').fetch('routing_algorithm').fetch('labels'))
      assert_equal 'generator/bin/generate', runtime_json.fetch('generator').fetch('args').first
      assert_equal 'generator/bin/drc', runtime_json.fetch('drc').fetch('args').first
      assert_equal 1, runtime_json.fetch('topology_presets').size
      refute runtime_json.key?('native')
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/plugin.json'))

      modules_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.ravenoc/modules.xml'))
      refute_includes modules_xml, '<buses>'
      assert_includes modules_xml, '<module name="RaveTile" palette_label="Rave Tile" graph_group="xps"'
      assert_includes modules_xml, '<module name="RaveEndpoint" palette_label="Rave Endpoint" graph_group="endpoints"'
      assert_includes modules_xml, '<interface id="north" label="North" bus="ravenoc_router_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">'
      assert_includes modules_xml, '<port id="east" direction="inout" type="bus" bus_type="ravenoc_router_link" role="router" name="East" description="East RaveNoC router link" interface="east" />'
      assert_includes modules_xml, '<parameter name="mesh_row" type="int" default="0" description="Logical RaveNoC mesh row." configurable="false" />'
      refute_includes modules_xml, 'name="flit_data_width"'

      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/graphics/RaveTile.xml'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/graphics/RaveEndpoint.xml'))
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/pages.json'))
    end
  end

  def test_generates_opennoc_ipcore_runtime_bundle
    Dir.mktmpdir do |dir|
      write_opennoc_source(dir)

      SpecGenerator.generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/opennoc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/opennoc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.opennoc')
      )

      runtime_json = JSON.parse(File.read(File.join(dir, 'generated/ipcores/finepaper.opennoc/ipcore-runtime.json')))
      assert_equal 'finepaper.opennoc', runtime_json.fetch('id')
      assert_equal 'OpenNoC', runtime_json.fetch('name')
      assert_equal '1.0', runtime_json.fetch('version')
      assert_equal 'noc', runtime_json.fetch('kind')
      assert_equal '../../../ipcores/opennoc', runtime_json.fetch('source_root')
      assert_equal 4, runtime_json.fetch('instance_parameters').size
      assert_equal 128, runtime_json.fetch('instance_parameters').fetch('req_flit_width').fetch('default')
      assert_equal 'generator/bin/generate', runtime_json.fetch('generator').fetch('args').first
      assert_equal 'generator/bin/drc', runtime_json.fetch('drc').fetch('args').first
      assert_equal 1, runtime_json.fetch('topology_presets').size
      assert_equal 'OpenNoCXP', runtime_json.fetch('topology_presets').first.fetch('router_module')
      refute runtime_json.key?('native')
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.opennoc/plugin.json'))

      modules_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.opennoc/modules.xml'))
      assert_includes modules_xml, '<module name="OpenNoCXP" palette_label="OpenNoC XP" graph_group="xps"'
      assert_includes modules_xml, '<module name="OpenNoCRNF" palette_label="RNF" graph_group="opennoc_agents"'
      assert_includes modules_xml, '<module name="OpenNoCRNI" palette_label="RNI" graph_group="opennoc_agents"'
      assert_includes modules_xml, '<module name="OpenNoCHNF" palette_label="HNF" graph_group="opennoc_agents"'
      assert_includes modules_xml, '<module name="OpenNoCHNI" palette_label="HNI" graph_group="opennoc_agents"'
      assert_includes modules_xml, '<module name="OpenNoCSNF" palette_label="SNF" graph_group="opennoc_agents"'
      assert_includes modules_xml, '<interface id="p0" label="P0" bus="opennoc_chi_attachment" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="endpoint_attachment">'
      assert_includes modules_xml, '<interface id="chi" label="CHI" bus="opennoc_chi_attachment" role="initiator" connects_to="target" match="" cardinality="one" autocomplete_group="endpoint_attachment">'

      %w[OpenNoCXP OpenNoCRNF OpenNoCRNI OpenNoCHNF OpenNoCHNI OpenNoCSNF].each do |name|
        assert File.file?(File.join(dir, "generated/ipcores/finepaper.opennoc/graphics/#{name}.xml")),
               "#{name} graphics should be generated"
      end
    end
  end

  def test_generation_removes_stale_plugin_json_from_existing_bundle_dir
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir)
      bundle_dir = File.join(dir, 'generated/ipcores/finepaper.ravenoc')
      FileUtils.mkdir_p(bundle_dir)
      File.write(File.join(bundle_dir, 'plugin.json'), '{"stale":true}')

      SpecGenerator.generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/ravenoc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/ravenoc/views'),
        runtime_bundle_dir: bundle_dir
      )

      refute File.exist?(File.join(bundle_dir, 'plugin.json'))
      assert File.file?(File.join(bundle_dir, 'ipcore-runtime.json'))
      assert File.file?(File.join(bundle_dir, 'modules.xml'))
    end
  end

  def test_generates_interface_anchor_bundle_for_renamed_noc_modules
    Dir.mktmpdir do |dir|
      write_file(dir, 'ipcores/renamed-noc/ipcore.yml', renamed_interface_anchor_ipcore_yaml)
      write_file(dir, 'ipcores/renamed-noc/views/RouterTile.xml', router_tile_view_xml)
      write_file(dir, 'ipcores/renamed-noc/views/NetworkPort.xml', network_port_view_xml)

      SpecGenerator.generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/renamed-noc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/renamed-noc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.renamed-noc'),
        ruby_model_dir: File.join(dir, 'ipcores/renamed-noc/generator/src/ruby/model')
      )

      modules_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.renamed-noc/modules.xml'))
      assert_includes modules_xml, '<module name="RouterTile"'
      assert_includes modules_xml, '<interface id="east" label="East" bus="router_link" role="initiator" connects_to="target" match="">'
      assert_includes modules_xml, '<port id="east" direction="output" type="bus" bus_type="router_link" role="router" name="East" description="East router interface" interface="east" />'
      refute_includes modules_xml, 'east_in'
      refute_includes modules_xml, 'east_out'

      graphics_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.renamed-noc/graphics/RouterTile.xml'))
      assert_includes graphics_xml, '<anchors>'
      assert_includes graphics_xml, '<anchor ref="east" x="136" y="58" normal_x="1" normal_y="0" label="East" label_x="112" label_y="58" />'
    end
  end

  def test_repository_ipcores_generate_runtime_interface_metadata
    Dir.mktmpdir do |dir|
      write_source_fixture_repo(dir)
      generate_fixture_runtime(dir)

      noc_modules_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.noc/modules.xml'))
      assert_includes noc_modules_xml, '<interface id="local0" label="Local 0" bus="ni_link" role="target" connects_to="initiator" match="protocol,data_width" cardinality="one" autocomplete_group="endpoint_attachment">'
      assert_includes noc_modules_xml, '<interface id="north" label="North" bus="router_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">'
      assert_includes noc_modules_xml, '<interface id="noc" label="NoC" bus="ni_link" role="initiator" connects_to="target" match="protocol,data_width" cardinality="one" autocomplete_group="endpoint_attachment">'

      ravenoc_modules_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.ravenoc/modules.xml'))
      assert_includes ravenoc_modules_xml, '<interface id="north" label="North" bus="ravenoc_router_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">'
      assert_includes ravenoc_modules_xml, '<interface id="local" label="Local" bus="ravenoc_endpoint_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="endpoint_attachment">'
      assert_includes ravenoc_modules_xml, '<interface id="noc" label="NoC" bus="ravenoc_endpoint_link" role="initiator" connects_to="target" match="" cardinality="one" autocomplete_group="endpoint_attachment">'
    end
  end

  def test_rejects_unknown_top_level_fields
    Dir.mktmpdir do |dir|
      yaml = finepaper_noc_ipcore_yaml.sub("kind: noc\n", "kind: noc\nextra: nope\n")
      write_finepaper_noc_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        generate_finepaper_noc(dir)
      end

      assert_match(/Unknown IP core top-level field: extra/, error.message)
    end
  end

  def test_rejects_interface_accepts_values_outside_bus_enum
    Dir.mktmpdir do |dir|
      yaml = finepaper_noc_ipcore_yaml.sub(
        "protocol: [axi4, chi]\n          data_width",
        "protocol: [axi4, wishbone]\n          data_width"
      )
      write_finepaper_noc_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        generate_finepaper_noc(dir)
      end

      assert_match(/XP.local0 accepts protocol value wishbone outside ni_link enum/, error.message)
    end
  end

  def test_rejects_bus_ipcore_interface_metadata_that_is_not_a_string
    Dir.mktmpdir do |dir|
      write_finepaper_noc_source(dir, yaml: finepaper_noc_ipcore_yaml.sub('cardinality: one', 'cardinality: 1'))

      error = assert_raises(SpecGenerator::SpecError) do
        generate_finepaper_noc(dir)
      end

      assert_match(/XP.local0 cardinality must be a string/, error.message)
    end
  end

  def test_rejects_ipcore_topology_rule_outside_known_values
    Dir.mktmpdir do |dir|
      write_finepaper_noc_source(dir, yaml: finepaper_noc_ipcore_yaml.sub('topology_rule: opposite_side', 'topology_rule: opposite-side'))

      error = assert_raises(SpecGenerator::SpecError) do
        generate_finepaper_noc(dir)
      end

      assert_match(/XP.north topology_rule is invalid/, error.message)
    end
  end

  def test_rejects_view_interface_refs_missing_from_spec
    Dir.mktmpdir do |dir|
      write_finepaper_noc_source(dir, xp_view: xp_view_xml.sub('ref="local3"', 'ref="local4"'))

      error = assert_raises(SpecGenerator::SpecError) do
        generate_finepaper_noc(dir)
      end

      assert_match(/view XP references unknown interface local4/, error.message)
    end
  end

  def test_rejects_ipcore_instance_parameter_without_default
    Dir.mktmpdir do |dir|
      yaml = ravenoc_ipcore_yaml.sub('default: 32, min: 8', 'min: 8')
      write_ravenoc_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        generate_ravenoc(dir)
      end

      assert_match(/instance parameter flit_data_width default is required/, error.message)
    end
  end

  def test_rejects_ipcore_kind_outside_noc
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir, yaml: ravenoc_ipcore_yaml.sub('kind: noc', 'kind: ip'))

      error = assert_raises(SpecGenerator::SpecError) do
        generate_ravenoc(dir)
      end

      assert_match(/kind must be noc/, error.message)
    end
  end

  def test_rejects_removed_extension_schema
    Dir.mktmpdir do |dir|
      legacy_schema = %w[finepaper extension v1].join('.')
      yaml = ravenoc_ipcore_yaml.sub('schema: finepaper.ipcore.v1', "schema: #{legacy_schema}")
      write_ravenoc_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        generate_ravenoc(dir)
      end

      assert_match(/schema must be finepaper\.ipcore\.v1/, error.message)
    end
  end

  def test_rejects_standalone_ipcore_interface_metadata_that_is_not_a_string
    Dir.mktmpdir do |dir|
      yaml = ravenoc_ipcore_yaml.sub('autocomplete_group: router_side', 'autocomplete_group: 42')
      write_ravenoc_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        generate_ravenoc(dir)
      end

      assert_match(/RaveTile.north autocomplete_group must be a string/, error.message)
    end
  end

  def test_rejects_ipcore_view_refs_missing_from_spec
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir, rave_tile_view: rave_tile_view_xml.sub('ref="east"', 'ref="debug"'))

      error = assert_raises(SpecGenerator::SpecError) do
        generate_ravenoc(dir)
      end

      assert_match(/view RaveTile references unknown interface debug/, error.message)
    end
  end

  def test_rejects_malformed_module_view_without_module_attribute
    Dir.mktmpdir do |dir|
      malformed = rave_tile_view_xml.sub(' module="RaveTile"', '')
      write_ravenoc_source(dir, rave_tile_view: malformed)

      error = assert_raises(SpecGenerator::SpecError) do
        generate_ravenoc(dir)
      end

      assert_match(/view RaveTile is missing module attribute/, error.message)
    end
  end

  def test_cli_generates_ipcore_bundle
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        '--ipcore', File.join(dir, 'ipcores/ravenoc/ipcore.yml'),
        '--views', File.join(dir, 'ipcores/ravenoc/views'),
        '--runtime-bundle', File.join(dir, 'generated/ipcores/finepaper.ravenoc')
      )

      assert status.success?, stderr
      assert_includes stdout, 'Generated IP core runtime bundle'
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/ipcore-runtime.json'))
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/plugin.json'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/modules.xml'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/graphics/RaveTile.xml'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/graphics/RaveEndpoint.xml'))
    end
  end

  def test_cli_default_generates_repository_ipcore_bundles
    Dir.mktmpdir do |dir|
      write_source_fixture_repo(dir)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        chdir: dir
      )

      assert status.success?, stderr
      assert_includes stdout, 'Generated repository IP core runtime bundles'
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.noc/ipcore-runtime.json'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.noc/modules.xml'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.noc/graphics/Endpoint.xml'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/ipcore-runtime.json'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/graphics/RaveTile.xml'))
      assert File.file?(File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model/xp.rb'))
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.noc/plugin.json'))
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.ravenoc/plugin.json'))
    end
  end

  def test_cli_check_passes_when_generated_runtime_artifacts_match_specs
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        '--check',
        chdir: dir
      )

      assert status.success?, stderr
      assert_includes stdout, 'Generated IP core runtime artifacts are up to date'
    end
  end

  def test_check_repository_generated_outputs_reports_noc_modules_drift
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      path = File.join(dir, 'generated/ipcores/finepaper.noc/modules.xml')
      File.write(path, File.read(path).sub('cardinality="one"', 'cardinality="many"'))

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(%r{generated/ipcores/finepaper\.noc/modules\.xml}, error.message)
    end
  end

  def test_check_repository_generated_outputs_reports_ravenoc_modules_drift
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      path = File.join(dir, 'generated/ipcores/finepaper.ravenoc/modules.xml')
      File.write(path, File.read(path).sub('cardinality="one"', 'cardinality="many"'))

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(%r{generated/ipcores/finepaper\.ravenoc/modules\.xml}, error.message)
    end
  end

  def test_check_repository_generated_outputs_reports_missing_committed_file
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      FileUtils.rm(File.join(dir, 'generated/ipcores/finepaper.noc/modules.xml'))

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(%r{missing committed: generated/ipcores/finepaper\.noc/modules\.xml}, error.message)
    end
  end

  def test_check_repository_generated_outputs_reports_stale_committed_file
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      File.write(File.join(dir, 'generated/ipcores/finepaper.noc/graphics/Stale.xml'), '<stale />')

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(%r{missing generated: generated/ipcores/finepaper\.noc/graphics/Stale\.xml}, error.message)
    end
  end

  def test_check_repository_generated_outputs_reports_stale_generated_model_file
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      File.write(File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model/stale.rb'), '# stale')

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(%r{missing generated: ipcores/finepaper-noc/generator/src/ruby/model/stale\.rb}, error.message)
    end
  end

  private

  def repo_path(*parts)
    File.expand_path(File.join('..', '..', *parts), __dir__)
  end

  def finepaper_noc_ipcore_yaml
    File.read(repo_path('ipcores/finepaper-noc/ipcore.yml'))
  end

  def ravenoc_ipcore_yaml
    File.read(repo_path('ipcores/ravenoc/ipcore.yml'))
  end

  def opennoc_ipcore_yaml
    File.read(repo_path('ipcores/opennoc/ipcore.yml'))
  end

  def opennoc_view_xml(name)
    File.read(repo_path("ipcores/opennoc/views/#{name}.xml"))
  end

  def write_source_fixture_repo(root)
    write_finepaper_noc_source(root)
    write_ravenoc_source(root)
    write_opennoc_source(root)
  end

  def build_generated_fixture_repo(root)
    write_source_fixture_repo(root)
    generate_fixture_runtime(root)
  end

  def generate_fixture_runtime(root)
    generate_finepaper_noc(root)
    generate_ravenoc(root)
    generate_opennoc(root)
  end

  def write_finepaper_noc_source(root, yaml: finepaper_noc_ipcore_yaml, xp_view: xp_view_xml, endpoint_view: endpoint_view_xml)
    write_file(root, 'ipcores/finepaper-noc/ipcore.yml', yaml)
    write_file(root, 'ipcores/finepaper-noc/views/XP.xml', xp_view)
    write_file(root, 'ipcores/finepaper-noc/views/Endpoint.xml', endpoint_view)
  end

  def write_ravenoc_source(root, yaml: ravenoc_ipcore_yaml, rave_tile_view: rave_tile_view_xml, rave_endpoint_view: rave_endpoint_view_xml)
    write_file(root, 'ipcores/ravenoc/ipcore.yml', yaml)
    write_file(root, 'ipcores/ravenoc/views/RaveTile.xml', rave_tile_view)
    write_file(root, 'ipcores/ravenoc/views/RaveEndpoint.xml', rave_endpoint_view)
  end

  def write_opennoc_source(root)
    write_file(root, 'ipcores/opennoc/ipcore.yml', opennoc_ipcore_yaml)
    %w[OpenNoCXP OpenNoCRNF OpenNoCRNI OpenNoCHNF OpenNoCHNI OpenNoCSNF].each do |name|
      write_file(root, "ipcores/opennoc/views/#{name}.xml", opennoc_view_xml(name))
    end
  end

  def generate_finepaper_noc(root)
    SpecGenerator.generate_ipcore(
      ipcore_path: File.join(root, 'ipcores/finepaper-noc/ipcore.yml'),
      views_dir: File.join(root, 'ipcores/finepaper-noc/views'),
      runtime_bundle_dir: File.join(root, 'generated/ipcores/finepaper.noc'),
      ruby_model_dir: File.join(root, 'ipcores/finepaper-noc/generator/src/ruby/model')
    )
  end

  def generate_ravenoc(root)
    SpecGenerator.generate_ipcore(
      ipcore_path: File.join(root, 'ipcores/ravenoc/ipcore.yml'),
      views_dir: File.join(root, 'ipcores/ravenoc/views'),
      runtime_bundle_dir: File.join(root, 'generated/ipcores/finepaper.ravenoc')
    )
  end

  def generate_opennoc(root)
    SpecGenerator.generate_ipcore(
      ipcore_path: File.join(root, 'ipcores/opennoc/ipcore.yml'),
      views_dir: File.join(root, 'ipcores/opennoc/views'),
      runtime_bundle_dir: File.join(root, 'generated/ipcores/finepaper.opennoc')
    )
  end

  def write_file(root, relative_path, content)
    path = File.join(root, relative_path)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, content)
    path
  end

  def renamed_interface_anchor_ipcore_yaml
    <<~YAML
      schema: finepaper.ipcore.v1
      id: finepaper.renamed-noc
      name: Renamed NoC
      version: '1.0'
      kind: noc
      runtime:
        generator:
          command: ruby
          input_format: ipcore_graph_v1
          args:
            - generator/bin/generate
        drc:
          command: ruby
          input_format: ipcore_graph_v1
          args:
            - generator/bin/drc
      buses:
        router_link:
          description: Router-to-router NoC link.
          compatibility:
            roles:
              initiator: [target]
              target: [initiator]
            match: []
          config: {}
          signals:
            - name: flit
              direction: initiator_to_target
              width: FLIT_WIDTH
        ni_link:
          description: Endpoint-to-router NoC interface.
          compatibility:
            roles:
              initiator: [target]
              target: [initiator]
            match: [protocol, data_width]
          config:
            protocol:
              type: string
              enum: [axi4, chi]
              default: axi4
              description: Interface protocol.
            data_width:
              type: int
              enum: [32, 64, 128]
              default: 64
              description: Data width in bits.
          signals:
            - name: flit
              direction: initiator_to_target
              width: FLIT_WIDTH
      modules:
        RouterTile:
          palette_label: Router Tile
          graph_group: xps
          description: Spec-named NoC router tile.
          identity:
            external_id_prefix: xp
            display_prefix: RT
            width: 2
            supports_mesh_coordinates: true
          capabilities:
            supports_collapse: true
          parameters:
            x: { type: int, default: 0, configurable: false, emit: attribute, description: Canvas X position. }
            y: { type: int, default: 0, configurable: false, emit: attribute, description: Canvas Y position. }
            display_name: { type: string, default: '', emit: editor, label: Display name, description: Name shown on the canvas. }
            external_id: { type: string, default: '', emit: editor, label: External ID, description: Framework-facing identifier. }
            routing_algorithm: { type: string, enum: [xy, yx], default: xy, emit: config, label: Routing algorithm, description: Router path selection strategy. }
          interfaces:
            east:
              label: East
              bus: router_link
              role: initiator
              port: { id: east, direction: output, type: bus, bus_type: router_link, role: router, name: East, description: East router interface }
            west:
              label: West
              bus: router_link
              role: target
              port: { id: west, direction: input, type: bus, bus_type: router_link, role: router, name: West, description: West router interface }
            local0:
              label: Local 0
              bus: ni_link
              role: target
              accepts: { protocol: [axi4, chi], data_width: [32, 64, 128] }
              port: { id: local0, direction: input, type: bus, bus_type: ni_link, role: attachment, name: Local 0, description: Local endpoint slot 0 }
        NetworkPort:
          palette_label: Network Port
          graph_group: endpoints
          description: Spec-named NoC endpoint.
          identity:
            external_id_prefix: ep
            display_prefix: NP
            width: 2
            supports_mesh_coordinates: false
          parameters:
            display_name: { type: string, default: '', emit: editor, label: Display name, description: Name shown on the canvas. }
            external_id: { type: string, default: '', emit: editor, label: External ID, description: Framework-facing identifier. }
            type: { type: string, enum: [master, slave], default: master, emit: attribute, label: Type, description: Endpoint traffic role. }
            protocol: { type: string, enum: [axi4, chi], default: axi4, emit: attribute, label: Protocol, description: Interface protocol. }
            data_width: { type: int, enum: [32, 64, 128], default: 64, emit: attribute, label: Data width, description: Bus width in bits. }
            buffer_depth: { type: int, default: 16, emit: config, label: Buffer depth, description: Ingress buffer depth. }
          interfaces:
            noc:
              label: NoC
              bus: ni_link
              role: initiator
              config:
                protocol: { parameter: protocol }
                data_width: { parameter: data_width }
              port: { id: noc, direction: output, type: bus, bus_type: ni_link, role: attachment, name: NoC, description: NoC attachment interface }
    YAML
  end

  def rave_tile_view_xml
    File.read(repo_path('ipcores/ravenoc/views/RaveTile.xml'))
  end

  def rave_endpoint_view_xml
    File.read(repo_path('ipcores/ravenoc/views/RaveEndpoint.xml'))
  end

  def xp_view_xml
    File.read(repo_path('ipcores/finepaper-noc/views/XP.xml'))
  end

  def endpoint_view_xml
    File.read(repo_path('ipcores/finepaper-noc/views/Endpoint.xml'))
  end

  def router_tile_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="RouterTile">
        <graphics layout="mesh_router" node_color="#7cb9e8" supports_collapse="true">
          <expanded min_width="136" height="116" caption_left="30" caption_top="6" port_inset="16" />
          <collapsed min_width="104" height="92" caption_left="30" caption_top="26" endpoint_inset="18" />
          <arrangement endpoint_offset_x="156" mesh_spacing_x="220" mesh_spacing_y="168" />
        </graphics>
        <anchors>
          <anchor ref="east" x="136" y="58" normal_x="1" normal_y="0" label="East" label_x="112" label_y="58" />
          <anchor ref="west" x="0" y="58" normal_x="-1" normal_y="0" label="West" label_x="24" label_y="58" />
          <anchor ref="local0" x="0" y="30" normal_x="-1" normal_y="0" label="Local 0" label_x="30" label_y="30" />
        </anchors>
      </module-view>
    XML
  end

  def network_port_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="NetworkPort">
        <graphics layout="endpoint" node_color="#d6f4b6">
          <expanded min_width="104" height="54" caption_left="8" caption_top="6" />
          <arrangement loose_endpoint_spacing_x="168" loose_endpoint_spacing_y="84" loose_endpoint_margin_y="116" />
        </graphics>
        <anchors>
          <anchor ref="noc" x="104" y="27" normal_x="1" normal_y="0" label="NoC" label_x="78" label_y="27" />
        </anchors>
      </module-view>
    XML
  end
end
