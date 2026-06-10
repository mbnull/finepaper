$LOAD_PATH.unshift File.expand_path('../lib', __dir__)

require 'fileutils'
require 'json'
require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'spec_generator'

class SpecGeneratorTest < Minitest::Test
  def test_rejects_unknown_top_level_fields
    Dir.mktmpdir do |dir|
      yaml = finepaper_noc_ipcore_yaml.sub("kind: noc\n", "kind: noc\nextra: nope\n")
      write_finepaper_noc_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        parse_finepaper_noc(dir)
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
        parse_finepaper_noc(dir)
      end

      assert_match(/XP.local0 accepts protocol value wishbone outside ni_link enum/, error.message)
    end
  end

  def test_rejects_bus_ipcore_interface_metadata_that_is_not_a_string
    Dir.mktmpdir do |dir|
      write_finepaper_noc_source(dir, yaml: finepaper_noc_ipcore_yaml.sub('cardinality: one', 'cardinality: 1'))

      error = assert_raises(SpecGenerator::SpecError) do
        parse_finepaper_noc(dir)
      end

      assert_match(/XP.local0 cardinality must be a string/, error.message)
    end
  end

  def test_rejects_ipcore_topology_rule_outside_known_values
    Dir.mktmpdir do |dir|
      write_finepaper_noc_source(dir, yaml: finepaper_noc_ipcore_yaml.sub('topology_rule: opposite_side', 'topology_rule: opposite-side'))

      error = assert_raises(SpecGenerator::SpecError) do
        parse_finepaper_noc(dir)
      end

      assert_match(/XP.north topology_rule is invalid/, error.message)
    end
  end

  def test_rejects_view_interface_refs_missing_from_spec
    Dir.mktmpdir do |dir|
      write_finepaper_noc_source(dir, xp_view: xp_view_xml.sub('ref="local3"', 'ref="local4"'))

      error = assert_raises(SpecGenerator::SpecError) do
        parse_finepaper_noc(dir)
      end

      assert_match(/view XP references unknown interface local4/, error.message)
    end
  end

  def test_rejects_ipcore_instance_parameter_without_default
    Dir.mktmpdir do |dir|
      yaml = ravenoc_ipcore_yaml.sub('default: 32, min: 8', 'min: 8')
      write_ravenoc_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        parse_ravenoc(dir)
      end

      assert_match(/instance parameter flit_data_width default is required/, error.message)
    end
  end

  def test_rejects_ipcore_kind_outside_noc
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir, yaml: ravenoc_ipcore_yaml.sub('kind: noc', 'kind: ip'))

      error = assert_raises(SpecGenerator::SpecError) do
        parse_ravenoc(dir)
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
        parse_ravenoc(dir)
      end

      assert_match(/schema must be finepaper\.ipcore\.v1/, error.message)
    end
  end

  def test_rejects_standalone_ipcore_interface_metadata_that_is_not_a_string
    Dir.mktmpdir do |dir|
      yaml = ravenoc_ipcore_yaml.sub('autocomplete_group: router_side', 'autocomplete_group: 42')
      write_ravenoc_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        parse_ravenoc(dir)
      end

      assert_match(/RaveTile.north autocomplete_group must be a string/, error.message)
    end
  end

  def test_rejects_ipcore_view_refs_missing_from_spec
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir, rave_tile_view: rave_tile_view_xml.sub('ref="east"', 'ref="debug"'))

      error = assert_raises(SpecGenerator::SpecError) do
        parse_ravenoc(dir)
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
      assert_equal 'ipcraft.package.v1', manifest.fetch('schema')
      assert_equal 'org.example.opennoc', manifest.fetch('id')
      editor = editor_manifest(manifest)
      assert_equal({ 'enabled' => true }, editor.fetch('extensions').fetch('noc.v1').slice('enabled'))
      assert_equal [
        { 'id' => 'chi_node_interface', 'roles' => %w[node interconnect], 'symmetric' => false }
      ], editor.fetch('connection_classes').map { |item| item.slice('id', 'roles', 'symmetric') }

      xp = editor.fetch('modules').find { |mod| mod.fetch('id') == 'xp' }
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
        editor.fetch('commands')
      )
    end
  end

  def test_build_emits_ipcraft_package_v1
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)

      manifest_path = build_ipcraft_manifest(package_root)
      manifest = JSON.parse(File.read(manifest_path))

      assert_equal 'ipcraft.package.v1', manifest.fetch('schema')
      assert_equal 'org.example.opennoc', manifest.fetch('id')
      assert_equal 'OpenNoC', manifest.fetch('name')
      assert_equal '1.0.0', manifest.fetch('version')
      assert_includes manifest.fetch('extensions'), 'ipcraft.views'
      refute manifest.key?('modules')
      refute manifest.key?('commands')
      refute manifest.key?('connection_classes')
      refute manifest.key?('emitters')
      refute manifest.key?('flows')
      refute manifest.key?('artifacts')
    end
  end

  def test_runtime_manifest_is_self_contained_without_ipcore_yml
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)

      manifest_path = build_ipcraft_manifest(package_root)
      FileUtils.rm_f(File.join(package_root, 'ipcore.yml'))
      manifest = JSON.parse(File.read(manifest_path))

      assert_equal 'ipcraft.package.v1', manifest.fetch('schema')
      assert_equal 'org.example.opennoc', manifest.fetch('id')
      assert_equal '1.0.0', manifest.fetch('version')
      assert_equal [{ 'module' => 'xp', 'file' => 'views/xp.xml' }],
                   manifest.fetch('views')
      refute_includes JSON.generate(manifest), 'ipcore.yml'
    end
  end

  def test_optional_sections_require_extensions
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml
             .sub("    enabled: true\n", "    enabled: true\n")
             .sub("\nviews:\n", "\nconfig_schema:\n  tables: []\nviews:\n")
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/config_schema\.tables requires extension ipcraft\.config\.tables/, error.message)
    end

    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml
             .sub("    enabled: true\n", "    enabled: true\n  ipcraft.config.tables:\n    enabled: true\n")
             .sub("\nviews:\n", "\nconfig_schema:\n  tables: []\nviews:\n")
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      manifest_path = build_ipcraft_manifest(package_root)
      manifest = JSON.parse(File.read(manifest_path))

      assert_includes manifest.fetch('extensions'), 'ipcraft.config.tables'
    end
  end

  def test_emits_declared_interfaces_and_connection_rules
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml
             .sub("  ipcraft.views:\n    enabled: true\n",
                  "  ipcraft.views:\n    enabled: true\n  ipcraft.interfaces:\n    enabled: true\n  ipcraft.composition:\n    enabled: true\n")
             .sub("\nipxact:\n", <<~YAML)

                interfaces:
                  - id: m_axi
                    kind: bus
                    protocol: AXI4
                    role: master
                connection_rules:
                  protocol_aliases:
                    AXI4: axi4
                  compatibility:
                    - connection_type: interface
                      from: { kind: bus, role: master, protocol: axi4 }
                      to: { kind: bus, role: slave, protocol: axi4 }
                      arity: binary
                ipxact:
             YAML
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      manifest = JSON.parse(File.read(build_ipcraft_manifest(package_root)))

      assert_includes manifest.fetch('extensions'), 'ipcraft.interfaces'
      assert_includes manifest.fetch('extensions'), 'ipcraft.composition'
      assert_equal 'm_axi', manifest.fetch('interfaces').first.fetch('id')
      assert_equal 'axi4',
                   manifest.fetch('connection_rules')
                           .fetch('protocol_aliases')
                           .fetch('AXI4')
    end
  end

  def test_rejects_author_native_ipcraft_editor_collision
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub("\nipxact:\n", <<~YAML)

        native:
          ipcraft:
            editor:
              owner: author
        ipxact:
      YAML
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/native\.ipcraft\.editor is reserved/, error.message)
    end
  end

  def test_specgen_does_not_emit_ipcraft_manifest_v1
    Dir.mktmpdir do |dir|
      write_repository_ipcraft_source_repo(dir)

      SpecGenerator.build_repository_ipcraft_manifests(root: dir)

      repository_ipcraft_package_dirs.each do |package_dir|
        manifest = File.read(File.join(dir, 'ipcores', package_dir, 'ipcraft.json'))
        refute_includes manifest, 'ipcraft.manifest.v1'
      end
    end
  end

  def test_emits_instance_policy
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "version: '1.0.0'\n",
          "version: '1.0.0'\ninstances:\n  max: 4\n"
        )
      )

      build_ipcraft_manifest(package_root)
      manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))

      assert_equal({ 'max' => 4 }, editor_manifest(manifest).fetch('instances'))
    end
  end

  def test_rejects_invalid_instance_policy
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "version: '1.0.0'\n",
          "version: '1.0.0'\ninstances:\n  max: 0\n"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/instances\.max must be positive/, error.message)
    end
  end

  def test_emits_display_label_binding
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "parameters:\n      x:",
          "display:\n      label_parameter: display_name\n      short_label_parameter: short_name\n    parameters:\n      display_name: { type: string, default: XP }\n      short_name: { type: string, default: X }\n      x:"
        )
      )

      build_ipcraft_manifest(package_root)
      manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))
      xp = editor_manifest(manifest).fetch('modules').find { |mod| mod.fetch('id') == 'xp' }

      assert_equal(
        { 'label_parameter' => 'display_name', 'short_label_parameter' => 'short_name' },
        xp.fetch('display')
      )
    end
  end

  def test_rejects_false_display_metadata
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "parameters:\n      x:",
          "display: false\n    parameters:\n      x:"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/module xp\.display must be a map/, error.message)
    end
  end

  def test_rejects_empty_display_metadata
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "parameters:\n      x:",
          "display:\n    parameters:\n      x:"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/module xp\.display must be a map/, error.message)
    end
  end

  def test_rejects_display_label_binding_missing_parameter
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "parameters:\n      x:",
          "display:\n      label_parameter: missing_label\n    parameters:\n      x:"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/module xp display.label_parameter references unknown parameter missing_label/, error.message)
    end
  end

  def test_emits_topology_side_metadata
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "modes: [chi_interconnect]\n        accepts:",
        "topology:\n          side: east\n          opposite: west\n          role: router_port\n        modes: [chi_interconnect]\n        accepts:"
      ).sub(
        "\nviews:\n",
        "\n      - id: west\n        modes: [chi_interconnect]\n        accepts:\n          - class: chi_node_interface\n            role: interconnect\n        multi_connection: false\nviews:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      build_ipcraft_manifest(package_root)
      manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))
      xp = editor_manifest(manifest).fetch('modules').find { |mod| mod.fetch('id') == 'xp' }
      rnf0 = xp.fetch('interfaces').find { |interface| interface.fetch('id') == 'rnf0' }

      assert_equal({ 'side' => 'east', 'opposite' => 'west', 'role' => 'router_port' }, rnf0.fetch('topology'))
    end
  end

  def test_rejects_false_interface_topology_metadata
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "modes: [chi_interconnect]\n        accepts:",
        "topology: false\n        modes: [chi_interconnect]\n        accepts:"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/xp\.rnf0\.topology must be a map/, error.message)
    end
  end

  def test_rejects_empty_interface_topology_metadata
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "modes: [chi_interconnect]\n        accepts:",
        "topology:\n        modes: [chi_interconnect]\n        accepts:"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/xp\.rnf0\.topology must be a map/, error.message)
    end
  end

  def test_rejects_topology_opposite_that_is_not_a_module_interface
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "modes: [chi_interconnect]\n        accepts:",
        "topology:\n          side: east\n          opposite: missing_west\n        modes: [chi_interconnect]\n        accepts:"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/xp.rnf0 topology.opposite references unknown interface missing_west/, error.message)
    end
  end

  def test_emits_generation_metadata
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  outputs:\n    - id: manifest\n      kind: json\n      path: manifest.json\n  module_mappings:\n    xp: XP\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      build_ipcraft_manifest(package_root)
      manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))

      generation = editor_manifest(manifest).fetch('generation')
      assert_equal 'ipcraft.common.v1', generation.fetch('engine')
      assert_equal 'manifest.json', generation.fetch('outputs').first.fetch('path')
      assert_equal({ 'xp' => 'XP' }, generation.fetch('module_mappings'))
    end
  end

  def test_rejects_generation_module_mapping_for_unknown_module
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  module_mappings:\n    missing_module: XP\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/generation.module_mappings references unknown module missing_module/, error.message)
    end
  end

  def test_rejects_generation_output_path_outside_package
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  outputs:\n    - id: bad\n      kind: json\n      path: ../manifest.json\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/generation output bad path escapes package root/, error.message)
    end
  end

  def test_rejects_generation_output_path_with_backslash_traversal
    Dir.mktmpdir do |dir|
      bad_path = %q(..\manifest.json)
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  outputs:\n    - id: bad\n      kind: json\n      path: #{bad_path}\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/generation output bad path .*backslash/, error.message)
    end
  end

  def test_rejects_generation_template_path_with_backslash_traversal
    Dir.mktmpdir do |dir|
      bad_path = %q(..\template.erb)
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  templates:\n    top: #{bad_path}\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/generation\.templates\.top .*backslash/, error.message)
    end
  end

  def test_rejects_duplicate_generation_output_ids
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  outputs:\n    - id: manifest\n      kind: json\n      path: manifest.json\n    - id: manifest\n      kind: json\n      path: manifest-copy.json\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/Duplicate generation output id: manifest/, error.message)
    end
  end

  def test_rejects_duplicate_generation_command_ids
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  commands:\n    - id: build\n      executable: tools/generate\n      args: []\n    - id: build\n      executable: tools/validate\n      args: []\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/Duplicate generation command id: build/, error.message)
    end
  end

  def test_rejects_generation_coordinate_binding_missing_parameter
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  coordinate_bindings:\n    xp: { col: missing_col }\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/generation\.coordinate_bindings\.xp\.col references unknown parameter missing_col/, error.message)
    end
  end

  def test_rejects_generation_attachment_binding_missing_interface
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  attachment_bindings:\n    xp:\n      local: missing_interface\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/generation\.attachment_bindings\.xp\.local references unknown interface missing_interface/, error.message)
    end
  end

  def test_rejects_generation_parameter_projection_missing_parameter
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "commands:\n",
        "generation:\n  engine: ipcraft.common.v1\n  parameter_projections:\n    top_width: missing_width\ncommands:\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/generation\.parameter_projections\.top_width references unknown parameter missing_width/, error.message)
    end
  end

  def test_emits_framework_tool_command
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "  generate:\n    executable: tools/generate\n    input_schema: ipcraft.noc.project.v1\n",
        "  generate:\n    framework_tool: ipcraft-generate\n    input_schema: ipcraft.noc.project.v1\n    args:\n      - --manifest\n      - \"{manifest}\"\n      - --input\n      - \"{input}\"\n      - --output\n      - \"{output}\"\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      build_ipcraft_manifest(package_root)
      manifest = JSON.parse(File.read(File.join(package_root, 'ipcraft.json')))
      command = editor_manifest(manifest).fetch('commands').fetch('generate')

      assert_equal(
        {
          'framework_tool' => 'ipcraft-generate',
          'input_schema' => 'ipcraft.noc.project.v1',
          'args' => common_ipcraft_generate_args
        },
        command
      )
    end
  end

  def test_rejects_command_with_executable_and_framework_tool
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "  generate:\n    executable: tools/generate\n",
        "  generate:\n    executable: tools/generate\n    framework_tool: ipcraft-generate\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/commands\.generate must specify exactly one of executable or framework_tool/, error.message)
    end
  end

  def test_rejects_command_without_executable_or_framework_tool
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub("    executable: tools/generate\n", '')
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/commands\.generate must specify exactly one of executable or framework_tool/, error.message)
    end
  end

  def test_rejects_unknown_framework_tool
    Dir.mktmpdir do |dir|
      yaml = ipcraft_package_yaml.sub(
        "  generate:\n    executable: tools/generate\n",
        "  generate:\n    framework_tool: unknown-tool\n"
      )
      package_root = write_ipcraft_package_source(dir, yaml: yaml)

      error = assert_raises(SpecGenerator::SpecError) { build_ipcraft_manifest(package_root) }
      assert_match(/commands\.generate\.framework_tool unknown tool unknown-tool/, error.message)
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
      modes = editor_manifest(manifest).fetch('extensions').fetch('noc.v1').fetch('modes')
      assert_equal 'target', modes.fetch('chi_interconnect').fetch('ipxact').fetch('mode')
      assert_equal 'initiator', modes.fetch('chi_requester_node').fetch('ipxact').fetch('mode')
    end
  end

  def test_repository_finepaper_noc_builds_ipcraft_manifest
    assert_repository_package_source_schema('finepaper-noc')

    manifest = build_repository_ipcraft_manifest('finepaper-noc')

    assert_repository_ipcraft_manifest_contract(manifest)
  end

  def test_repository_ravenoc_builds_ipcraft_manifest
    assert_repository_package_source_schema('ravenoc')

    manifest = build_repository_ipcraft_manifest('ravenoc')

    assert_repository_ipcraft_manifest_contract(manifest)
  end

  def test_repository_opennoc_builds_ipcraft_manifest
    assert_repository_package_source_schema('opennoc')

    manifest = build_repository_ipcraft_manifest('opennoc')

    assert_repository_ipcraft_manifest_contract(manifest)
    editor = editor_manifest(manifest)
    extension_modes = editor.fetch('extensions').fetch('noc.v1').fetch('modes')
    chi_modes = editor.fetch('modules').flat_map do |mod|
      mod.fetch('interfaces').flat_map { |interface| interface.fetch('modes') }
    end.select { |mode| mode.start_with?('chi_') }.uniq
    refute_empty chi_modes
    chi_modes.each do |mode|
      assert_includes extension_modes.keys, mode
      assert extension_modes.fetch(mode).fetch('ipxact').fetch('mode')
    end
    assert_equal 'target', extension_modes.fetch('chi_interconnect').fetch('ipxact').fetch('mode')
    assert_equal 'initiator', extension_modes.fetch('chi_requester_node').fetch('ipxact').fetch('mode')
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
        "\nviews:\n",
        "\n  - id: xp\n    name: Duplicate XP\n    interfaces: []\nviews:\n"
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
        "\nviews:\n",
        "\n      - id: rnf0\n        modes: [chi_interconnect]\n        accepts:\n          - class: chi_node_interface\n            role: interconnect\n        multi_connection: false\n        ipxact:\n          bus_interface: rnf0_duplicate\nviews:\n"
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

  def test_rejects_unknown_topology_fields
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub("    module: xp\n", "    module: xp\n    stray: true\n")
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/Unknown topology mesh field: stray/, error.message)
    end
  end

  def test_rejects_topology_module_missing_from_package
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub("    module: xp\n", "    module: missing_router\n")
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/topology mesh references unknown module missing_router/, error.message)
    end
  end

  def test_rejects_unsupported_topology_kind
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub('kind: mesh', 'kind: star')
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/topology mesh kind star is unsupported/, error.message)
    end
  end

  def test_rejects_topology_port_values_missing_from_module_interfaces
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "    module: xp\n",
          "    module: xp\n    ports: { north: missing_north }\n"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/topology mesh port north references unknown xp interface missing_north/, error.message)
    end
  end

  def test_rejects_malformed_topology_parameters
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(
        dir,
        yaml: ipcraft_package_yaml.sub(
          "    module: xp\n",
          "    module: xp\n    parameters:\n      rows: { label: Rows, default: two, min: 1, max: 8 }\n"
        )
      )

      error = assert_raises(SpecGenerator::SpecError) do
        build_ipcraft_manifest(package_root)
      end

      assert_match(/topology mesh parameter rows default must be an integer/, error.message)
    end
  end

  def test_cli_checks_and_builds_ipcraft_manifest
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)
      ipcore_path = File.join(package_root, 'ipcore.yml')

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

      check_stdout, check_stderr, check_status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        'check',
        '--ipcore',
        ipcore_path
      )
      assert check_status.success?, check_stderr
      assert_includes check_stdout, "Checked ipcraft package source: #{ipcore_path}"
    end
  end

  def test_cli_check_rejects_drifted_ipcraft_manifest
    Dir.mktmpdir do |dir|
      package_root = write_ipcraft_package_source(dir)
      ipcore_path = File.join(package_root, 'ipcore.yml')

      build_ipcraft_manifest(package_root)
      manifest_path = File.join(package_root, 'ipcraft.json')
      manifest = JSON.parse(File.read(manifest_path))
      manifest['drift_probe'] = true
      File.write(manifest_path, "#{JSON.pretty_generate(manifest)}\n")

      check_stdout, check_stderr, check_status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        'check',
        '--ipcore',
        ipcore_path,
        '--package-root',
        package_root
      )
      refute check_status.success?
      assert_empty check_stdout
      assert_match(/ipcraft\.json.*out of date|out of date.*ipcraft\.json|mismatch/i, check_stderr)
    end
  end

  def test_cli_rejects_legacy_finepaper_schema_for_ipcraft_check_and_build
    Dir.mktmpdir do |dir|
      write_ravenoc_source(dir)
      ipcore_path = File.join(dir, 'ipcores/ravenoc/ipcore.yml')
      package_root = File.dirname(ipcore_path)

      check_stdout, check_stderr, check_status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        'check',
        '--ipcore',
        ipcore_path
      )
      refute check_status.success?
      assert_empty check_stdout
      assert_includes check_stderr, 'error: schema must be ipcraft.package.v1'

      build_stdout, build_stderr, build_status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        'build',
        '--ipcore',
        ipcore_path,
        '--package-root',
        package_root
      )
      refute build_status.success?
      assert_empty build_stdout
      assert_includes build_stderr, 'error: schema must be ipcraft.package.v1'
      refute File.file?(File.join(package_root, 'ipcraft.json'))
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
      refute_includes stderr, 'command is required'
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
      refute_includes stderr, 'command is required'
    end
  end

  def test_rejects_malformed_module_view_without_module_attribute
    Dir.mktmpdir do |dir|
      malformed = rave_tile_view_xml.sub(' module="RaveTile"', '')
      write_ravenoc_source(dir, rave_tile_view: malformed)

      error = assert_raises(SpecGenerator::SpecError) do
        parse_ravenoc(dir)
      end

      assert_match(/view RaveTile is missing module attribute/, error.message)
    end
  end

  def test_cli_default_builds_repository_ipcraft_manifests
    Dir.mktmpdir do |dir|
      write_repository_ipcraft_source_repo(dir)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        chdir: dir
      )

      assert status.success?, stderr
      assert_includes stdout, 'Generated repository ipcraft manifests'
      repository_ipcraft_package_dirs.each do |package_dir|
        assert File.file?(File.join(dir, 'ipcores', package_dir, 'ipcraft.json'))
      end
    end
  end

  def test_cli_check_passes_when_repository_ipcraft_manifests_match_sources
    Dir.mktmpdir do |dir|
      write_repository_ipcraft_source_repo(dir, include_manifests: true)

      stdout, stderr, status = Open3.capture3(
        RbConfig.ruby,
        File.expand_path('../bin/spec-gen', __dir__),
        '--check',
        chdir: dir
      )

      assert status.success?, stderr
      assert_includes stdout, 'Repository ipcraft manifests are up to date'
    end
  end

  def test_check_repository_ipcraft_manifests_reports_manifest_drift
    Dir.mktmpdir do |dir|
      write_repository_ipcraft_source_repo(dir, include_manifests: true)
      path = File.join(dir, 'ipcores/ravenoc/ipcraft.json')
      manifest = JSON.parse(File.read(path))
      manifest['name'] = 'Drifted RaveNoC'
      File.write(path, "#{JSON.pretty_generate(manifest)}\n")

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_ipcraft_manifests(root: dir)
      end

      assert_match(%r{content mismatch: ipcores/ravenoc/ipcraft\.json}, error.message)
    end
  end

  private

  def repo_path(*parts)
    File.expand_path(File.join('..', '..', *parts), __dir__)
  end

  def repository_ipcraft_package_dirs
    %w[finepaper-noc ravenoc opennoc]
  end

  def finepaper_noc_ipcore_yaml
    legacy_ipcore_yaml('finepaper-noc')
  end

  def ravenoc_ipcore_yaml
    legacy_ipcore_yaml('ravenoc')
  end

  def opennoc_ipcore_yaml
    legacy_ipcore_yaml('opennoc')
  end

  def legacy_ipcore_yaml(package_dir)
    @legacy_ipcore_yaml ||= {}
    @legacy_ipcore_yaml[package_dir] ||= begin
      relpath = "ipcores/#{package_dir}/ipcore.yml"
      fixture = legacy_ipcore_yaml_from_git(relpath)
      fixture ||= begin
        current = File.read(repo_path(relpath))
        current if legacy_ipcore_yaml?(current)
      end
      raise "Legacy IP core fixture not found for #{package_dir}" unless fixture

      fixture
    end
  end

  def legacy_ipcore_yaml_from_git(relpath)
    revisions, _stderr, status = Open3.capture3(
      'git',
      'rev-list',
      '--max-count=50',
      'HEAD',
      '--',
      relpath,
      chdir: repo_path
    )
    return nil unless status.success?

    revisions.each_line do |revision|
      text, _stderr, show_status = Open3.capture3(
        'git',
        'show',
        "#{revision.strip}:#{relpath}",
        chdir: repo_path
      )
      return text if show_status.success? && legacy_ipcore_yaml?(text)
    end
    nil
  end

  def legacy_ipcore_yaml?(text)
    text.start_with?("schema: finepaper.ipcore.v1\n")
  end

  def opennoc_view_xml(name)
    File.read(repo_path("ipcores/opennoc/views/#{name}.xml"))
  end

  def write_repository_ipcraft_source_repo(root, include_manifests: false)
    repository_ipcraft_package_dirs.each do |package_dir|
      package_root = File.join(root, 'ipcores', package_dir)
      FileUtils.mkdir_p(package_root)
      FileUtils.cp(repo_path('ipcores', package_dir, 'ipcore.yml'), File.join(package_root, 'ipcore.yml'))
      FileUtils.cp_r(repo_path('ipcores', package_dir, 'views'), File.join(package_root, 'views'))
      if include_manifests
        FileUtils.cp(repo_path('ipcores', package_dir, 'ipcraft.json'), File.join(package_root, 'ipcraft.json'))
      end
    end
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

  def parse_finepaper_noc(root)
    SpecGenerator::IpCoreParser
      .new(File.join(root, 'ipcores/finepaper-noc/ipcore.yml'),
           File.join(root, 'ipcores/finepaper-noc/views'))
      .parse
  end

  def parse_ravenoc(root)
    SpecGenerator::IpCoreParser
      .new(File.join(root, 'ipcores/ravenoc/ipcore.yml'),
           File.join(root, 'ipcores/ravenoc/views'))
      .parse
  end

  def build_ipcraft_manifest(package_root)
    SpecGenerator.build_ipcraft_manifest(
      ipcore_path: File.join(package_root, 'ipcore.yml'),
      package_root: package_root
    )
  end

  def assert_repository_package_source_schema(package_dir)
    source = SpecGenerator::ConstrainedYamlLoader.load_file(repo_path('ipcores', package_dir, 'ipcore.yml'))

    assert_equal 'ipcraft.package.v1', source.fetch('schema')
  end

  def build_repository_ipcraft_manifest(package_dir)
    Dir.mktmpdir do |dir|
      package_root = File.join(dir, 'ipcores', package_dir)
      FileUtils.mkdir_p(package_root)
      FileUtils.cp(repo_path('ipcores', package_dir, 'ipcore.yml'), File.join(package_root, 'ipcore.yml'))
      FileUtils.cp_r(repo_path('ipcores', package_dir, 'views'), File.join(package_root, 'views'))

      manifest_path = SpecGenerator.build_ipcraft_manifest(
        ipcore_path: File.join(package_root, 'ipcore.yml'),
        package_root: package_root
      )
      assert_equal File.join(package_root, 'ipcraft.json'), manifest_path

      JSON.parse(File.read(manifest_path))
    end
  end

  def assert_repository_ipcraft_manifest_contract(manifest)
    assert_equal 'ipcraft.package.v1', manifest.fetch('schema')
    assert_includes manifest.fetch('extensions'), 'ipcraft.config.params'
    assert_includes manifest.fetch('extensions'), 'ipcraft.graph_config'
    assert_includes manifest.fetch('extensions'), 'ipcraft.views'
    assert_includes manifest.fetch('extensions'), 'ipcraft.emitters'
    assert_includes manifest.fetch('extensions'), 'ipcraft.flows'
    assert_includes manifest.fetch('extensions'), 'ipcraft.artifacts'
    assert_equal(
      %w[emit_graph_config emit_parameters],
      manifest.fetch('emitters').map { |emitter| emitter.fetch('kind') }
    )
    generate_flow = manifest.fetch('flows').find { |flow| flow.fetch('id') == 'generate' }
    refute_nil generate_flow
    assert_equal %w[emit_inputs exec collect_artifacts],
                 generate_flow.fetch('steps').map { |step| step.fetch('kind') }
    command = generate_flow.fetch('steps').fetch(1).fetch('command')
    assert_equal 'ipcraft-generate', command.fetch('framework_tool')
    assert_includes command.fetch('args'), '{package.manifest}'
    assert_includes command.fetch('args'), '{inputs.manifest}'
    validate_flow = manifest.fetch('flows').find { |flow| flow.fetch('id') == 'validate' }
    refute_nil validate_flow
    assert_equal %w[emit_inputs exec],
                 validate_flow.fetch('steps').map { |step| step.fetch('kind') }
    validate_command = validate_flow.fetch('steps').fetch(1).fetch('command')
    assert_equal 'ipcraft-generate', validate_command.fetch('framework_tool')
    assert_equal ['--manifest', '{package.manifest}', '--input', '{inputs.manifest}', '--validate'],
                 validate_command.fetch('args')
    assert_equal({ 'stdout' => 'stdout.log', 'stderr' => 'stderr.log', 'max_bytes' => 1048576 },
                 validate_command.fetch('capture'))
    refute_includes validate_flow.fetch('steps').map { |step| step.fetch('kind') }, 'collect_artifacts'
    refute_empty manifest.fetch('artifacts')
    refute manifest.key?('modules')
    refute manifest.key?('commands')
    refute manifest.key?('connection_classes')

    editor = editor_manifest(manifest)
    refute editor.key?('commands')

    module_ids = editor.fetch('modules').map { |mod| mod.fetch('id') }
    manifest.fetch('views').each do |view|
      assert_includes module_ids, view.fetch('module')
    end

    connection_class_ids = editor.fetch('connection_classes').map { |klass| klass.fetch('id') }
    editor.fetch('modules').each do |mod|
      parameters = mod.fetch('parameters', {})
      if parameters.key?('display_name')
        display = mod.fetch('display')
        assert_equal 'display_name', display.fetch('label_parameter')
        assert_equal 'external_id', display.fetch('short_label_parameter') if parameters.key?('external_id')
      end

      mod.fetch('interfaces').each do |interface|
        interface.fetch('accepts').each do |accept|
          assert_includes connection_class_ids, accept.fetch('class')
        end
      end
    end
  end

  def editor_manifest(manifest)
    manifest.fetch('native').fetch('ipcraft').fetch('editor')
  end

  def common_ipcraft_generate_args
    ['--manifest', '{manifest}', '--input', '{input}', '--output', '{output}']
  end

  def parse_minimal_ipcore(root)
    SpecGenerator::IpCoreParser.new(File.join(root, 'ipcore.yml'), File.join(root, 'views')).parse
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
        ipcraft.config.params:
          enabled: true
        ipcraft.graph_config:
          enabled: true
        ipcraft.views:
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
