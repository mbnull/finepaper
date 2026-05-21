# frozen_string_literal: true

require "fileutils"
require "json"
require "minitest/autorun"
require "open3"
require "tmpdir"

class IpcraftNocPhaseReviewTest < Minitest::Test
  REPO_ROOT = File.expand_path("../../..", __dir__)
  SPEC_GEN = File.join(REPO_ROOT, "spec_generator", "bin", "spec-gen")
  IPCRAFT_GENERATE = File.join(REPO_ROOT, "ipcraft_generator", "bin", "ipcraft-generate")
  XMAKE = "xmake"

  PACKAGE_ID = "phase.synthetic.noc"

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
    @tmpdir = ENV["IPCRAFT_PHASE_REVIEW_EVIDENCE_DIR"]
    @tmpdir = Dir.mktmpdir("ipcraft-phase-review-") if @tmpdir.nil? || @tmpdir.empty?
    FileUtils.mkdir_p(@tmpdir)
  end

  def teardown
    return if ENV["IPCRAFT_PHASE_REVIEW_KEEP"] == "1"
    return if ENV["IPCRAFT_PHASE_REVIEW_EVIDENCE_DIR"] && !ENV["IPCRAFT_PHASE_REVIEW_EVIDENCE_DIR"].empty?

    FileUtils.remove_entry(@tmpdir) if @tmpdir && Dir.exist?(@tmpdir)
  end

  def test_public_review_tools_are_available
    [SPEC_GEN, IPCRAFT_GENERATE].each do |tool|
      assert_path_exists tool, "ENVIRONMENT_GAP: public review tool is missing: #{tool}"
      assert File.executable?(tool), "ENVIRONMENT_GAP: public review tool is not executable: #{tool}"
    end
  end

  def test_sr01_synthetic_package_builds_and_emits_review_metadata
    package_root = write_synthetic_package("phase-synthetic-noc")

    build = spec_gen("build", package_root)
    assert_success build, "REGRESSION: SR-01 synthetic package must build"

    manifest = read_manifest(package_root)
    assert_equal "ipcraft.manifest.v1", manifest.fetch("schema")
    assert_equal PACKAGE_ID, manifest.fetch("id")

    tile = manifest.fetch("modules").find { |mod| mod.fetch("id") == "Tile" }
    endpoint = manifest.fetch("modules").find { |mod| mod.fetch("id") == "Endpoint" }
    refute_nil tile, "REGRESSION: Tile module missing from manifest"
    refute_nil endpoint, "REGRESSION: Endpoint module missing from manifest"

    assert_equal "display_name", tile.fetch("display").fetch("label_parameter")
    assert_equal "external_id", tile.fetch("display").fetch("short_label_parameter")

    fabric_tx = tile.fetch("interfaces").find { |interface| interface.fetch("id") == "fabric_tx" }
    refute_nil fabric_tx, "REGRESSION: non-cardinal fabric interface missing"
    assert_equal({"side" => "east", "opposite" => "fabric_rx"}, fabric_tx.fetch("topology"))

    topology = manifest.fetch("topologies").find { |entry| entry.fetch("id") == "mesh" }
    refute_nil topology, "REGRESSION: mesh topology preset missing"
    assert_equal "Tile", topology.fetch("module")

    generation = manifest.fetch("generation")
    assert_equal "ipcraft.common.v1", generation.fetch("engine")
    assert_equal "manifest.json", generation.fetch("outputs").find { |out| out.fetch("id") == "manifest" }.fetch("path")

    command = manifest.fetch("commands").fetch("generate")
    assert_equal "ipcraft-generate", command.fetch("framework_tool")
    assert_equal "ipcraft.noc.project.v1", command.fetch("input_schema")
  end

  def test_sr02_synthetic_manifest_generation_is_deterministic
    package_root = write_synthetic_package("phase-synthetic-noc")

    first = spec_gen("build", package_root)
    assert_success first, "REGRESSION: SR-02 first synthetic build failed"
    first_manifest = normalized_manifest(package_root)

    second = spec_gen("build", package_root)
    assert_success second, "REGRESSION: SR-02 second synthetic build failed"
    assert_equal first_manifest, normalized_manifest(package_root),
                 "REGRESSION: SR-02 synthetic manifest generation is not deterministic"
  end

  def test_sr03_spec_gen_check_rejects_drifted_synthetic_manifest
    package_root = build_synthetic_package("phase-synthetic-noc")
    manifest_path = File.join(package_root, "ipcraft.json")
    manifest = JSON.parse(File.read(manifest_path))
    manifest["phase_review_drift_probe"] = true
    File.write(manifest_path, JSON.pretty_generate(manifest))

    check = spec_gen("check", package_root)
    refute check.success?, "REGRESSION: SR-03 spec-gen check accepted drifted ipcraft.json\n#{check.details}"
    assert_match(/drift|match|mismatch|out.of.date|ipcraft\.json/i, check.output,
                 "REGRESSION: SR-03 drift diagnostic is not actionable\n#{check.details}")
  end

  def test_sr04_to_sr08_spec_gen_rejects_invalid_synthetic_variants
    invalid_variants = {
      "missing-view-anchor-target" => {
        mutate: ->(options) { options[:tile_anchor_override] = "missing_interface" },
        diagnostic: /anchor|interface|missing_interface|unknown/i
      },
      "missing-topology-opposite" => {
        mutate: ->(options) { options[:fabric_tx_opposite] = "missing_rx" },
        diagnostic: /topology|opposite|missing_rx|interface|unknown/i
      },
      "missing-display-binding-parameter" => {
        mutate: ->(options) { options[:tile_display_label_parameter] = "missing_label" },
        diagnostic: /display|label_parameter|missing_label|parameter|unknown/i
      },
      "unsafe-generation-output-path" => {
        mutate: ->(options) { options[:generation_manifest_path] = "../manifest.json" },
        diagnostic: /generation|path|escape|outside|\.\./i
      },
      "command-has-executable-and-framework-tool" => {
        mutate: ->(options) { options[:include_generate_executable] = true },
        diagnostic: /command|executable|framework_tool|exactly|one|both/i
      },
      "unsupported-framework-tool" => {
        mutate: ->(options) { options[:framework_tool] = "ruby" },
        diagnostic: /framework_tool|ipcraft-generate|unsupported|ruby/i
      }
    }

    failures = []

    invalid_variants.each do |name, config|
      options = {}
      config.fetch(:mutate).call(options)
      package_root = write_synthetic_package(name, **options)

      result = spec_gen("build", package_root)
      if result.success?
        failures << "REGRESSION: #{name} was accepted by spec-gen\n#{result.details}"
        next
      end

      unless result.output.match?(config.fetch(:diagnostic))
        failures << "REGRESSION: #{name} diagnostic was not actionable\n#{result.details}"
      end
    end

    assert_empty failures, failures.join("\n\n")
  end

  def test_sr09_existing_package_manifests_match_specgen_output
    package_roots = %w[
      ipcores/finepaper-noc
      ipcores/opennoc
      ipcores/ravenoc
    ]

    failures = package_roots.filter_map do |relative_root|
      package_root = File.join(REPO_ROOT, relative_root)
      result = run_command(SPEC_GEN,
                           "check",
                           "--ipcore", File.join(package_root, "ipcore.yml"),
                           "--package-root", package_root,
                           chdir: REPO_ROOT)
      next if result.success?

      "REGRESSION: SR-09 #{relative_root} manifest does not match specgen output\n#{result.details}"
    end

    assert_empty failures, failures.join("\n\n")
  end

  def test_gr01_and_gr02_synthetic_generation_works_from_different_cwd
    package_root = build_synthetic_package("phase-synthetic-noc")
    input_path = write_command_input("valid-command-input.json")
    output_dir = File.join(@tmpdir, "generated-synthetic")
    cwd = Dir.mktmpdir("phase-review-cwd-", @tmpdir)

    result = run_command(IPCRAFT_GENERATE,
                         "--manifest", File.join(package_root, "ipcraft.json"),
                         "--input", input_path,
                         "--output", output_dir,
                         chdir: cwd)

    assert_success result, "REGRESSION: GR-01/GR-02 synthetic generation failed"
    output_manifest_path = File.join(output_dir, "manifest.json")
    assert_path_exists output_manifest_path, "REGRESSION: GR-01 output manifest.json missing"

    output_manifest = JSON.parse(File.read(output_manifest_path))
    assert_equal PACKAGE_ID, output_manifest.fetch("ipcore")
    assert_equal "ipcraft.noc.project.v1", output_manifest.fetch("schema")
  end

  def test_gr03_invalid_command_inputs_are_rejected_without_success_manifest
    package_root = build_synthetic_package("phase-synthetic-noc")
    manifest_path = File.join(package_root, "ipcraft.json")
    invalid_inputs = invalid_command_input_mutations

    failures = []

    invalid_inputs.each do |name, mutate|
      input = command_input
      mutate.call(input)
      input_path = File.join(@tmpdir, "#{name}.json")
      File.write(input_path, JSON.pretty_generate(input))
      output_dir = File.join(@tmpdir, "generated-invalid-#{name}")

      result = run_command(IPCRAFT_GENERATE,
                           "--manifest", manifest_path,
                           "--input", input_path,
                           "--output", output_dir)

      if result.success?
        failures << "REGRESSION: GR-03 generator accepted invalid input #{name}\n#{result.details}"
        next
      end

      if File.exist?(File.join(output_dir, "manifest.json"))
        failures << "REGRESSION: GR-03 invalid input #{name} produced success manifest"
      end

      unless result.output.match?(/package|schema|duplicate|module|instance|interface|connection|class|endpoint|graph|mesh|shape|invalid|unknown|unsupported/i)
        failures << "REGRESSION: GR-03 invalid input #{name} diagnostic is not actionable\n#{result.details}"
      end
    end

    assert_empty failures, failures.join("\n\n")
  end

  def test_gr07_failed_generation_does_not_preserve_misleading_success_manifest
    package_root = build_synthetic_package("phase-synthetic-noc")
    manifest_path = File.join(package_root, "ipcraft.json")
    output_dir = File.join(@tmpdir, "generated-reused-output")
    valid_input_path = write_command_input("valid-command-input.json")

    success = run_command(IPCRAFT_GENERATE,
                          "--manifest", manifest_path,
                          "--input", valid_input_path,
                          "--output", output_dir)
    assert_success success, "REGRESSION: GR-07 setup generation failed"
    original_manifest = File.read(File.join(output_dir, "manifest.json"))

    invalid_input = command_input
    invalid_input["schema"] = "ipcraft.noc.project.v0"
    invalid_input_path = File.join(@tmpdir, "invalid-reused-output.json")
    File.write(invalid_input_path, JSON.pretty_generate(invalid_input))

    failure = run_command(IPCRAFT_GENERATE,
                          "--manifest", manifest_path,
                          "--input", invalid_input_path,
                          "--output", output_dir)

    refute failure.success?, "REGRESSION: GR-07 invalid generation unexpectedly succeeded\n#{failure.details}"

    reused_manifest = File.join(output_dir, "manifest.json")
    if File.exist?(reused_manifest)
      refute_equal original_manifest, File.read(reused_manifest),
                   "REGRESSION: GR-07 failed run left a prior success manifest in the requested output directory"
    end
  end

  def test_qt_phase_review_targets_when_requested
    unless ENV["IPCRAFT_PHASE_REVIEW_QT"] == "1"
      skip "Set IPCRAFT_PHASE_REVIEW_QT=1 to run Qt phase review targets."
    end

    targets = %w[
      appsettings_test
      ipcatalogservice_test
      ipcraftmanifest_test
      ipcraft_phase_review_test
      connectionruleservice_test
      topology_preset_test
      ipcoregraphexporter_test
      projectgenerationrunner_test
    ]
    targets << "ipcatalogpanel_test" if ENV["IPCRAFT_PHASE_REVIEW_QT_UI"] == "1"

    build_failures = targets.filter_map do |target|
      result = run_command(XMAKE, "build", "-P", "qt", target, chdir: REPO_ROOT)
      next if result.success?

      "ENVIRONMENT_GAP: Qt target #{target} failed to build\n#{result.details}"
    end
    assert_empty build_failures, build_failures.join("\n\n")

    failures = targets.filter_map do |target|
      result = run_command(XMAKE, "run", "-P", "qt", target, chdir: REPO_ROOT)
      next if result.success?

      "REGRESSION_OR_ENVIRONMENT_GAP: Qt target #{target} failed\n#{result.details}"
    end

    assert_empty failures, failures.join("\n\n")
  end

  private

  def assert_success(result, message)
    assert result.success?, "#{message}\n#{result.details}"
  end

  def run_command(*argv, chdir: @tmpdir)
    stdout, stderr, status = Open3.capture3(*argv, chdir: chdir)
    CommandResult.new(argv: argv, stdout: stdout, stderr: stderr, status: status.exitstatus)
  end

  def spec_gen(command, package_root)
    run_command(SPEC_GEN,
                command,
                "--ipcore", File.join(package_root, "ipcore.yml"),
                "--package-root", package_root)
  end

  def build_synthetic_package(name, **options)
    package_root = write_synthetic_package(name, **options)
    result = spec_gen("build", package_root)
    assert_success result, "REGRESSION: synthetic package #{name} must build before review step"
    package_root
  end

  def read_manifest(package_root)
    JSON.parse(File.read(File.join(package_root, "ipcraft.json")))
  end

  def normalized_manifest(package_root)
    JSON.pretty_generate(read_manifest(package_root))
  end

  def write_synthetic_package(name, **options)
    package_root = File.join(@tmpdir, name)
    views_dir = File.join(package_root, "views")
    FileUtils.mkdir_p(views_dir)

    File.write(File.join(package_root, "ipcore.yml"), synthetic_ipcore_yml(**options))
    File.write(File.join(views_dir, "Tile.xml"), tile_view_xml(options[:tile_anchor_override]))
    File.write(File.join(views_dir, "Endpoint.xml"), endpoint_view_xml)

    package_root
  end

  def synthetic_ipcore_yml(**options)
    tile_display_label_parameter = options.fetch(:tile_display_label_parameter, "display_name")
    fabric_tx_opposite = options.fetch(:fabric_tx_opposite, "fabric_rx")
    generation_manifest_path = options.fetch(:generation_manifest_path, "manifest.json")
    framework_tool = options.fetch(:framework_tool, "ipcraft-generate")
    generate_command_lines = [
      "  generate:",
      (options[:include_generate_executable] ? "    executable: tools/generate" : nil),
      "    framework_tool: #{framework_tool}",
      "    input_schema: ipcraft.noc.project.v1",
      '    args: ["--manifest", "{manifest}", "--input", "{input}", "--output", "{output}"]'
    ].compact.join("\n")

    <<~YAML
      schema: ipcraft.package.v1
      id: #{PACKAGE_ID}
      name: Phase Synthetic NoC
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
          graph_role: host
          display:
            label_parameter: #{tile_display_label_parameter}
            short_label_parameter: external_id
          parameters:
            display_name: { type: string, default: '', emit: editor, label: Display name }
            external_id: { type: string, default: '', emit: editor, label: External ID }
            mesh_col: { type: int, default: 0, configurable: false, emit: attribute }
            mesh_row: { type: int, default: 0, configurable: false, emit: attribute }
          interfaces:
            - id: fabric_tx
              label: Fabric East TX
              modes: [initiator]
              accepts: [{ class: fabric_link, role: initiator }]
              multi_connection: false
              topology: { side: east, opposite: #{fabric_tx_opposite} }
            - id: fabric_rx
              label: Fabric West RX
              modes: [target]
              accepts: [{ class: fabric_link, role: target }]
              multi_connection: false
              topology: { side: west, opposite: fabric_tx }
            - id: vertical_tx
              label: Fabric South TX
              modes: [initiator]
              accepts: [{ class: fabric_link, role: initiator }]
              multi_connection: false
              topology: { side: south, opposite: vertical_rx }
            - id: vertical_rx
              label: Fabric North RX
              modes: [target]
              accepts: [{ class: fabric_link, role: target }]
              multi_connection: false
              topology: { side: north, opposite: vertical_tx }

        - id: Endpoint
          name: Endpoint
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
            path: #{generation_manifest_path}

      commands:
      #{generate_command_lines}
    YAML
  end

  def tile_view_xml(anchor_override)
    first_anchor = anchor_override || "fabric_tx"

    <<~XML
      <?xml version="1.0" encoding="UTF-8"?>
      <module-view schema="v1" module="Tile">
        <graphics layout="mesh_router" node_color="#7cb9e8" supports_collapse="true">
          <expanded min_width="128" height="112" caption_left="28" caption_top="6" port_inset="18" />
          <collapsed min_width="96" height="84" caption_left="28" caption_top="24" endpoint_inset="18" />
          <arrangement endpoint_offset_x="152" mesh_spacing_x="200" mesh_spacing_y="160" />
        </graphics>
        <anchors>
          <anchor ref="#{first_anchor}" x="128" y="56" normal_x="1" normal_y="0" label="Fabric East TX" label_x="94" label_y="56" />
          <anchor ref="fabric_rx" x="0" y="56" normal_x="-1" normal_y="0" label="Fabric West RX" label_x="34" label_y="56" />
          <anchor ref="vertical_tx" x="64" y="112" normal_x="0" normal_y="1" label="Fabric South TX" label_x="64" label_y="94" />
          <anchor ref="vertical_rx" x="64" y="0" normal_x="0" normal_y="-1" label="Fabric North RX" label_x="64" label_y="28" />
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

  def write_command_input(filename, input = command_input)
    path = File.join(@tmpdir, filename)
    File.write(path, JSON.pretty_generate(input))
    path
  end

  def command_input
    {
      "schema" => "ipcraft.noc.project.v1",
      "package" => PACKAGE_ID,
      "package_id" => PACKAGE_ID,
      "project" => {
        "id" => "phase_review_project",
        "name" => "Phase Review Project"
      },
      "ip_instance" => {
        "id" => "noc_0",
        "package_id" => PACKAGE_ID,
        "name" => "Synthetic Review NoC"
      },
      "instances" => [
        tile_instance("tile_0_0", "Alpha Tile", "T00", 0, 0),
        tile_instance("tile_0_1", "Beta Tile", "T01", 0, 1),
        tile_instance("tile_1_0", "Gamma Tile", "T10", 1, 0),
        tile_instance("tile_1_1", "Delta Tile", "T11", 1, 1),
        endpoint_instance("endpoint_0", "DMA 0", "E0")
      ],
      "connections" => [
        connection("tile_0_0_to_tile_0_1", "tile_0_0", "fabric_tx", "tile_0_1", "fabric_rx", "fabric_link"),
        connection("tile_0_0_to_tile_1_0", "tile_0_0", "vertical_tx", "tile_1_0", "vertical_rx", "fabric_link"),
        connection("tile_0_1_to_tile_1_1", "tile_0_1", "vertical_tx", "tile_1_1", "vertical_rx", "fabric_link"),
        connection("tile_1_0_to_tile_1_1", "tile_1_0", "fabric_tx", "tile_1_1", "fabric_rx", "fabric_link"),
        connection("endpoint_0_to_tile_0_0", "endpoint_0", "local_noc", "tile_0_0", "fabric_rx", "endpoint_link")
      ]
    }
  end

  def tile_instance(id, display_name, external_id, row, col)
    {
      "id" => id,
      "module_id" => "Tile",
      "type" => "Tile",
      "parameters" => {
        "display_name" => display_name,
        "external_id" => external_id,
        "mesh_row" => row,
        "mesh_col" => col
      },
      "interfaces" => %w[fabric_tx fabric_rx vertical_tx vertical_rx]
    }
  end

  def endpoint_instance(id, display_name, external_id)
    {
      "id" => id,
      "module_id" => "Endpoint",
      "type" => "Endpoint",
      "parameters" => {
        "display_name" => display_name,
        "external_id" => external_id
      },
      "interfaces" => %w[local_noc]
    }
  end

  def connection(id, source_instance, source_interface, target_instance, target_interface, connection_class)
    {
      "id" => id,
      "class" => connection_class,
      "source" => {
        "instance" => source_instance,
        "interface" => source_interface
      },
      "target" => {
        "instance" => target_instance,
        "interface" => target_interface
      },
      "interfaces" => [
        {
          "instance" => source_instance,
          "interface" => source_interface
        },
        {
          "instance" => target_instance,
          "interface" => target_interface
        }
      ]
    }
  end

  def invalid_command_input_mutations
    {
      "package-mismatch" => ->(input) {
        input["package"] = "phase.synthetic.other"
        input["package_id"] = "phase.synthetic.other"
      },
      "unsupported-schema" => ->(input) {
        input["schema"] = "ipcraft.noc.project.v0"
      },
      "duplicate-instance-id" => ->(input) {
        input.fetch("instances").last["id"] = "tile_0_0"
      },
      "unknown-module-id" => ->(input) {
        input.fetch("instances").first["module_id"] = "MissingTile"
        input.fetch("instances").first["type"] = "MissingTile"
      },
      "unknown-instance-reference" => ->(input) {
        input.fetch("connections").first.fetch("target")["instance"] = "missing_tile"
        input.fetch("connections").first.fetch("interfaces").last["instance"] = "missing_tile"
      },
      "unknown-interface-reference" => ->(input) {
        input.fetch("connections").first.fetch("source")["interface"] = "missing_interface"
        input.fetch("connections").first.fetch("interfaces").first["interface"] = "missing_interface"
      },
      "unknown-connection-class" => ->(input) {
        input.fetch("connections").first["class"] = "ghost_link"
      },
      "invalid-endpoint-shape" => ->(input) {
        input.fetch("connections").first["source"] = {"module" => "tile_0_0", "port" => "fabric_tx"}
      },
      "non-rectangular-mesh" => ->(input) {
        input.fetch("instances").delete_if { |instance| instance["id"] == "tile_1_0" }
      }
    }
  end
end
