$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'fileutils'
require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'spec_generator'

class SpecGeneratorTest < Minitest::Test
  def stale_runtime_manifest_file_name
    SpecGenerator.stale_runtime_manifest_file_name
  end

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
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.noc', stale_runtime_manifest_file_name))

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
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.ravenoc', stale_runtime_manifest_file_name))

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
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.opennoc', stale_runtime_manifest_file_name))

      modules_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.opennoc/modules.xml'))
      assert_includes modules_xml, '<module name="OpenNoCXP" palette_label="OpenNoC XP" graph_group="xps"'
      assert_includes modules_xml, '<module name="OpenNoCRNF" palette_label="RNF" graph_group="endpoints"'
      assert_includes modules_xml, '<module name="OpenNoCRNI" palette_label="RNI" graph_group="endpoints"'
      assert_includes modules_xml, '<module name="OpenNoCHNF" palette_label="HNF" graph_group="endpoints"'
      assert_includes modules_xml, '<module name="OpenNoCHNI" palette_label="HNI" graph_group="endpoints"'
      assert_includes modules_xml, '<module name="OpenNoCSNF" palette_label="SNF" graph_group="endpoints"'
      assert_includes modules_xml, '<interface id="p0" label="P0" bus="opennoc_chi_attachment" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="endpoint_attachment">'
      assert_includes modules_xml, '<interface id="chi" label="CHI" bus="opennoc_chi_attachment" role="initiator" connects_to="target" match="" cardinality="one" autocomplete_group="endpoint_attachment">'

      %w[OpenNoCXP OpenNoCRNF OpenNoCRNI OpenNoCHNF OpenNoCHNI OpenNoCSNF].each do |name|
        assert File.file?(File.join(dir, "generated/ipcores/finepaper.opennoc/graphics/#{name}.xml")),
               "#{name} graphics should be generated"
      end
      xp_graphics = File.read(File.join(dir, 'generated/ipcores/finepaper.opennoc/graphics/OpenNoCXP.xml'))
      assert_includes xp_graphics, '<graphics layout="mesh_router"'
      assert_includes xp_graphics, 'supports_collapse="true"'
      rni_graphics = File.read(File.join(dir, 'generated/ipcores/finepaper.opennoc/graphics/OpenNoCRNI.xml'))
      assert_includes rni_graphics, '<graphics layout="endpoint"'
    end
  end

  def test_generation_removes_stale_plugin_json_from_existing_bundle_dir
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir)
      bundle_dir = File.join(dir, 'generated/ipcores/finepaper.ravenoc')
      FileUtils.mkdir_p(bundle_dir)
      File.write(File.join(bundle_dir, stale_runtime_manifest_file_name), '{"stale":true}')

      SpecGenerator.generate_ipcore(
        ipcore_path: File.join(dir, 'ipcores/ravenoc/ipcore.yml'),
        views_dir: File.join(dir, 'ipcores/ravenoc/views'),
        runtime_bundle_dir: bundle_dir
      )

      refute File.exist?(File.join(bundle_dir, stale_runtime_manifest_file_name))
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

  def test_rejects_yaml_anchors_aliases_and_merge_keys
    Dir.mktmpdir do |dir|
      yaml = minimal_ipcore_yaml.sub(
        "runtime:\n",
        "shared_runtime: &runtime_defaults\n  command: ruby\n  input_format: ipcore_graph_v1\n  args: []\nruntime:\n"
      ).sub(
        "    command: ruby\n    input_format: ipcore_graph_v1\n    args: []\n",
        "    <<: *runtime_defaults\n"
      )
      write_file(dir, 'ipcore.yml', yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        parse_minimal_ipcore(dir)
      end

      assert_match(/YAML anchors and aliases are not allowed/, error.message)
    end
  end

  def test_rejects_duplicate_yaml_keys
    Dir.mktmpdir do |dir|
      yaml = minimal_ipcore_yaml.sub("name: Minimal NoC\n", "name: Minimal NoC\nname: Duplicate NoC\n")
      write_file(dir, 'ipcore.yml', yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        parse_minimal_ipcore(dir)
      end

      assert_match(/Duplicate YAML key/, error.message)
    end
  end

  def test_rejects_normalized_duplicate_yaml_keys
    duplicate_cases = [
      "true: first\nTrue: second\n",
      "1: first\n01: second\n"
    ]

    duplicate_cases.each do |yaml|
      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator::ConstrainedYamlLoader.new(yaml).load
      end

      assert_match(/Duplicate YAML key/, error.message)
    end
  end

  def test_rejects_multi_document_yaml
    Dir.mktmpdir do |dir|
      write_file(dir, 'ipcore.yml', "#{minimal_ipcore_yaml}\n---\nschema: finepaper.ipcore.v1\n")

      error = assert_raises(SpecGenerator::SpecError) do
        parse_minimal_ipcore(dir)
      end

      assert_match(/YAML multi-document streams are not allowed/, error.message)
    end
  end

  def test_rejects_implicit_timestamp_fields
    Dir.mktmpdir do |dir|
      yaml = minimal_ipcore_yaml.sub("version: '1.0'\n", "version: 2026-05-14\n")
      write_file(dir, 'ipcore.yml', yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        parse_minimal_ipcore(dir)
      end

      assert_match(/Implicit timestamp values are not allowed/, error.message)
    end
  end

  def test_rejects_implicit_timestamp_keys
    error = assert_raises(SpecGenerator::SpecError) do
      SpecGenerator::ConstrainedYamlLoader.new("2026-05-14: release\n").load
    end

    assert_match(/Implicit timestamp values are not allowed/, error.message)
  end

  def test_rejects_yaml_custom_tags
    Dir.mktmpdir do |dir|
      yaml = minimal_ipcore_yaml.sub('name: Minimal NoC', 'name: !ipcraft.example Minimal NoC')
      write_file(dir, 'ipcore.yml', yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        parse_minimal_ipcore(dir)
      end

      assert_match(/YAML custom tags are not allowed/, error.message)
    end
  end

  def test_allows_block_scalar_text_that_looks_like_restricted_yaml_constructs
    data = SpecGenerator::ConstrainedYamlLoader.new(<<~YAML).load
      notes: |
        ---
        &name
        !tag
    YAML

    assert_equal "---\n&name\n!tag\n", data.fetch('notes')
  end

  def test_stringifies_numeric_version_fields
    data = SpecGenerator::ConstrainedYamlLoader.new(<<~YAML).load
      version: 1.0
      nested:
        - version: 2
    YAML

    assert_equal '1.0', data.fetch('version')
    assert_equal '2', data.fetch('nested').first.fetch('version')
  end

  def test_builds_ipcraft_manifest_with_interfaces_and_connection_classes
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)

      manifest_path = SpecGenerator.build_ipcraft_manifest(
        ipcore_path: File.join(package_root, 'ipcore.yml'),
        package_root: package_root
      )

      assert_equal File.join(package_root, 'ipcraft.json'), manifest_path
      manifest = JSON.parse(File.read(manifest_path))
      assert_equal 'ipcraft.manifest.v1', manifest.fetch('schema')
      assert_equal 'org.example.opennoc', manifest.fetch('id')
      assert_equal({ 'enabled' => true }, manifest.fetch('extensions').fetch('noc.v1').slice('enabled'))
      assert_equal [
        { 'id' => 'chi_node_interface', 'roles' => %w[node interconnect], 'symmetric' => false }
      ], manifest.fetch('connection_classes').map { |item| item.slice('id', 'roles', 'symmetric') }

      xp = manifest.fetch('modules').find { |mod| mod.fetch('id') == 'xp' }
      refute_nil xp
      assert_equal [
        {
          'id' => 'rnf0',
          'modes' => ['chi_interconnect'],
          'accepts' => [{ 'class' => 'chi_node_interface', 'role' => 'interconnect' }]
        }
      ], xp.fetch('interfaces').map { |item| item.slice('id', 'modes', 'accepts') }
      assert_equal(
        {
          'validate' => { 'executable' => 'tools/validate', 'input_schema' => 'ipcraft.noc.project.v1' },
          'generate' => { 'executable' => 'tools/generate', 'input_schema' => 'ipcraft.noc.project.v1' }
        },
        manifest.fetch('commands')
      )
    end
  end

  def test_requires_command_input_schema
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub("    input_schema: ipcraft.noc.project.v1\n\n  generate:", "\n  generate:")
      )

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.build_ipcraft_manifest(
          ipcore_path: File.join(package_root, 'ipcore.yml'),
          package_root: package_root
        )
      end

      assert_match(/commands\.validate\.input_schema is required/, error.message)
    end
  end

  def test_rejects_interface_mode_without_ipxact_mapping
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub('modes: [chi_interconnect]', 'modes: [chi_unknown]')
      )

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.build_ipcraft_manifest(
          ipcore_path: File.join(package_root, 'ipcore.yml'),
          package_root: package_root
        )
      end

      assert_match(/xp\.rnf0 mode chi_unknown has no IP-XACT mapping/, error.message)
    end
  end

  def test_rejects_invalid_explicit_ipxact_mode_mapping
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml
             .sub('modes: [chi_interconnect]', 'modes: [definitely_not_ipxact]')
             .sub(
               "        ipxact:\n          bus_interface: rnf0\n",
               "        ipxact:\n          bus_interface: rnf0\n          mode: definitely_not_ipxact\n"
             )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/xp\.rnf0 ipxact\.mode is invalid/, error.message)
    end
  end

  def test_rejects_empty_explicit_ipxact_mode_mapping
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml
             .sub('modes: [chi_interconnect]', 'modes: [custom_mode]')
             .sub(
               "        ipxact:\n          bus_interface: rnf0\n",
               "        ipxact:\n          bus_interface: rnf0\n          modes:\n            custom_mode: {}\n"
             )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/xp\.rnf0 ipxact\.modes\.custom_mode has no IP-XACT mapping/, error.message)
    end
  end

  def test_rejects_extension_mode_mapping_to_invalid_ipxact_mode
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml
             .sub(
               "    enabled: true\n",
               "    enabled: true\n    modes:\n      custom_mode:\n        ipxact:\n          mode: definitely_not_ipxact\n"
             )
             .sub('modes: [chi_interconnect]', 'modes: [custom_mode]')
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/extension noc\.v1 mode custom_mode ipxact\.mode is invalid/, error.message)
    end
  end

  def test_rejects_noc_extension_modes_that_are_not_a_map
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub("    enabled: true\n", "    enabled: true\n    modes: []\n")
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/extension noc\.v1\.modes must be a map/, error.message)
    end
  end

  def test_rejects_arbitrary_connection_class_ipxact_mapping
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "    symmetric: false\n",
        "    symmetric: false\n    ipxact:\n      arbitrary: data\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/connection class chi_node_interface ipxact field arbitrary is not recognized/, error.message)
    end
  end

  def test_rejects_empty_connection_class_ipxact_mapping
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "    symmetric: false\n",
        "    symmetric: false\n    ipxact: {}\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/connection class chi_node_interface ipxact cannot be empty/, error.message)
    end
  end

  def test_expands_noc_chi_extension_modes
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)

      SpecGenerator.build_ipcraft_manifest(
        ipcore_path: File.join(package_root, 'ipcore.yml'),
        package_root: package_root
      )

      manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))
      modes = manifest.fetch('extensions').fetch('noc.v1').fetch('modes')
      assert_equal 'target', modes.fetch('chi_interconnect').fetch('ipxact').fetch('mode')
      assert_equal 'initiator', modes.fetch('chi_requester_node').fetch('ipxact').fetch('mode')
    end
  end

  def test_does_not_write_generated_ipcore_runtime_bundle
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)

      SpecGenerator.build_ipcraft_manifest(
        ipcore_path: File.join(package_root, 'ipcore.yml'),
        package_root: package_root
      )

      assert File.file?(File.join(package_root, 'ipcraft.json'))
      refute File.exist?(File.join(dir, 'generated/ipcores/org.example.opennoc/ipcore-runtime.json'))
      refute File.exist?(File.join(dir, 'generated/ipcores/org.example.opennoc/modules.xml'))
      refute File.exist?(File.join(dir, 'generated/ipcores/org.example.opennoc/graphics'))
    end
  end

  def test_rejects_view_path_outside_package_root
    Dir.mktmpdir do |dir|
      write_file(dir, 'ipcores/outside.xml', ipcraft_xp_view_xml)
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub('file: views/xp.xml', 'file: ../outside.xml')
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/view xp file escapes package root/, error.message)
    end
  end

  def test_rejects_absolute_view_path
    Dir.mktmpdir do |dir|
      outside_path = write_file(dir, 'outside.xml', ipcraft_xp_view_xml)
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub('file: views/xp.xml', "file: #{outside_path}")
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/view xp file must be package-local/, error.message)
    end
  end

  def test_rejects_command_executable_path_outside_package_root
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub('executable: tools/validate', 'executable: ../bin/validate')
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/commands\.validate\.executable escapes package root/, error.message)
    end
  end

  def test_rejects_absolute_command_executable_path
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub('executable: tools/validate', 'executable: /bin/validate')
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/commands\.validate\.executable must be package-local/, error.message)
    end
  end

  def test_rejects_plugin_library_path_outside_package_root
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "version: '1.0.0'\n",
          "version: '1.0.0'\nplugin:\n  library: ../lib/plugin.rb\n"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/plugin\.library escapes package root/, error.message)
    end
  end

  def test_rejects_absolute_plugin_library_path
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "version: '1.0.0'\n",
          "version: '1.0.0'\nplugin:\n  library: /opt/ipcraft/plugin.rb\n"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/plugin\.library must be package-local/, error.message)
    end
  end

  def test_rejects_ipxact_root_path_outside_package_root
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "ipxact:\n  generated: true\n",
          "ipxact:\n  root: ../ipxact\n  generated: true\n"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/ipxact\.root escapes package root/, error.message)
    end
  end

  def test_rejects_absolute_ipxact_root_path
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "ipxact:\n  generated: true\n",
          "ipxact:\n  root: /opt/ipcraft\n  generated: true\n"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/ipxact\.root must be package-local/, error.message)
    end
  end

  def test_rejects_duplicate_connection_class_ids
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "connection_classes:\n  - id: chi_node_interface\n    roles: [node, interconnect]\n    symmetric: false\n",
        "connection_classes:\n  - id: chi_node_interface\n    roles: [node, interconnect]\n    symmetric: false\n  - id: chi_node_interface\n    roles: [node, interconnect]\n    symmetric: false\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/Duplicate connection class id: chi_node_interface/, error.message)
    end
  end

  def test_rejects_duplicate_module_ids
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "views:\n",
        "  - id: xp\n    name: Duplicate XP\n    interfaces: []\nviews:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/Duplicate module id: xp/, error.message)
    end
  end

  def test_rejects_duplicate_interface_ids_within_module
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "views:\n",
        "      - id: rnf0\n        modes: [chi_interconnect]\n        accepts:\n          - class: chi_node_interface\n            role: interconnect\n        multi_connection: false\n        ipxact:\n          bus_interface: rnf0_duplicate\nviews:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/Duplicate module xp interface id: rnf0/, error.message)
    end
  end

  def test_rejects_duplicate_view_module_entries
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "topologies:\n",
        "  - module: xp\n    file: views/xp.xml\ntopologies:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/Duplicate view module: xp/, error.message)
    end
  end

  def test_rejects_duplicate_topology_ids
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "  - id: mesh\n    kind: mesh\n    module: xp\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/Duplicate topology id: mesh/, error.message)
    end
  end

  def test_cli_checks_and_builds_ipcraft_manifest
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)
      ipcore_path = File.join(package_root, 'ipcore.yml')

      check_stdout, check_stderr, check_status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        'check',
        '--ipcore',
        ipcore_path
      )
      assert check_status.success?, check_stderr
      assert_includes check_stdout, "Checked ipcraft package source: #{ipcore_path}"

      build_stdout, build_stderr, build_status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        'build',
        '--ipcore',
        ipcore_path,
        '--package-root',
        package_root
      )
      assert build_status.success?, build_stderr
      assert_includes build_stdout, "Built ipcraft manifest: #{File.join(package_root, 'ipcraft.json')}"
      assert File.file?(File.join(package_root, 'ipcraft.json'))
    end
  end

  def test_cli_rejects_unknown_positional_command
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        'chek',
        '--ipcore',
        File.join(package_root, 'ipcore.yml')
      )

      refute status.success?
      assert_empty stdout
      assert_includes stderr, 'error: unknown command: chek'
      refute_includes stderr, '--runtime-bundle is required'
    end
  end

  def test_cli_rejects_unknown_positional_arg_after_options
    Dir.mktmpdir do |dir|
      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        '--ipcore',
        File.join(dir, 'nonexistent.yml'),
        'chek'
      )

      refute status.success?
      assert_empty stdout
      assert_includes stderr, 'error: unknown command: chek'
      refute_includes stderr, '--runtime-bundle is required'
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
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.ravenoc', stale_runtime_manifest_file_name))
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
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.noc', stale_runtime_manifest_file_name))
      refute File.exist?(File.join(dir, 'generated/ipcores/finepaper.ravenoc', stale_runtime_manifest_file_name))
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

  def test_check_repository_generated_outputs_reports_stale_generated_plugin_manifest
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      File.write(File.join(dir, 'generated/ipcores/finepaper.noc', stale_runtime_manifest_file_name), '{"stale":true}')

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      stale_manifest_pattern = Regexp.escape(stale_runtime_manifest_file_name)
      assert_match(%r{generated/ipcores/finepaper\.noc/#{stale_manifest_pattern}}, error.message)
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

  def write_ipcraft_package_source(root, yaml: ipcraft_package_yaml, xp_view: ipcraft_xp_view_xml)
    package_root = File.join(root, 'ipcores/opennoc')
    write_file(root, 'ipcores/opennoc/ipcore.yml', yaml)
    write_file(root, 'ipcores/opennoc/views/xp.xml', xp_view)
    package_root
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

  def build_ipcraft_manifest(package_root)
    SpecGenerator.build_ipcraft_manifest(
      ipcore_path: File.join(package_root, 'ipcore.yml'),
      package_root: package_root
    )
  end

  def parse_minimal_ipcore(root)
    SpecGenerator.generate_ipcore(
      ipcore_path: File.join(root, 'ipcore.yml'),
      views_dir: File.join(root, 'views'),
      runtime_bundle_dir: File.join(root, 'generated/ipcores/minimal')
    )
  end

  def write_file(root, relative_path, content)
    path = File.join(root, relative_path)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, content)
    path
  end

  def minimal_ipcore_yaml
    <<~YAML
      schema: finepaper.ipcore.v1
      id: finepaper.minimal
      name: Minimal NoC
      version: '1.0'
      kind: noc
      runtime:
        generator:
          command: ruby
          input_format: ipcore_graph_v1
          args: []
        drc:
          command: ruby
          input_format: ipcore_graph_v1
          args: []
      modules: {}
    YAML
  end

  def ipcraft_package_yaml
    <<~YAML
      schema: ipcraft.package.v1
      id: org.example.opennoc
      name: OpenNoC
      version: '1.0.0'
      extensions:
        noc.v1:
          enabled: true
      ipxact:
        generated: true
      parameters:
        req_flit_width: { type: int, default: 128, min: 1, max: 1024, label: REQ flit width }
      connection_classes:
        - id: chi_node_interface
          roles: [node, interconnect]
          symmetric: false
      modules:
        - id: xp
          name: XP
          graph_role: host
          parameters:
            x: { type: int, default: 0, configurable: false }
          interfaces:
            - id: rnf0
              modes: [chi_interconnect]
              accepts:
                - class: chi_node_interface
                  role: interconnect
              multi_connection: false
              ipxact:
                bus_interface: rnf0
      views:
        - module: xp
          file: views/xp.xml
      topologies:
        - id: mesh
          kind: mesh
          module: xp
      commands:
        validate:
          executable: tools/validate
          input_schema: ipcraft.noc.project.v1

        generate:
          executable: tools/generate
          input_schema: ipcraft.noc.project.v1
    YAML
  end

  def ipcraft_xp_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="xp">
        <graphics layout="mesh_router">
          <expanded min_width="136" height="116" caption_left="30" caption_top="6" />
        </graphics>
        <anchors>
          <anchor ref="rnf0" x="0" y="30" normal_x="-1" normal_y="0" label="RNF0" label_x="30" label_y="30" />
        </anchors>
      </module-view>
    XML
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
