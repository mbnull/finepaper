# frozen_string_literal: true

require "fileutils"
require "json"
require "minitest/autorun"
require "open3"
require "tmpdir"

class IpcraftNocBlackBoxValidationTest < Minitest::Test
  REPO_ROOT = File.expand_path("../../..", __dir__)
  SPEC_GEN = File.join(REPO_ROOT, "spec_generator", "bin", "spec-gen")
  IPCRAFT_GENERATE = File.join(REPO_ROOT, "ipcraft_generator", "bin", "ipcraft-generate")

  FIXTURE_ID = "blackbox.synthetic.noc"

  CommandResult = Struct.new(:argv, :stdout, :stderr, :status, keyword_init: true) do
    def success?
      status == 0
    end

    def output
      [stdout, stderr].join("\n")
    end

    def details
      <<~TEXT
        command: #{argv.join(" ")}
        exit: #{status}
        stdout:
        #{stdout}
        stderr:
        #{stderr}
      TEXT
    end
  end

  def setup
    @tmpdir = Dir.mktmpdir("ipcraft-noc-blackbox-")
  end

  def teardown
    FileUtils.remove_entry(@tmpdir) if @tmpdir && Dir.exist?(@tmpdir)
  end

  def test_public_tools_are_available
    [SPEC_GEN, IPCRAFT_GENERATE].each do |tool|
      assert_path_exists tool, "ENVIRONMENT_GAP: public tool is missing: #{tool}"
      assert File.executable?(tool), "ENVIRONMENT_GAP: public tool is not executable: #{tool}"
    end
  end

  def test_fixture_a_builds_checks_and_is_deterministic
    package_root = write_fixture_a("fixture-a")

    build = run_tool(SPEC_GEN, "build", "--ipcore", File.join(package_root, "ipcore.yml"), "--package-root", package_root)
    assert_success build, "REGRESSION: Fixture A must build through spec-gen"

    manifest_path = File.join(package_root, "ipcraft.json")
    assert_path_exists manifest_path, "REGRESSION: spec-gen build did not write ipcraft.json"

    manifest = JSON.parse(File.read(manifest_path))
    assert_equal "ipcraft.manifest.v1", manifest.fetch("schema"), "REGRESSION: generated manifest schema changed"
    assert_equal FIXTURE_ID, manifest.fetch("id"), "REGRESSION: generated manifest package ID changed"
    assert_equal ["Endpoint", "Tile"], manifest.fetch("modules").map { |mod| mod.fetch("id") }.sort

    first_manifest = File.read(manifest_path)

    check = run_tool(SPEC_GEN, "check", "--ipcore", File.join(package_root, "ipcore.yml"), "--package-root", package_root)
    assert_success check, "REGRESSION: spec-gen check must pass after a fresh build"

    rebuild = run_tool(SPEC_GEN, "build", "--ipcore", File.join(package_root, "ipcore.yml"), "--package-root", package_root)
    assert_success rebuild, "REGRESSION: second spec-gen build failed"
    assert_equal first_manifest, File.read(manifest_path), "REGRESSION: spec-gen build is not deterministic for unchanged source"
  end

  def test_spec_gen_check_detects_manifest_drift
    package_root = write_fixture_a("fixture-a")
    build = run_tool(SPEC_GEN, "build", "--ipcore", File.join(package_root, "ipcore.yml"), "--package-root", package_root)
    assert_success build, "REGRESSION: Fixture A must build before drift can be tested"

    manifest_path = File.join(package_root, "ipcraft.json")
    manifest = JSON.parse(File.read(manifest_path))
    manifest["blackbox_drift_probe"] = true
    File.write(manifest_path, JSON.pretty_generate(manifest))

    check = run_tool(SPEC_GEN, "check", "--ipcore", File.join(package_root, "ipcore.yml"), "--package-root", package_root)
    refute check.success?, "REGRESSION: spec-gen check accepted a drifted ipcraft.json\n#{check.details}"
    assert_match(/drift|match|mismatch|out.of.date|ipcraft\.json/i, check.output,
                 "REGRESSION: drift diagnostic was not actionable\n#{check.details}")
  end

  def test_unknown_view_anchor_is_rejected
    package_root = write_fixture_a("invalid-anchor", invalid_tile_anchor: true)

    build = run_tool(SPEC_GEN, "build", "--ipcore", File.join(package_root, "ipcore.yml"), "--package-root", package_root)
    refute build.success?, "REGRESSION: spec-gen accepted a view anchor for an unknown interface\n#{build.details}"
    assert_match(/unknown|missing|anchor|interface|fabric_missing/i, build.output,
                 "REGRESSION: unknown-interface diagnostic was not actionable\n#{build.details}")
  end

  def test_ipcraft_generate_accepts_contract_input_from_different_cwd
    package_root = build_fixture_a("fixture-a")
    input_path = write_contract_command_input("valid-input.json")
    output_dir = File.join(@tmpdir, "generated")
    cwd = Dir.mktmpdir("ipcraft-generate-cwd-", @tmpdir)

    result = run_tool(IPCRAFT_GENERATE,
                      "--manifest", File.join(package_root, "ipcraft.json"),
                      "--input", input_path,
                      "--output", output_dir,
                      chdir: cwd)

    assert_success result, "REGRESSION: ipcraft-generate must accept ipcraft.noc.project.v1 input from a different cwd"
    assert_path_exists File.join(output_dir, "manifest.json"),
                       "REGRESSION: ipcraft-generate did not write output manifest.json"
  end

  def test_ipcraft_generate_rejects_invalid_command_inputs_without_success_manifest
    package_root = build_fixture_a("fixture-a")
    manifest_path = File.join(package_root, "ipcraft.json")

    invalid_cases = {
      "mismatched-package" => lambda do |input|
        input["package"] = "blackbox.synthetic.other"
        input["package_id"] = "blackbox.synthetic.other"
      end,
      "unknown-instance" => lambda do |input|
        input.fetch("connections").first.fetch("target")["instance"] = "missing_tile"
      end,
      "unknown-interface" => lambda do |input|
        input.fetch("connections").first.fetch("source")["interface"] = "missing_interface"
      end,
      "invalid-endpoint-shape" => lambda do |input|
        input.fetch("connections").first["source"] = {"module" => "tile_0_0", "port" => "fabric_a_out"}
      end,
      "unsupported-schema" => lambda do |input|
        input["schema"] = "ipcraft.noc.project.v0"
      end
    }

    failures = []

    invalid_cases.each do |name, mutate|
      input = contract_command_input
      mutate.call(input)

      input_path = File.join(@tmpdir, "#{name}.json")
      File.write(input_path, JSON.pretty_generate(input))
      output_dir = File.join(@tmpdir, "generated-#{name}")

      result = run_tool(IPCRAFT_GENERATE,
                        "--manifest", manifest_path,
                        "--input", input_path,
                        "--output", output_dir)

      if result.success?
        failures << "REGRESSION: ipcraft-generate accepted invalid input case #{name}\n#{result.details}"
        next
      end

      if File.exist?(File.join(output_dir, "manifest.json"))
        failures << "REGRESSION: failed generation case #{name} produced a success manifest"
      end

      unless result.output.match?(/package|instance|interface|endpoint|schema|connection|graph|invalid|unknown|unsupported/i)
        failures << "REGRESSION: invalid input case #{name} did not produce an actionable diagnostic\n#{result.details}"
      end
    end

    assert_empty failures, failures.join("\n\n")
  end

  private

  def assert_success(result, message)
    assert result.success?, "#{message}\n#{result.details}"
  end

  def run_tool(*argv, chdir: @tmpdir)
    stdout, stderr, status = Open3.capture3(*argv, chdir: chdir)
    CommandResult.new(argv: argv, stdout: stdout, stderr: stderr, status: status.exitstatus)
  end

  def build_fixture_a(name)
    package_root = write_fixture_a(name)
    build = run_tool(SPEC_GEN, "build", "--ipcore", File.join(package_root, "ipcore.yml"), "--package-root", package_root)
    assert_success build, "REGRESSION: Fixture A must build before generator validation"
    package_root
  end

  def write_fixture_a(name, invalid_tile_anchor: false)
    package_root = File.join(@tmpdir, name)
    views_dir = File.join(package_root, "views")
    FileUtils.mkdir_p(views_dir)

    File.write(File.join(package_root, "ipcore.yml"), fixture_a_ipcore_yml)
    File.write(File.join(views_dir, "Tile.xml"), tile_view_xml(invalid_anchor: invalid_tile_anchor))
    File.write(File.join(views_dir, "Endpoint.xml"), endpoint_view_xml)

    package_root
  end

  def fixture_a_ipcore_yml
    <<~YAML
      schema: ipcraft.package.v1
      id: #{FIXTURE_ID}
      name: Black-Box Synthetic NoC
      version: '1.0'

      extensions:
        noc.v1:
          enabled: true

      connection_classes:
        - id: fabric_link
          roles: [initiator, target]
          symmetric: false
        - id: endpoint_link
          roles: [initiator, target]
          symmetric: false

      modules:
        - id: Tile
          name: Tile
          description: Synthetic metadata-only mesh tile.
          graph_role: host
          display:
            label_parameter: display_name
            short_label_parameter: external_id
          parameters:
            display_name: { type: string, default: '', emit: editor, label: Display name }
            external_id: { type: string, default: '', emit: editor, label: External ID }
            mesh_col: { type: int, default: 0, configurable: false, emit: attribute }
            mesh_row: { type: int, default: 0, configurable: false, emit: attribute }
          interfaces:
            - id: fabric_a_out
              label: Fabric A Out
              modes: [initiator]
              accepts: [{ class: fabric_link, role: initiator }]
              multi_connection: false
              topology: { side: east, opposite: fabric_a_in }
            - id: fabric_a_in
              label: Fabric A In
              modes: [target]
              accepts: [{ class: fabric_link, role: target }]
              multi_connection: false
              topology: { side: west, opposite: fabric_a_out }
            - id: fabric_b_out
              label: Fabric B Out
              modes: [initiator]
              accepts: [{ class: fabric_link, role: initiator }]
              multi_connection: false
              topology: { side: south, opposite: fabric_b_in }
            - id: fabric_b_in
              label: Fabric B In
              modes: [target]
              accepts: [{ class: fabric_link, role: target }]
              multi_connection: false
              topology: { side: north, opposite: fabric_b_out }

        - id: Endpoint
          name: Endpoint
          description: Synthetic endpoint.
          graph_role: attached
          display:
            label_parameter: display_name
            short_label_parameter: external_id
          parameters:
            display_name: { type: string, default: '', emit: editor, label: Display name }
            external_id: { type: string, default: '', emit: editor, label: External ID }
          interfaces:
            - id: local_noc
              label: Local NoC
              modes: [initiator]
              accepts: [{ class: endpoint_link, role: initiator }]
              multi_connection: false

      views:
        - module: Tile
          file: views/Tile.xml
        - module: Endpoint
          file: views/Endpoint.xml

      topologies:
        - id: mesh
          label: Mesh
          kind: mesh
          module: Tile
          id_pattern: tile_{row}_{col}
          ports: { east: fabric_a_out, west: fabric_a_in, south: fabric_b_out, north: fabric_b_in }
          parameters:
            rows: { label: Rows, default: 2, min: 1, max: 8 }
            cols: { label: Columns, default: 2, min: 1, max: 8 }

      generation:
        engine: ipcraft.common.v1
        module_mappings:
          Tile: router
          Endpoint: endpoint
        coordinate_bindings:
          Tile: { col: mesh_col, row: mesh_row }
        outputs:
          - id: manifest
            kind: json
            path: manifest.json

      commands:
        generate:
          framework_tool: ipcraft-generate
          input_schema: ipcraft.noc.project.v1
          args: ["--manifest", "{manifest}", "--input", "{input}", "--output", "{output}"]
    YAML
  end

  def tile_view_xml(invalid_anchor: false)
    first_ref = invalid_anchor ? "fabric_missing" : "fabric_a_out"

    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="Tile">
        <graphics layout="mesh_router" node_color="#7cb9e8" supports_collapse="true">
          <expanded min_width="128" height="112" caption_left="28" caption_top="6" port_inset="18" />
          <collapsed min_width="96" height="84" caption_left="28" caption_top="24" endpoint_inset="18" />
          <arrangement endpoint_offset_x="152" mesh_spacing_x="200" mesh_spacing_y="160" />
        </graphics>
        <anchors>
          <anchor ref="#{first_ref}" x="128" y="56" normal_x="1" normal_y="0" label="Fabric A Out" label_x="96" label_y="56" />
          <anchor ref="fabric_a_in" x="0" y="56" normal_x="-1" normal_y="0" label="Fabric A In" label_x="32" label_y="56" />
          <anchor ref="fabric_b_out" x="64" y="112" normal_x="0" normal_y="1" label="Fabric B Out" label_x="64" label_y="94" />
          <anchor ref="fabric_b_in" x="64" y="0" normal_x="0" normal_y="-1" label="Fabric B In" label_x="64" label_y="28" />
        </anchors>
      </module-view>
    XML
  end

  def endpoint_view_xml
    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="Endpoint">
        <graphics layout="endpoint" node_color="#d6f4b6">
          <expanded min_width="104" height="54" caption_left="8" caption_top="6" />
          <arrangement loose_endpoint_spacing_x="168" loose_endpoint_spacing_y="84" loose_endpoint_margin_y="116" />
        </graphics>
        <anchors>
          <anchor ref="local_noc" x="104" y="27" normal_x="1" normal_y="0" label="Local NoC" label_x="72" label_y="40" />
        </anchors>
      </module-view>
    XML
  end

  def write_contract_command_input(filename)
    path = File.join(@tmpdir, filename)
    File.write(path, JSON.pretty_generate(contract_command_input))
    path
  end

  def contract_command_input
    {
      "schema" => "ipcraft.noc.project.v1",
      "package_id" => FIXTURE_ID,
      "package" => FIXTURE_ID,
      "project" => {
        "id" => "blackbox_project",
        "name" => "Black-Box Synthetic Project"
      },
      "ip_instance" => {
        "id" => "noc_0",
        "package_id" => FIXTURE_ID,
        "name" => "Synthetic NoC"
      },
      "instances" => [
        {
          "id" => "tile_0_0",
          "module_id" => "Tile",
          "type" => "Tile",
          "parameters" => {
            "display_name" => "Alpha Tile",
            "external_id" => "T00",
            "mesh_row" => 0,
            "mesh_col" => 0
          },
          "interfaces" => ["fabric_a_out", "fabric_a_in", "fabric_b_out", "fabric_b_in"]
        },
        {
          "id" => "tile_0_1",
          "module_id" => "Tile",
          "type" => "Tile",
          "parameters" => {
            "display_name" => "Beta Tile",
            "external_id" => "T01",
            "mesh_row" => 0,
            "mesh_col" => 1
          },
          "interfaces" => ["fabric_a_out", "fabric_a_in", "fabric_b_out", "fabric_b_in"]
        }
      ],
      "connections" => [
        {
          "id" => "tile_0_0_to_tile_0_1",
          "class" => "fabric_link",
          "source" => {
            "instance" => "tile_0_0",
            "interface" => "fabric_a_out"
          },
          "target" => {
            "instance" => "tile_0_1",
            "interface" => "fabric_a_in"
          },
          "interfaces" => [
            {
              "instance" => "tile_0_0",
              "interface" => "fabric_a_out"
            },
            {
              "instance" => "tile_0_1",
              "interface" => "fabric_a_in"
            }
          ]
        }
      ]
    }
  end
end
