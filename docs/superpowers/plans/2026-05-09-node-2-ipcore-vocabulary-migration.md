# Node 2 IP Core Vocabulary Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move concrete IP core source packages from `plugins/` to `ipcores/`, generate committed runtime bundles under `generated/ipcores/`, and reserve `plugins/` for feature plugins.

**Architecture:** `ipcores/<package>/ipcore.yml`, `views/`, `generator/`, and `vendor/` are editable source package assets. `generated/ipcores/<ipcore-id>/plugin.json`, `modules.xml`, and `graphics/` are generated runtime metadata discovered by Qt. Each generated manifest carries `source_root`, and Qt uses that resolved package root as the generator and DRC working directory while loading `modules.xml` and `graphics/` from the generated runtime directory.

**Tech Stack:** Ruby `spec_generator`, Minitest, YAML IP core specs, generated JSON/XML runtime bundles, C++23 Qt plugin registry, xmake.

---

## File Structure

- Create: `ipcores/finepaper-noc/ipcore.yml`
  - New source of truth for `finepaper.noc`; derived from `spec/noc/noc.yaml` plus runtime and topology metadata currently in `plugins/noc/plugin.json`.
- Create: `ipcores/finepaper-noc/views/Endpoint.xml`
  - Source view XML moved from `spec/noc/views/Endpoint.xml`.
- Create: `ipcores/finepaper-noc/views/XP.xml`
  - Source view XML moved from `spec/noc/views/XP.xml`.
- Move/Create: `ipcores/finepaper-noc/generator/**`
  - Ruby NoC generator moved from `plugins/noc/generator/**`.
  - Regenerated Ruby model files remain inside `ipcores/finepaper-noc/generator/src/ruby/model/`.
- Create: `ipcores/ravenoc/ipcore.yml`
  - New source of truth for `finepaper.ravenoc`; derived from `spec/noc/ravenoc.yml` with `schema: finepaper.ipcore.v1` and top-level `id`, `name`, `version`.
- Create: `ipcores/ravenoc/views/RaveEndpoint.xml`
  - Source view XML moved from `spec/noc/views/RaveEndpoint.xml`.
- Create: `ipcores/ravenoc/views/RaveTile.xml`
  - Source view XML moved from `spec/noc/views/RaveTile.xml`.
- Move/Create: `ipcores/ravenoc/generator/**`
  - Ruby RaveNoC generator moved from `plugins/ravenoc/generator/**`.
- Move/Create: `ipcores/ravenoc/vendor/**`
  - RaveNoC vendor submodule moved from `plugins/ravenoc/vendor/**`; update `.gitmodules` with the new submodule path.
- Create/Regenerate: `generated/ipcores/finepaper.noc/**`
  - Runtime manifest, module bundle, and graphics generated from `ipcores/finepaper-noc/ipcore.yml`.
- Create/Regenerate: `generated/ipcores/finepaper.ravenoc/**`
  - Runtime manifest, module bundle, and graphics generated from `ipcores/ravenoc/ipcore.yml`.
- Delete: `plugins/noc/plugin.json`
- Delete: `plugins/noc/modules.xml`
- Delete: `plugins/noc/graphics/Endpoint.xml`
- Delete: `plugins/noc/graphics/XP.xml`
- Delete: `plugins/noc/generator/**`
- Delete: `plugins/ravenoc/plugin.json`
- Delete: `plugins/ravenoc/modules.xml`
- Delete: `plugins/ravenoc/graphics/RaveEndpoint.xml`
- Delete: `plugins/ravenoc/graphics/RaveTile.xml`
- Delete: `plugins/ravenoc/generator/**`
- Delete: `plugins/ravenoc/vendor/**`
- Modify: `.gitmodules`
  - Change the RaveNoC submodule path to `ipcores/ravenoc/vendor/ravenoc`.
- Modify: `spec_generator/lib/spec_generator.rb`
  - Replace the extension schema path with an IP core parser and emitter.
  - Generate runtime bundles under `generated/ipcores/<ipcore-id>/`.
  - Emit `source_root` into `plugin.json`.
  - Update repository drift checking to compare generated runtime roots and generated NoC model files in `ipcores/finepaper-noc/generator/src/ruby/model/`.
- Modify: `spec_generator/bin/spec-gen`
  - Replace `--extension` with `--ipcore`.
  - Make the repository default regenerate both IP cores.
  - Keep `--check` as the generated-runtime drift command.
- Modify: `spec_generator/test/spec_generator_test.rb`
  - Replace extension tests with IP core tests.
  - Add generated-root, `source_root`, CLI, schema rejection, and drift checks.
- Modify: `spec_generator/README.md`
  - Document `ipcores/` as editable source and `generated/ipcores/` as committed runtime metadata.
- Modify: `qt/inc/plugins/plugindescriptor.h`
  - Add `runtimeRootPath` and `sourceRootPath` to `PluginDescriptor`.
- Modify: `qt/src/plugins/pluginregistry.cpp`
  - Parse `source_root`.
  - Resolve manifest-local module and graphics paths against the generated runtime directory.
  - Include repository-local `generated/ipcores/` in default discovery.
- Modify: `qt/test/plugin_test.cpp`
  - Update test names and assertions from plugin package wording to IP core wording.
  - Verify generated repository IP core discovery and generator working directory resolution.
- Modify: `qt/doc/README.md`
  - Use "IP core" for concrete NoC/RaveNoC packages and "feature plugin" only for editor behavior.
- Modify: `qt/doc/architecture.md`
  - Document generated IP core runtime discovery and source-root command execution.

---

## Generator Working Directory Decision

Use this rule in the implementation:

- Generated runtime manifests live in `generated/ipcores/<ipcore-id>/`.
- Generator and vendor assets live in `ipcores/<package>/`.
- Each generated `plugin.json` includes `"source_root": "../../../ipcores/<package>"`.
- `PluginRegistry` resolves `modules` and `graphics` against the generated runtime root.
- `PluginRegistry` resolves `source_root` against the generated runtime root and stores it as `PluginDescriptor::sourceRootPath`.
- `GeneratorRunner` and DRC resolution set `GeneratorCommand::workingDirectory` to `PluginDescriptor::sourceRootPath`.

The expected generated RaveNoC manifest shape is:

```json
{
  "id": "finepaper.ravenoc",
  "name": "RaveNoC",
  "version": "1.0",
  "kind": "noc",
  "source_root": "../../../ipcores/ravenoc",
  "modules": "modules.xml",
  "graphics": "graphics",
  "generator": {
    "command": "ruby",
    "input_format": "generic_graph_v1",
    "args": ["generator/bin/generate", "-i", "{input}", "-o", "{output}", "-t", "generator/template"]
  },
  "drc": {
    "command": "ruby",
    "input_format": "generic_graph_v1",
    "args": ["generator/bin/drc", "-i", "{input}"]
  }
}
```

When Qt runs that command, the process working directory is `ipcores/ravenoc`, so `generator/bin/generate`, `generator/template`, and the generator default `vendor/ravenoc` path resolve inside the source package.

---

### Task 1: Add Failing Spec Generator Tests For IP Core Sources And Generated Roots

**Files:**
- Modify: `spec_generator/test/spec_generator_test.rb`

- [x] **Step 1: Rename extension fixture helpers to IP core language**

Rename these helper and test names in `spec_generator/test/spec_generator_test.rb`:

```ruby
ravenoc_extension_yaml -> ravenoc_ipcore_yaml
test_generates_ravenoc_extension_runtime_bundle -> test_generates_ravenoc_ipcore_runtime_bundle
test_rejects_extension_instance_parameter_without_default -> test_rejects_ipcore_instance_parameter_without_default
test_rejects_extension_kind_outside_noc -> test_rejects_ipcore_kind_outside_noc
test_rejects_extension_interface_metadata_that_is_not_a_string -> test_rejects_ipcore_interface_metadata_that_is_not_a_string
test_rejects_extension_topology_rule_outside_known_values -> test_rejects_ipcore_topology_rule_outside_known_values
test_rejects_extension_view_refs_missing_from_spec -> test_rejects_ipcore_view_refs_missing_from_spec
test_cli_generates_extension_bundle -> test_cli_generates_ipcore_bundle
```

Expected temporary failure after this step: test code still calls `SpecGenerator.generate_extension` and still uses `schema: finepaper.extension.v1`.

- [x] **Step 2: Replace the RaveNoC fixture schema and metadata shape**

In the renamed `ravenoc_ipcore_yaml` helper, replace the header:

```yaml
schema: finepaper.extension.v1
kind: noc
extension:
  id: finepaper.ravenoc
  name: RaveNoC
  version: '1.0'
```

with:

```yaml
schema: finepaper.ipcore.v1
id: finepaper.ravenoc
name: RaveNoC
version: '1.0'
kind: noc
```

Expected temporary failure: the parser rejects `finepaper.ipcore.v1` and unknown top-level `id`.

- [x] **Step 3: Add a Finepaper NoC IP core fixture helper**

Add this helper in the private section near `valid_spec_yaml`:

```ruby
  def finepaper_noc_ipcore_yaml
    valid_spec_yaml.sub(
      "schema: v1\nkind: noc-definition\nname: NoC\nversion: '1.0'\n",
      <<~YAML
        schema: finepaper.ipcore.v1
        id: finepaper.noc
        name: NoC
        version: '1.0'
        kind: noc
        runtime:
          generator:
            command: ruby
            input_format: generic_graph_v1
            args:
              - generator/bin/generate
              - -i
              - "{input}"
              - -o
              - "{output}"
              - -t
              - generator/template
          drc:
            command: ruby
            input_format: generic_graph_v1
            args:
              - generator/bin/drc
              - -i
              - "{input}"
        topology_presets:
          - id: mesh
            label: Mesh
            kind: mesh
            router_module: XP
            id_pattern: xp_{row}_{col}
            ports: { east: east, west: west, north: north, south: south }
            parameters:
              rows: { label: Rows, default: 2, min: 1, max: 16 }
              cols: { label: Columns, default: 2, min: 1, max: 16 }
          - id: ring
            label: Ring
            kind: ring
            router_module: XP
            id_pattern: xp_{index}
            ports: { east: east, west: west }
            parameters:
              nodes: { label: Nodes, default: 4, min: 2, max: 64 }
      YAML
    )
  end
```

Expected temporary failure: no test calls this helper yet.

- [x] **Step 4: Add a generated Finepaper NoC runtime test**

Add this test after `test_generates_qt_bundle_graphics_and_ruby_models`:

```ruby
  def test_generates_finepaper_noc_ipcore_runtime_bundle
    Dir.mktmpdir do |dir|
      ipcore_path = write_file(dir, 'ipcores/finepaper-noc/ipcore.yml', finepaper_noc_ipcore_yaml)
      write_file(dir, 'ipcores/finepaper-noc/views/XP.xml', xp_view_xml)
      write_file(dir, 'ipcores/finepaper-noc/views/Endpoint.xml', endpoint_view_xml)

      SpecGenerator.generate_ipcore(
        ipcore_path: ipcore_path,
        views_dir: File.join(dir, 'ipcores/finepaper-noc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.noc'),
        ruby_model_dir: File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model')
      )

      plugin_json = JSON.parse(File.read(File.join(dir, 'generated/ipcores/finepaper.noc/plugin.json')))
      assert_equal 'finepaper.noc', plugin_json.fetch('id')
      assert_equal 'NoC', plugin_json.fetch('name')
      assert_equal '1.0', plugin_json.fetch('version')
      assert_equal 'noc', plugin_json.fetch('kind')
      assert_equal '../../../ipcores/finepaper-noc', plugin_json.fetch('source_root')
      assert_equal 'modules.xml', plugin_json.fetch('modules')
      assert_equal 'graphics', plugin_json.fetch('graphics')
      assert_equal 'ruby', plugin_json.fetch('generator').fetch('command')
      assert_equal 'generic_graph_v1', plugin_json.fetch('generator').fetch('input_format')
      assert_equal 'generator/bin/generate', plugin_json.fetch('generator').fetch('args').first
      assert_equal 'ruby', plugin_json.fetch('drc').fetch('command')
      assert_equal 'generic_graph_v1', plugin_json.fetch('drc').fetch('input_format')
      assert_equal 'generator/bin/drc', plugin_json.fetch('drc').fetch('args').first
      assert_equal 2, plugin_json.fetch('topology_presets').size

      modules_xml = File.read(File.join(dir, 'generated/ipcores/finepaper.noc/modules.xml'))
      assert_includes modules_xml, '<bus name="ni_link"'
      assert_includes modules_xml, '<module name="XP"'
      assert_includes modules_xml, '<module name="Endpoint"'
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.noc/graphics/XP.xml'))
      assert File.file?(File.join(dir, 'generated/ipcores/finepaper.noc/graphics/Endpoint.xml'))
      assert File.file?(File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model/xp.rb'))
      assert File.file?(File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model/endpoint.rb'))
    end
  end
```

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL with `undefined method 'generate_ipcore' for SpecGenerator:Module`.

- [x] **Step 5: Update the RaveNoC runtime test to use generated roots**

Inside `test_generates_ravenoc_ipcore_runtime_bundle`, replace the call and output paths with:

```ruby
      SpecGenerator.generate_ipcore(
        ipcore_path: ipcore_path,
        views_dir: File.join(dir, 'ipcores/ravenoc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.ravenoc')
      )

      plugin_json = JSON.parse(File.read(File.join(dir, 'generated/ipcores/finepaper.ravenoc/plugin.json')))
      assert_equal 'finepaper.ravenoc', plugin_json.fetch('id')
      assert_equal 'RaveNoC', plugin_json.fetch('name')
      assert_equal '1.0', plugin_json.fetch('version')
      assert_equal 'noc', plugin_json.fetch('kind')
      assert_equal '../../../ipcores/ravenoc', plugin_json.fetch('source_root')
```

Update every remaining path in that test from `plugins/ravenoc` to `generated/ipcores/finepaper.ravenoc`.

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL because `generate_ipcore` is not implemented.

- [x] **Step 6: Add schema rejection for the removed extension schema**

Add this test after `test_rejects_ipcore_kind_outside_noc`:

```ruby
  def test_rejects_removed_extension_schema
    Dir.mktmpdir do |dir|
      legacy_schema = ['finepaper', 'extension', 'v1'].join('.')
      yaml = ravenoc_ipcore_yaml.sub('schema: finepaper.ipcore.v1', "schema: #{legacy_schema}")
      ipcore_path = write_file(dir, 'ipcores/ravenoc/ipcore.yml', yaml)
      write_file(dir, 'ipcores/ravenoc/views/RaveTile.xml', rave_tile_view_xml)
      write_file(dir, 'ipcores/ravenoc/views/RaveEndpoint.xml', rave_endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate_ipcore(
          ipcore_path: ipcore_path,
          views_dir: File.join(dir, 'ipcores/ravenoc/views'),
          runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.ravenoc')
        )
      end

      assert_match(/schema must be finepaper\.ipcore\.v1/, error.message)
    end
  end
```

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL because `generate_ipcore` is not implemented.

---

### Task 2: Implement `finepaper.ipcore.v1` In The Spec Generator

**Files:**
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `spec_generator/test/spec_generator_test.rb`

- [x] **Step 1: Replace extension constants with IP core constants**

In `spec_generator/lib/spec_generator.rb`, replace the extension constants with:

```ruby
  IPCORE_TOP_LEVEL_KEYS = %w[
    schema id name version kind buses runtime topology_presets instance_parameters modules
  ].freeze
  RUNTIME_KEYS = %w[generator drc].freeze
  COMMAND_KEYS = %w[command input_format args].freeze
  TOPOLOGY_PRESET_KEYS = %w[id label kind router_module id_pattern ports parameters].freeze
  TOPOLOGY_PRESET_PARAMETER_KEYS = %w[label default min max].freeze
  IPCORE_MODULE_KEYS = %w[
    palette_label graph_group description identity capabilities interface_limits parameters interfaces
  ].freeze
  IPCORE_INTERFACE_KEYS = %w[
    label bus role connects_to match accepts config cardinality autocomplete_group topology_rule port ports
  ].freeze
  GENERATED_OUTPUT_ROOTS = [
    ['generated/ipcores/finepaper.noc/plugin.json', :file],
    ['generated/ipcores/finepaper.noc/modules.xml', :file],
    ['generated/ipcores/finepaper.noc/graphics', :directory],
    ['ipcores/finepaper-noc/generator/src/ruby/model', :generated_files],
    ['generated/ipcores/finepaper.ravenoc/plugin.json', :file],
    ['generated/ipcores/finepaper.ravenoc/modules.xml', :file],
    ['generated/ipcores/finepaper.ravenoc/graphics', :directory]
  ].freeze
```

Remove `EXTENSION_TOP_LEVEL_KEYS`, `EXTENSION_KEYS`, `EXTENSION_MODULE_KEYS`, and `EXTENSION_INTERFACE_KEYS`.

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL from unresolved extension constant references.

- [x] **Step 2: Add `SpecGenerator.generate_ipcore`**

Replace:

```ruby
  def self.generate_extension(extension_path:, views_dir:, bundle_dir:)
    parsed = ExtensionParser.new(extension_path, views_dir).parse
    ExtensionBundleEmitter.new(parsed).write(bundle_dir)
  end
```

with:

```ruby
  def self.generate_ipcore(ipcore_path:, views_dir:, runtime_bundle_dir:, ruby_model_dir: nil)
    parsed = IpCoreParser.new(ipcore_path, views_dir).parse
    IpCoreRuntimeEmitter.new(parsed, source_root: File.dirname(ipcore_path)).write(runtime_bundle_dir)
    RubyModelEmitter.new(parsed.data).write(ruby_model_dir) if ruby_model_dir
  end
```

Keep `SpecGenerator.generate` only until all tests are migrated in this task. Delete it after repository defaults call `generate_ipcore`.

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL with `uninitialized constant SpecGenerator::IpCoreParser`.

- [x] **Step 3: Replace `ExtensionParser` with `IpCoreParser`**

Rename `class ExtensionParser` to `class IpCoreParser` and update these methods:

```ruby
    def initialize(ipcore_path, views_dir)
      @ipcore_path = ipcore_path
      @views_dir = views_dir
    end
```

```ruby
    def load_yaml
      YAML.safe_load(File.read(@ipcore_path), aliases: false).tap do |data|
        raise SpecError, 'IP core spec root must be a map' unless data.is_a?(Hash)
      end
    rescue Psych::Exception => error
      raise SpecError, "Invalid YAML: #{error.message}"
    end
```

```ruby
    def validate_top_level(data)
      validate_keys!(data, IPCORE_TOP_LEVEL_KEYS, 'IP core top-level')
      raise SpecError, 'schema must be finepaper.ipcore.v1' unless data['schema'] == 'finepaper.ipcore.v1'
      %w[id name version kind].each do |key|
        raise SpecError, "#{key} must be a string" unless data[key].is_a?(String)
      end
      raise SpecError, 'kind must be noc' unless data['kind'] == 'noc'
      raise SpecError, 'runtime must be a map' unless data['runtime'].is_a?(Hash)
      raise SpecError, 'modules must be a map' unless data['modules'].is_a?(Hash)
      validate_buses(data.fetch('buses', {})) if data.key?('buses')
    end
```

Update `parse` to remove `validate_extension(data.fetch('extension'))`.

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL from module/interface validation still referencing removed extension constants.

- [x] **Step 4: Use one IP core module/interface validator**

In `IpCoreParser#validate_modules`, use:

```ruby
        validate_keys!(mod, IPCORE_MODULE_KEYS, "module #{module_name}")
```

In `IpCoreParser#validate_interfaces`, use:

```ruby
        validate_keys!(interface, IPCORE_INTERFACE_KEYS, "module #{module_name} interface #{interface_name}")
        %w[label bus role].each do |key|
          raise SpecError, "#{module_name}.#{interface_name} #{key} must be a string" unless interface[key].is_a?(String)
        end
        if interface.key?('connects_to')
          raise SpecError, "#{module_name}.#{interface_name} connects_to must be a string" unless interface['connects_to'].is_a?(String)
        end
        if interface.key?('match')
          raise SpecError, "#{module_name}.#{interface_name} match must be a list" unless interface['match'].is_a?(Array)
          interface['match'].each do |field|
            raise SpecError, "#{module_name}.#{interface_name} match entries must be strings" unless field.is_a?(String)
          end
        end
```

For IP cores with `buses`, derive `connects_to` and `match` in the emitter from bus compatibility, matching current Finepaper NoC behavior. For IP cores without `buses`, require `connects_to` and `match` in the YAML, matching current RaveNoC behavior.

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL from `ExtensionBundleEmitter` references.

- [x] **Step 5: Replace `ExtensionBundleEmitter` with `IpCoreRuntimeEmitter`**

Rename `ExtensionBundleEmitter` to `IpCoreRuntimeEmitter`. Change the initializer to:

```ruby
    def initialize(parsed, source_root:)
      @spec = parsed.data
      @views = parsed.views
      @source_root = File.expand_path(source_root)
    end
```

Change `write` to:

```ruby
    def write(bundle_dir)
      @bundle_dir = File.expand_path(bundle_dir)
      FileUtils.mkdir_p(File.join(bundle_dir, 'graphics'))
      File.write(File.join(bundle_dir, 'plugin.json'), plugin_json)
      File.write(File.join(bundle_dir, 'modules.xml'), modules_xml)

      @views.each do |module_name, view|
        File.write(
          File.join(bundle_dir, 'graphics', "#{module_name}.xml"),
          graphics_xml(module_name, view)
        )
      end
    end
```

Change `plugin_json` to read metadata from top-level IP core keys:

```ruby
    def plugin_json
      runtime = @spec.fetch('runtime')
      generator = runtime.fetch('generator')
      drc = runtime.fetch('drc')
      JSON.pretty_generate(
        {
          id: @spec.fetch('id'),
          name: @spec.fetch('name'),
          version: @spec.fetch('version'),
          kind: @spec.fetch('kind'),
          source_root: source_root_relative_to_bundle,
          instance_parameters: @spec.fetch('instance_parameters', {}),
          modules: 'modules.xml',
          graphics: 'graphics',
          generator: {
            command: generator.fetch('command'),
            input_format: generator.fetch('input_format'),
            args: generator.fetch('args')
          },
          drc: {
            command: drc.fetch('command'),
            input_format: drc.fetch('input_format'),
            args: drc.fetch('args')
          },
          topology_presets: @spec.fetch('topology_presets', []),
          native: {
            enabled: false,
            library: ''
          }
        }
      ) + "\n"
    end

    def source_root_relative_to_bundle
      Pathname.new(@source_root).relative_path_from(Pathname.new(@bundle_dir)).to_s
    end
```

Add `require 'pathname'` at the top of the file.

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL until all old extension call sites in tests and CLI are replaced.

- [x] **Step 6: Update repository drift generation paths**

In `check_repository_generated_outputs`, replace the old `plugins/` generation calls with:

```ruby
      generate_ipcore(
        ipcore_path: File.join(root, 'ipcores/finepaper-noc/ipcore.yml'),
        views_dir: File.join(root, 'ipcores/finepaper-noc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.noc'),
        ruby_model_dir: File.join(dir, 'ipcores/finepaper-noc/generator/src/ruby/model')
      )

      generate_ipcore(
        ipcore_path: File.join(root, 'ipcores/ravenoc/ipcore.yml'),
        views_dir: File.join(root, 'ipcores/ravenoc/views'),
        runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.ravenoc')
      )
```

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL from fixture repository helper paths until tests are updated.

- [x] **Step 7: Update CLI options**

In `spec_generator/bin/spec-gen`, use these defaults:

```ruby
options = {
  check: false,
  ipcore_path: nil,
  views_dir: nil,
  runtime_bundle_dir: nil,
  ruby_model_dir: nil
}
```

Replace `--extension`, `--qt-bundle`, and `--bundle` handling with:

```ruby
    parser.on('--ipcore PATH', 'IP core YAML path') { |value| options[:ipcore_path] = value }
    parser.on('--views DIR', 'IP core view XML directory') { |value| options[:views_dir] = value }
    parser.on('--runtime-bundle DIR', 'Generated runtime bundle output directory') { |value| options[:runtime_bundle_dir] = value }
    parser.on('--ruby-model DIR', 'Generated Ruby model output directory') { |value| options[:ruby_model_dir] = value }
```

Add this helper above the `begin` block:

```ruby
def generate_repository_ipcores
  SpecGenerator.generate_ipcore(
    ipcore_path: 'ipcores/finepaper-noc/ipcore.yml',
    views_dir: 'ipcores/finepaper-noc/views',
    runtime_bundle_dir: 'generated/ipcores/finepaper.noc',
    ruby_model_dir: 'ipcores/finepaper-noc/generator/src/ruby/model'
  )
  SpecGenerator.generate_ipcore(
    ipcore_path: 'ipcores/ravenoc/ipcore.yml',
    views_dir: 'ipcores/ravenoc/views',
    runtime_bundle_dir: 'generated/ipcores/finepaper.ravenoc'
  )
end
```

Use this dispatch:

```ruby
  if options[:check]
    SpecGenerator.check_repository_generated_outputs(root: Dir.pwd)
    puts 'Generated IP core runtime artifacts are up to date'
  elsif options[:ipcore_path]
    raise SpecGenerator::SpecError, '--runtime-bundle is required with --ipcore' unless options[:runtime_bundle_dir]
    views_dir = options[:views_dir] || File.join(File.dirname(options[:ipcore_path]), 'views')
    SpecGenerator.generate_ipcore(
      ipcore_path: options[:ipcore_path],
      views_dir: views_dir,
      runtime_bundle_dir: options[:runtime_bundle_dir],
      ruby_model_dir: options[:ruby_model_dir]
    )
    puts "Generated IP core runtime bundle #{options[:runtime_bundle_dir]}"
  else
    generate_repository_ipcores
    puts 'Generated repository IP core runtime bundles'
  end
```

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL until CLI tests assert the new output strings and paths.

- [x] **Step 8: Update spec generator tests to call `generate_ipcore`**

Replace all remaining `SpecGenerator.generate_extension` calls with `SpecGenerator.generate_ipcore`, using these argument names:

```ruby
ipcore_path:
views_dir:
runtime_bundle_dir:
ruby_model_dir:
```

Update CLI tests:

```ruby
        '--ipcore', ipcore_path,
        '--views', File.join(dir, 'ipcores/ravenoc/views'),
        '--runtime-bundle', File.join(dir, 'generated/ipcores/finepaper.ravenoc')
```

Update expected CLI stdout:

```ruby
assert_includes stdout, 'Generated IP core runtime bundle'
assert_includes stdout, 'Generated IP core runtime artifacts are up to date'
```

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: tests still fail if any old `generate_extension` call remains.

- [x] **Step 9: Migrate base NoC tests off `SpecGenerator.generate`**

Update the old base NoC tests so no test directly parses `schema: v1` as a runtime source format:

```text
test_generates_qt_bundle_graphics_and_ruby_models
test_generates_interface_anchor_bundle_for_renamed_noc_modules
test_rejects_unknown_top_level_fields
test_rejects_interface_accepts_values_outside_bus_enum
test_rejects_base_interface_metadata_that_is_not_a_string
test_rejects_base_interface_cardinality_outside_known_values
test_rejects_view_interface_refs_missing_from_spec
test_cli_generates_outputs
```

For each test:

- write `ipcores/finepaper-noc/ipcore.yml` instead of `spec/noc.yaml`;
- write views under `ipcores/finepaper-noc/views`;
- call `SpecGenerator.generate_ipcore` instead of `SpecGenerator.generate`;
- read generated Qt runtime metadata under `generated/ipcores/finepaper.noc`;
- write generated Ruby models under `ipcores/finepaper-noc/generator/src/ruby/model`.

Use `finepaper_noc_ipcore_yaml` for the standard NoC fixture. For `test_generates_interface_anchor_bundle_for_renamed_noc_modules`, add this helper near `renamed_interface_anchor_spec_yaml`:

```ruby
  def renamed_interface_anchor_ipcore_yaml
    renamed_interface_anchor_spec_yaml.sub(
      "schema: v1\nkind: noc-definition\nname: NoC\nversion: '1.0'\n",
      <<~YAML
        schema: finepaper.ipcore.v1
        id: finepaper.renamed-noc
        name: Renamed NoC
        version: '1.0'
        kind: noc
        runtime:
          generator:
            command: ruby
            input_format: generic_graph_v1
            args:
              - generator/bin/generate
          drc:
            command: ruby
            input_format: generic_graph_v1
            args:
              - generator/bin/drc
      YAML
    )
  end
```

Run:

```bash
rg -n 'SpecGenerator\.generate\(|schema: finepaper\.extension\.v1|generate_extension' spec_generator/test/spec_generator_test.rb
ruby spec_generator/test/spec_generator_test.rb
```

Expected: `rg` has no output. Tests pass for temp fixtures, while repository drift checks still fail until Task 3 creates the repository `ipcores/` and `generated/ipcores/` paths.

- [x] **Step 10: Remove legacy generator entry points**

After the tests no longer call `SpecGenerator.generate`, delete the old base-only parser/emitter entry point:

- remove `SpecGenerator.generate`;
- remove `Parser` if it is no longer referenced;
- keep or merge `QtBundleEmitter` and `RubyModelEmitter` only as implementation helpers behind `generate_ipcore`;
- remove old CLI options `--spec` and `--qt-bundle`.

Run:

```bash
rg -n 'SpecGenerator\.generate\(|schema: v1|noc-definition|--spec|--qt-bundle|generate_extension|ExtensionParser|ExtensionBundleEmitter' spec_generator
ruby spec_generator/test/spec_generator_test.rb
```

Expected: `rg` has no live-code hits except fixture helper strings still used to derive `finepaper.ipcore.v1` test fixtures and the split-string `legacy_schema` negative test. Tests pass for temp fixtures, while repository drift checks still fail until Task 3 creates repository paths.

---

### Task 3: Move Source Packages And Regenerate Runtime Artifacts

**Files:**
- Create: `ipcores/finepaper-noc/ipcore.yml`
- Create: `ipcores/finepaper-noc/views/Endpoint.xml`
- Create: `ipcores/finepaper-noc/views/XP.xml`
- Move/Create: `ipcores/finepaper-noc/generator/**`
- Create: `ipcores/ravenoc/ipcore.yml`
- Create: `ipcores/ravenoc/views/RaveEndpoint.xml`
- Create: `ipcores/ravenoc/views/RaveTile.xml`
- Move/Create: `ipcores/ravenoc/generator/**`
- Move/Create: `ipcores/ravenoc/vendor/**`
- Create/Regenerate: `generated/ipcores/finepaper.noc/**`
- Create/Regenerate: `generated/ipcores/finepaper.ravenoc/**`
- Delete: `plugins/noc/**`
- Delete: `plugins/ravenoc/**`
- Modify: `.gitmodules`

- [x] **Step 1: Move Finepaper NoC source files**

Run:

```bash
mkdir -p ipcores/finepaper-noc/views
git mv spec/noc/noc.yaml ipcores/finepaper-noc/ipcore.yml
git mv spec/noc/views/Endpoint.xml ipcores/finepaper-noc/views/Endpoint.xml
git mv spec/noc/views/XP.xml ipcores/finepaper-noc/views/XP.xml
git mv plugins/noc/generator ipcores/finepaper-noc/generator
```

Expected: files move cleanly. `plugins/noc/plugin.json`, `plugins/noc/modules.xml`, and `plugins/noc/graphics/` remain until generated output is produced.

- [x] **Step 2: Edit `ipcores/finepaper-noc/ipcore.yml` header**

Replace the first four lines:

```yaml
schema: v1
kind: noc-definition
name: NoC
version: '1.0'
```

with:

```yaml
schema: finepaper.ipcore.v1
id: finepaper.noc
name: NoC
version: '1.0'
kind: noc
runtime:
  generator:
    command: ruby
    input_format: generic_graph_v1
    args:
      - generator/bin/generate
      - -i
      - "{input}"
      - -o
      - "{output}"
      - -t
      - generator/template
  drc:
    command: ruby
    input_format: generic_graph_v1
    args:
      - generator/bin/drc
      - -i
      - "{input}"
topology_presets:
  - id: mesh
    label: Mesh
    kind: mesh
    router_module: XP
    id_pattern: xp_{row}_{col}
    ports: { east: east, west: west, north: north, south: south }
    parameters:
      rows: { label: Rows, default: 2, min: 1, max: 16 }
      cols: { label: Columns, default: 2, min: 1, max: 16 }
  - id: ring
    label: Ring
    kind: ring
    router_module: XP
    id_pattern: xp_{index}
    ports: { east: east, west: west }
    parameters:
      nodes: { label: Nodes, default: 4, min: 2, max: 64 }
```

Leave the existing `buses:` and `modules:` sections below this inserted block.

Run:

```bash
ruby spec_generator/bin/spec-gen \
  --ipcore ipcores/finepaper-noc/ipcore.yml \
  --views ipcores/finepaper-noc/views \
  --runtime-bundle generated/ipcores/finepaper.noc \
  --ruby-model ipcores/finepaper-noc/generator/src/ruby/model
```

Expected: PASS and stdout contains `Generated IP core runtime bundle generated/ipcores/finepaper.noc`.

- [x] **Step 3: Move RaveNoC source files**

Run:

```bash
mkdir -p ipcores/ravenoc/views ipcores/ravenoc/vendor
git mv spec/noc/ravenoc.yml ipcores/ravenoc/ipcore.yml
git mv spec/noc/views/RaveEndpoint.xml ipcores/ravenoc/views/RaveEndpoint.xml
git mv spec/noc/views/RaveTile.xml ipcores/ravenoc/views/RaveTile.xml
git mv plugins/ravenoc/generator ipcores/ravenoc/generator
git mv plugins/ravenoc/vendor/ravenoc ipcores/ravenoc/vendor/ravenoc
```

Expected: files move cleanly. If `spec/noc/views` and `spec/noc` are empty after the moves, remove the empty directories with `rmdir spec/noc/views spec/noc`.

- [x] **Step 4: Update RaveNoC submodule path**

Edit `.gitmodules` from:

```ini
[submodule "plugins/ravenoc/vendor/ravenoc"]
	path = plugins/ravenoc/vendor/ravenoc
	url = https://github.com/aignacio/ravenoc.git
```

to:

```ini
[submodule "ipcores/ravenoc/vendor/ravenoc"]
	path = ipcores/ravenoc/vendor/ravenoc
	url = https://github.com/aignacio/ravenoc.git
```

Run:

```bash
git submodule sync -- ipcores/ravenoc/vendor/ravenoc
git status --short
```

Expected: status shows the vendor submodule as moved, and no `.gitmodules` entry references `plugins/ravenoc/vendor/ravenoc`.

- [x] **Step 5: Edit `ipcores/ravenoc/ipcore.yml` header**

Replace:

```yaml
schema: finepaper.extension.v1
kind: noc
extension:
  id: finepaper.ravenoc
  name: RaveNoC
  version: '1.0'
```

with:

```yaml
schema: finepaper.ipcore.v1
id: finepaper.ravenoc
name: RaveNoC
version: '1.0'
kind: noc
```

Run:

```bash
ruby spec_generator/bin/spec-gen \
  --ipcore ipcores/ravenoc/ipcore.yml \
  --views ipcores/ravenoc/views \
  --runtime-bundle generated/ipcores/finepaper.ravenoc
```

Expected: PASS and stdout contains `Generated IP core runtime bundle generated/ipcores/finepaper.ravenoc`.

- [x] **Step 6: Remove old generated runtime files from `plugins/`**

Run:

```bash
git rm plugins/noc/plugin.json \
  plugins/noc/modules.xml \
  plugins/noc/graphics/Endpoint.xml \
  plugins/noc/graphics/XP.xml \
  plugins/ravenoc/plugin.json \
  plugins/ravenoc/modules.xml \
  plugins/ravenoc/graphics/RaveEndpoint.xml \
  plugins/ravenoc/graphics/RaveTile.xml
rmdir plugins/noc/graphics plugins/noc \
  plugins/ravenoc/graphics plugins/ravenoc/vendor plugins/ravenoc \
  plugins 2>/dev/null || true
```

Expected: `plugins/` is absent or contains only feature-plugin files. It must not contain `noc/` or `ravenoc/` concrete IP core packages.

- [x] **Step 7: Regenerate all repository IP core outputs**

Run:

```bash
ruby spec_generator/bin/spec-gen
```

Expected:

- stdout contains `Generated repository IP core runtime bundles`.
- `generated/ipcores/finepaper.noc/plugin.json` exists.
- `generated/ipcores/finepaper.noc/modules.xml` exists.
- `generated/ipcores/finepaper.noc/graphics/Endpoint.xml` exists.
- `generated/ipcores/finepaper.noc/graphics/XP.xml` exists.
- `generated/ipcores/finepaper.ravenoc/plugin.json` exists.
- `generated/ipcores/finepaper.ravenoc/modules.xml` exists.
- `generated/ipcores/finepaper.ravenoc/graphics/RaveEndpoint.xml` exists.
- `generated/ipcores/finepaper.ravenoc/graphics/RaveTile.xml` exists.

- [x] **Step 8: Run source migration checks**

Run:

```bash
test -f ipcores/finepaper-noc/generator/test/test_generator.rb
test -f ipcores/ravenoc/generator/test/test_generator.rb
test -f ipcores/ravenoc/generator/test/test_smoke.rb
test -f ipcores/ravenoc/vendor/ravenoc/ravenoc.core
test ! -e plugins/noc
test ! -e plugins/ravenoc
rg -n 'finepaper\.extension\.v1|plugins/noc|plugins/ravenoc' spec_generator spec ipcores generated .gitmodules
```

Expected: all `test` commands pass, and `rg` has no output.

---

### Task 4: Update Qt Discovery And Generator Working Directory

**Files:**
- Modify: `qt/inc/plugins/plugindescriptor.h`
- Modify: `qt/src/plugins/pluginregistry.cpp`
- Modify: `qt/test/plugin_test.cpp`

- [x] **Step 1: Add failing manifest source-root test**

In `qt/test/plugin_test.cpp`, rename `testPluginManifestLoadsRelativePaths` to `testIpCoreManifestLoadsRuntimeAndSourcePaths`.

Inside its JSON fixture, add:

```json
"source_root": "../ipcores/demo",
```

Create the source directory before discovery:

```cpp
    require(root.mkpath(QStringLiteral("ipcores/demo/generator")), "failed to create source dirs");
```

After discovery, add:

```cpp
    require(plugins.first().runtimeRootPath == QFileInfo(root.filePath(QStringLiteral("demo"))).absoluteFilePath(),
            "runtime root should be the generated manifest directory");
    require(plugins.first().sourceRootPath == QFileInfo(root.filePath(QStringLiteral("ipcores/demo"))).absoluteFilePath(),
            "source root should resolve from source_root");
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: FAIL because `PluginDescriptor` has no `runtimeRootPath` or `sourceRootPath`.

- [x] **Step 2: Add descriptor paths**

In `qt/inc/plugins/plugindescriptor.h`, add these fields to `PluginDescriptor`:

```cpp
    QString runtimeRootPath;
    QString sourceRootPath;
```

Keep `rootPath` for existing code during this node, but set it to the source root after manifest loading so existing generator code continues to use the package root.

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: FAIL because the registry does not populate the new fields.

- [x] **Step 3: Resolve manifest runtime paths and source root**

In `qt/src/plugins/pluginregistry.cpp`, inside `loadManifest`, replace:

```cpp
    descriptor.rootPath = QFileInfo(pluginDirectory).absoluteFilePath();
    descriptor.modulesPath = resolvePath(descriptor.rootPath, object.value(QStringLiteral("modules")).toString());
    descriptor.graphicsPath = resolvePath(descriptor.rootPath, object.value(QStringLiteral("graphics")).toString());
```

with:

```cpp
    descriptor.runtimeRootPath = QFileInfo(pluginDirectory).absoluteFilePath();
    const QString sourceRoot = object.value(QStringLiteral("source_root")).toString().trimmed();
    if (sourceRoot.isEmpty()) {
        qWarning() << "Skipping plugin manifest without source_root" << manifestInfo.absoluteFilePath();
        return std::nullopt;
    }
    descriptor.sourceRootPath = resolvePath(descriptor.runtimeRootPath, sourceRoot);
    descriptor.rootPath = descriptor.sourceRootPath;
    descriptor.modulesPath = resolvePath(descriptor.runtimeRootPath, object.value(QStringLiteral("modules")).toString());
    descriptor.graphicsPath = resolvePath(descriptor.runtimeRootPath, object.value(QStringLiteral("graphics")).toString());
```

`source_root` is mandatory for runtime manifests in this node. Do not retain a fallback for source-root-less pre-v1 concrete IP manifests.

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: PASS for the updated source-root test; repository discovery tests still fail until paths are updated.

- [x] **Step 4: Discover repository generated IP core runtime roots**

In `qt/src/plugins/pluginregistry.cpp`, rename `appendLocalPluginRootsFrom` to `appendLocalRuntimeRootsFrom`.

Inside the ancestor loop, append both runtime roots:

```cpp
        const QString pluginCandidate = dir.filePath(QStringLiteral("plugins"));
        if (QFileInfo(pluginCandidate).isDir()) {
            appendUniquePath(roots, pluginCandidate);
        }

        const QString ipcoreCandidate = dir.filePath(QStringLiteral("generated/ipcores"));
        if (QFileInfo(ipcoreCandidate).isDir()) {
            appendUniquePath(roots, ipcoreCandidate);
        }
```

Update callers in `defaultPluginRoots()`:

```cpp
    appendLocalRuntimeRootsFrom(roots, QDir::currentPath());
    appendLocalRuntimeRootsFrom(roots, QCoreApplication::applicationDirPath());
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: repository generated IP core tests still need path updates, but default discovery includes `generated/ipcores` when that directory exists.

- [x] **Step 5: Update repository RaveNoC test path and assertions**

Rename `testRepositoryRaveNoCPluginMetadataLoads` to `testRepositoryGeneratedRaveNoCIpCoreMetadataLoads`.

Replace:

```cpp
    const QString pluginRoot = repositoryPluginPath(QStringLiteral("plugins/ravenoc"));
```

with:

```cpp
    const QString pluginRoot = repositoryPluginPath(QStringLiteral("generated/ipcores/finepaper.ravenoc"));
```

Add assertions after discovery:

```cpp
    require(plugins.first().runtimeRootPath.endsWith(QStringLiteral("generated/ipcores/finepaper.ravenoc")),
            "RaveNoC runtime root should be the generated bundle directory");
    require(plugins.first().sourceRootPath.endsWith(QStringLiteral("ipcores/ravenoc")),
            "RaveNoC source root should point at the source package");
    require(plugins.first().rootPath == plugins.first().sourceRootPath,
            "generator working root should be the source package root");
```

Update failure strings in that test from "plugin" to "IP core" where they refer to RaveNoC as a concrete package.

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: PASS for RaveNoC repository metadata once generated runtime files exist.

- [x] **Step 6: Add generated Finepaper NoC repository discovery test**

Add this test after the RaveNoC repository test:

```cpp
void testRepositoryGeneratedFinepaperNoCIpCoreMetadataLoads() {
    const QString runtimeRoot = repositoryPluginPath(QStringLiteral("generated/ipcores/finepaper.noc"));
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({runtimeRoot});

    require(plugins.size() == 1, "Finepaper NoC IP core should be discovered");
    require(plugins.first().id == QStringLiteral("finepaper.noc"),
            "Finepaper NoC IP core id should load");
    require(plugins.first().kind == QStringLiteral("noc"),
            "Finepaper NoC kind should load");
    require(plugins.first().runtimeRootPath.endsWith(QStringLiteral("generated/ipcores/finepaper.noc")),
            "Finepaper NoC runtime root should be generated");
    require(plugins.first().sourceRootPath.endsWith(QStringLiteral("ipcores/finepaper-noc")),
            "Finepaper NoC source root should point at the source package");
    require(plugins.first().generator.command == QStringLiteral("ruby"),
            "Finepaper NoC generator should load");
    require(plugins.first().drc.command == QStringLiteral("ruby"),
            "Finepaper NoC DRC should load");
    require(plugins.first().topologyPresets.size() == 2,
            "Finepaper NoC should expose mesh and ring presets");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);
    const QStringList nocTypes = registry.availableTypesForPlugin(QStringLiteral("finepaper.noc"));
    require(nocTypes == QStringList({QStringLiteral("Endpoint"), QStringLiteral("XP")}),
            "Finepaper NoC should list its internal editable module types");
}
```

Call it from `main()`:

```cpp
        testRepositoryGeneratedFinepaperNoCIpCoreMetadataLoads();
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: PASS once generated Finepaper NoC runtime files exist.

- [x] **Step 7: Verify generator command working directory**

In `testGeneratorRunnerPropagatesInputFormat`, set both paths:

```cpp
    plugin.runtimeRootPath = QStringLiteral("/tmp/finepaper-format-runtime");
    plugin.sourceRootPath = QStringLiteral("/tmp/finepaper-format-source");
    plugin.rootPath = plugin.sourceRootPath;
```

Add after command resolution:

```cpp
    require(command.workingDirectory == QStringLiteral("/tmp/finepaper-format-source"),
            "generator working directory should use the source root");
```

In `testGeneratorRunnerResolvesDrcCommand`, set both paths and add:

```cpp
    require(command.workingDirectory == QStringLiteral("/tmp/finepaper-drc-source"),
            "DRC working directory should use the source root");
```

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: PASS.

---

### Task 5: Update Docs And Repository Terminology

**Files:**
- Modify: `spec_generator/README.md`
- Modify: `qt/doc/README.md`
- Modify: `qt/doc/architecture.md`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `qt/test/plugin_test.cpp`

- [x] **Step 1: Update spec generator README**

Replace the opening paragraph in `spec_generator/README.md` with:

```markdown
`spec_generator` turns editable IP core package specs into the generated runtime metadata consumed by the Qt editor.
```

Replace Inputs with:

```markdown
Inputs:

- `ipcores/finepaper-noc/ipcore.yml` for the Finepaper NoC IP core package.
- `ipcores/finepaper-noc/views/*.xml` for Finepaper NoC graphics and interface-anchor placement.
- `ipcores/ravenoc/ipcore.yml` for the RaveNoC IP core package.
- `ipcores/ravenoc/views/*.xml` for RaveNoC graphics and interface-anchor placement.
```

Replace Outputs with:

```markdown
Outputs:

- `generated/ipcores/finepaper.noc/plugin.json`
- `generated/ipcores/finepaper.noc/modules.xml`
- `generated/ipcores/finepaper.noc/graphics/*.xml`
- `ipcores/finepaper-noc/generator/src/ruby/model/xp.rb`
- `ipcores/finepaper-noc/generator/src/ruby/model/endpoint.rb`
- `generated/ipcores/finepaper.ravenoc/plugin.json`
- `generated/ipcores/finepaper.ravenoc/modules.xml`
- `generated/ipcores/finepaper.ravenoc/graphics/*.xml`
```

Replace the regenerate commands with:

```bash
ruby spec_generator/bin/spec-gen
```

Add this paragraph:

```markdown
`plugin.json` remains the Qt runtime manifest name for this node. Concrete package source lives under `ipcores/`; `plugins/` is reserved for feature plugins that add editor behavior.
```

Run:

```bash
rg -n 'plugins/noc|plugins/ravenoc|finepaper.extension.v1|extension bundle|NoC plugin|RaveNoC plugin' spec_generator/README.md
```

Expected: no output.

- [x] **Step 2: Update Qt README terminology**

In `qt/doc/README.md`, replace the repository layout bullet:

```markdown
- `../plugins/noc/`: bundled NoC plugin with module definitions, graphics, and the Ruby sample generator.
```

with:

```markdown
- `../ipcores/finepaper-noc/` and `../ipcores/ravenoc/`: editable IP core source packages with generators and views.
- `../generated/ipcores/`: generated runtime manifests and module bundles discovered by the editor.
```

Replace the `PluginRegistry` bullet with:

```markdown
- `PluginRegistry`: discovers feature plugin manifests from `FINEPAPER_PLUGIN_PATH` and generated IP core runtime manifests from repository-local `generated/ipcores/`.
```

Replace the plugin integration section title with:

```markdown
## IP core and feature plugin integration
```

In that section, make these wording replacements:

```text
startup-discovered IP plugins -> startup-discovered IP cores
plugin that owns the modules -> IP core that owns the modules
active plugin generator -> active IP core generator
```

Run:

```bash
rg -n 'plugins/noc|plugins/ravenoc|NoC plugin|IP plugins|active plugin' qt/doc/README.md
```

Expected: no output.

- [x] **Step 3: Update Qt architecture terminology**

In `qt/doc/architecture.md`, replace:

```markdown
- Plugin/integration layer: startup plugin discovery, local validators, and plugin generator runners
```

with:

```markdown
- Integration layer: startup feature plugin discovery, generated IP core runtime discovery, local validators, and IP core generator runners
```

Replace the startup paragraph with:

```markdown
Before module metadata is used, `PluginRegistry` discovers feature plugin manifests from `FINEPAPER_PLUGIN_PATH` and repository-local generated IP core manifests from `generated/ipcores/`. Discovery is startup-only. The registry stores runtime-root paths for generated metadata and source-root paths for generator and DRC command execution.
```

Replace the operational assumption:

```markdown
- Plugins are directories with `plugin.json`; the bundled NoC plugin uses Ruby and provides `generator/bin/generate`.
```

with:

```markdown
- Generated IP core runtime directories contain `plugin.json`; editable IP core source packages under `ipcores/` provide generator commands such as `generator/bin/generate`.
```

Run:

```bash
rg -n 'plugins/noc|plugins/ravenoc|NoC plugin|plugin generator|plugin graph|plugin-owned IP' qt/doc/architecture.md
```

Expected: no output.

- [x] **Step 4: Search code and tests for stale concrete IP package paths**

Run:

```bash
rg -n 'plugins/noc|plugins/ravenoc|finepaper\.extension\.v1|schema: v1|noc-definition|SpecGenerator\.generate\(|--spec|--qt-bundle|generate_extension|ExtensionParser|ExtensionBundleEmitter' \
  spec_generator qt ipcores generated
```

Expected: no live-code output. Fixture helper strings used only to derive `finepaper.ipcore.v1` test inputs and the split-string `legacy_schema` negative test are acceptable if old parser and CLI entry points have been removed.

If `qt/test/topology_preset_test.cpp` still references `plugins/ravenoc`, update that test path to `generated/ipcores/finepaper.ravenoc` in the same style as `qt/test/plugin_test.cpp`.

---

### Task 6: Run Required Verification And Archive

**Files:**
- Verify all Node 2 files changed by previous tasks.

- [x] **Step 1: Run spec generator tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: PASS. Failure signal: any Minitest failure mentioning `finepaper.extension.v1`, `plugins/noc`, `plugins/ravenoc`, missing generated files, or drift check paths.

- [x] **Step 2: Run Qt plugin test**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: PASS and stdout contains:

```text
plugin_test passed
```

Failure signal: discovery cannot find `generated/ipcores/finepaper.ravenoc` or generator command working directory is the generated runtime root instead of the source package root.

- [x] **Step 3: Run moved Finepaper NoC generator tests**

Run:

```bash
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
```

Expected: PASS. Failure signal: tests cannot load generated Ruby model files from `ipcores/finepaper-noc/generator/src/ruby/model`.

- [x] **Step 4: Run moved RaveNoC generator tests**

Run:

```bash
ruby ipcores/ravenoc/generator/test/test_generator.rb
```

Expected: PASS. Failure signal: tests cannot resolve templates under `ipcores/ravenoc/generator/template` or fake vendor paths.

- [x] **Step 5: Run moved RaveNoC smoke test**

Run:

```bash
ruby ipcores/ravenoc/generator/test/test_smoke.rb
```

Expected:

- PASS when Verilator and the RaveNoC submodule source are available.
- SKIP only when the test reports `verilator is not installed` or `RaveNoC submodule is not initialized`.

- [x] **Step 6: Run whitespace check**

Run:

```bash
git diff --check
```

Expected: no output.

- [x] **Step 7: Run Node 2 vocabulary scans**

Run:

```bash
rg -n 'finepaper\.extension\.v1|schema: v1|noc-definition|SpecGenerator\.generate\(|--spec|--qt-bundle|generate_extension|ExtensionParser|ExtensionBundleEmitter|plugins/noc|plugins/ravenoc' \
  spec_generator qt ipcores generated .gitmodules
```

Expected: no live-code output. Fixture helper strings used only to derive `finepaper.ipcore.v1` test inputs and the split-string `legacy_schema` negative test are acceptable if old parser and CLI entry points have been removed.

Run:

```bash
find plugins -maxdepth 2 -type d -print 2>/dev/null || true
```

Expected: no `plugins/noc` or `plugins/ravenoc` directory appears.

- [x] **Step 8: Review worktree before archive**

Run:

```bash
git status --short
```

Expected changed paths are limited to Node 2 implementation files:

- `.gitmodules`
- `docs/superpowers/plans/2026-05-09-node-2-ipcore-vocabulary-migration.md`
- `generated/ipcores/**`
- `ipcores/**`
- `plugins/noc/**` deletions
- `plugins/ravenoc/**` deletions
- `qt/doc/README.md`
- `qt/doc/architecture.md`
- `qt/inc/plugins/plugindescriptor.h`
- `qt/src/plugins/pluginregistry.cpp`
- `qt/test/plugin_test.cpp`
- `qt/test/topology_preset_test.cpp` only if it referenced the old RaveNoC path
- `spec_generator/README.md`
- `spec_generator/bin/spec-gen`
- `spec_generator/lib/spec_generator.rb`
- `spec_generator/test/spec_generator_test.rb`

Do not add `.codex/`, `.superpowers/`, `image.png`, or local helper artifacts.

- [x] **Step 9: Archive Node 2**

Run:

```bash
git add .gitmodules \
  ipcores generated spec_generator \
  qt/inc/plugins/plugindescriptor.h \
  qt/src/plugins/pluginregistry.cpp \
  qt/test/plugin_test.cpp \
  qt/doc/README.md \
  qt/doc/architecture.md \
  docs/superpowers/plans/2026-05-09-node-2-ipcore-vocabulary-migration.md
git add -u plugins/noc plugins/ravenoc
git status --short
git commit -m "archive: complete node-2 ipcore vocabulary migration"
```

If `qt/test/topology_preset_test.cpp` changed in Task 5 Step 4, include it in the first `git add` command.

Expected: archive commit is created with marker:

```text
archive: complete node-2 ipcore vocabulary migration
```

---

## Node 2 Boundary

This plan stops after vocabulary, source tree, generated runtime discovery, and documentation migration.

Do not implement Node 3 service/UI work in this node:

- Do not create `IpCatalogService`, `ProjectIpService`, or `ActiveWorkspaceController`.
- Do not rename saved project JSON keys from `plugins` to `ipcores`.
- Do not change the active toolbar combo or palette workflow.
- Do not split structural, feature-plugin, IP-core, and DRC validation semantics.
- Do not change graph export schema names such as `finepaper-plugin-graph-v1` in Ruby generators.

Those changes belong to later node plans.
