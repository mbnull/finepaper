# Node 1 Spec Source Of Truth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the source specs regenerate the committed NoC and RaveNoC runtime metadata, then fail verification when generated runtime files are hand-edited.

**Architecture:** Extend the existing Ruby spec generator schema in place: source YAML owns interface runtime metadata, emitters serialize it, and a repository drift check regenerates artifacts into a temporary tree before comparing them with committed files. No compatibility path is added; this node updates the current base NoC schema and current `finepaper.extension.v1` RaveNoC schema only.

**Tech Stack:** Ruby `spec_generator`, Minitest, YAML specs under `spec/noc`, generated XML/JSON/Ruby artifacts under `plugins/noc` and `plugins/ravenoc`.

---

## File Structure

- Modify: `spec_generator/lib/spec_generator.rb`
  - Add interface metadata keys to base and extension parser allowlists.
  - Validate `cardinality`, `autocomplete_group`, and `topology_rule` as optional string fields.
  - Emit the three metadata attributes from both XML emitters.
  - Add `SpecGenerator.check_repository_generated_outputs(root: Dir.pwd)` for drift checking.
- Modify: `spec_generator/bin/spec-gen`
  - Add `--check` to run the drift check command from the repository root.
- Modify: `spec_generator/README.md`
  - Document the source/generated boundary and the drift check command.
- Modify: `spec_generator/test/spec_generator_test.rb`
  - Add generator assertions for new interface attributes and NoC mesh parameters.
  - Add validation tests for non-string interface metadata.
  - Add drift check command tests.
- Modify: `spec/noc/noc.yaml`
  - Add `mesh_col` and `mesh_row` to `XP.parameters`.
  - Add interface metadata to `XP` and `Endpoint` interfaces.
- Modify: `spec/noc/ravenoc.yml`
  - Add interface metadata to `RaveTile` and `RaveEndpoint` interfaces.
- Regenerate: `plugins/noc/modules.xml`
- Regenerate: `plugins/noc/graphics/Endpoint.xml`
- Regenerate: `plugins/noc/graphics/XP.xml`
- Regenerate: `plugins/noc/generator/src/ruby/model/endpoint.rb`
- Regenerate: `plugins/noc/generator/src/ruby/model/xp.rb`
- Regenerate: `plugins/ravenoc/plugin.json`
- Regenerate: `plugins/ravenoc/modules.xml`
- Regenerate: `plugins/ravenoc/graphics/RaveEndpoint.xml`
- Regenerate: `plugins/ravenoc/graphics/RaveTile.xml`

---

### Task 1: Add Failing Generator And Drift Tests

**Files:**
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Add repository path helpers**

Add these helper methods inside `class SpecGeneratorTest < Minitest::Test`, in the existing `private` section before `write_file`:

```ruby
  def repo_path(*parts)
    File.expand_path(File.join('..', '..', *parts), __dir__)
  end

  def build_generated_fixture_repo(root)
    FileUtils.mkdir_p(File.join(root, 'spec'))
    FileUtils.cp_r(repo_path('spec/noc'), File.join(root, 'spec/noc'))

    SpecGenerator.generate(
      spec_path: File.join(root, 'spec/noc/noc.yaml'),
      views_dir: File.join(root, 'spec/noc/views'),
      qt_bundle_dir: File.join(root, 'plugins/noc'),
      ruby_model_dir: File.join(root, 'plugins/noc/generator/src/ruby/model')
    )

    SpecGenerator.generate_extension(
      extension_path: File.join(root, 'spec/noc/ravenoc.yml'),
      views_dir: File.join(root, 'spec/noc/views'),
      bundle_dir: File.join(root, 'plugins/ravenoc')
    )
  end
```

- [ ] **Step 2: Add generated XML assertions for repository specs**

Add this test after `test_generates_ravenoc_extension_runtime_bundle`:

```ruby
  def test_repository_specs_generate_runtime_interface_metadata
    Dir.mktmpdir do |dir|
      SpecGenerator.generate(
        spec_path: repo_path('spec/noc/noc.yaml'),
        views_dir: repo_path('spec/noc/views'),
        qt_bundle_dir: File.join(dir, 'plugins/noc'),
        ruby_model_dir: File.join(dir, 'plugins/noc/generator/src/ruby/model')
      )

      SpecGenerator.generate_extension(
        extension_path: repo_path('spec/noc/ravenoc.yml'),
        views_dir: repo_path('spec/noc/views'),
        bundle_dir: File.join(dir, 'plugins/ravenoc')
      )

      noc_modules_xml = File.read(File.join(dir, 'plugins/noc/modules.xml'))
      assert_includes noc_modules_xml, '<interface id="local0" label="Local 0" bus="ni_link" role="target" connects_to="initiator" match="protocol,data_width" cardinality="one" autocomplete_group="endpoint_attachment">'
      assert_includes noc_modules_xml, '<interface id="north" label="North" bus="router_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">'
      assert_includes noc_modules_xml, '<interface id="noc" label="NoC" bus="ni_link" role="initiator" connects_to="target" match="protocol,data_width" cardinality="one" autocomplete_group="endpoint_attachment">'
      assert_includes noc_modules_xml, '<parameter name="mesh_col" type="int" default="0" description="Logical mesh column." configurable="false" />'
      assert_includes noc_modules_xml, '<parameter name="mesh_row" type="int" default="0" description="Logical mesh row." configurable="false" />'

      ravenoc_modules_xml = File.read(File.join(dir, 'plugins/ravenoc/modules.xml'))
      assert_includes ravenoc_modules_xml, '<interface id="north" label="North" bus="ravenoc_router_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="router_side" topology_rule="opposite_side">'
      assert_includes ravenoc_modules_xml, '<interface id="local" label="Local" bus="ravenoc_endpoint_link" role="target" connects_to="initiator" match="" cardinality="one" autocomplete_group="endpoint_attachment">'
      assert_includes ravenoc_modules_xml, '<interface id="noc" label="NoC" bus="ravenoc_endpoint_link" role="initiator" connects_to="target" match="" cardinality="one" autocomplete_group="endpoint_attachment">'
    end
  end
```

- [ ] **Step 3: Add parser validation tests for metadata type checks**

Add these tests after `test_rejects_interface_accepts_values_outside_bus_enum`:

```ruby
  def test_rejects_base_interface_metadata_that_is_not_a_string
    Dir.mktmpdir do |dir|
      yaml = valid_spec_yaml.sub(
        'local0: { bus: ni_link, role: target, accepts:',
        'local0: { cardinality: 1, bus: ni_link, role: target, accepts:'
      )
      spec_path = write_file(dir, 'spec/noc.yaml', yaml)
      write_file(dir, 'spec/views/XP.xml', xp_view_xml)
      write_file(dir, 'spec/views/Endpoint.xml', endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate(
          spec_path: spec_path,
          views_dir: File.join(dir, 'spec/views'),
          qt_bundle_dir: File.join(dir, 'out/qt'),
          ruby_model_dir: File.join(dir, 'out/ruby')
        )
      end

      assert_match(/XP.local0 cardinality must be a string/, error.message)
    end
  end
```

Add this test after `test_rejects_base_interface_metadata_that_is_not_a_string`:

```ruby
  def test_rejects_base_interface_cardinality_outside_known_values
    Dir.mktmpdir do |dir|
      yaml = valid_spec_yaml.sub(
        'local0: { bus: ni_link, role: target, accepts:',
        'local0: { cardinality: eno, bus: ni_link, role: target, accepts:'
      )
      spec_path = write_file(dir, 'spec/noc.yaml', yaml)
      write_file(dir, 'spec/views/XP.xml', xp_view_xml)
      write_file(dir, 'spec/views/Endpoint.xml', endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate(
          spec_path: spec_path,
          views_dir: File.join(dir, 'spec/views'),
          qt_bundle_dir: File.join(dir, 'out/qt'),
          ruby_model_dir: File.join(dir, 'out/ruby')
        )
      end

      assert_match(/XP.local0 cardinality is invalid/, error.message)
    end
  end
```

Add this test after `test_rejects_extension_kind_outside_noc`:

```ruby
  def test_rejects_extension_interface_metadata_that_is_not_a_string
    Dir.mktmpdir do |dir|
      yaml = ravenoc_extension_yaml.sub(
        'connects_to: initiator',
        "connects_to: initiator\n        autocomplete_group: 42"
      )
      extension_path = write_file(dir, 'spec/noc/ravenoc.yml', yaml)
      write_file(dir, 'spec/noc/views/RaveTile.xml', rave_tile_view_xml)
      write_file(dir, 'spec/noc/views/RaveEndpoint.xml', rave_endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate_extension(
          extension_path: extension_path,
          views_dir: File.join(dir, 'spec/noc/views'),
          bundle_dir: File.join(dir, 'plugins/ravenoc')
        )
      end

      assert_match(/RaveTile.north autocomplete_group must be a string/, error.message)
    end
  end
```

Add this test after `test_rejects_extension_interface_metadata_that_is_not_a_string`:

```ruby
  def test_rejects_extension_topology_rule_outside_known_values
    Dir.mktmpdir do |dir|
      yaml = ravenoc_extension_yaml.sub(
        'match: []',
        "match: []\n        topology_rule: opposite-side"
      )
      extension_path = write_file(dir, 'spec/noc/ravenoc.yml', yaml)
      write_file(dir, 'spec/noc/views/RaveTile.xml', rave_tile_view_xml)
      write_file(dir, 'spec/noc/views/RaveEndpoint.xml', rave_endpoint_view_xml)

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.generate_extension(
          extension_path: extension_path,
          views_dir: File.join(dir, 'spec/noc/views'),
          bundle_dir: File.join(dir, 'plugins/ravenoc')
        )
      end

      assert_match(/RaveTile.north topology_rule is invalid/, error.message)
    end
  end
```

- [ ] **Step 4: Add drift check tests**

Add these tests after `test_cli_generates_extension_bundle`:

```ruby
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
      assert_includes stdout, 'Generated runtime artifacts are up to date'
    end
  end

  def test_check_repository_generated_outputs_reports_noc_modules_drift
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      path = File.join(dir, 'plugins/noc/modules.xml')
      File.write(path, File.read(path).sub('cardinality="one"', 'cardinality="many"'))

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(/plugins\/noc\/modules.xml/, error.message)
    end
  end

  def test_check_repository_generated_outputs_reports_ravenoc_modules_drift
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      path = File.join(dir, 'plugins/ravenoc/modules.xml')
      File.write(path, File.read(path).sub('cardinality="one"', 'cardinality="many"'))

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(/plugins\/ravenoc\/modules.xml/, error.message)
    end
  end

  def test_check_repository_generated_outputs_reports_missing_committed_file
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      FileUtils.rm(File.join(dir, 'plugins/noc/modules.xml'))

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(/missing committed: plugins\/noc\/modules.xml/, error.message)
    end
  end

  def test_check_repository_generated_outputs_reports_stale_committed_file
    Dir.mktmpdir do |dir|
      build_generated_fixture_repo(dir)
      File.write(File.join(dir, 'plugins/noc/graphics/Stale.xml'), '<stale />')

      error = assert_raises(SpecGenerator::SpecError) do
        SpecGenerator.check_repository_generated_outputs(root: dir)
      end

      assert_match(/missing generated: plugins\/noc\/graphics\/Stale.xml/, error.message)
    end
  end
```

- [ ] **Step 5: Run the new tests and verify failure**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb \
  --name '/test_repository_specs_generate_runtime_interface_metadata|test_rejects_base_interface_metadata_that_is_not_a_string|test_rejects_extension_interface_metadata_that_is_not_a_string|test_cli_check_passes_when_generated_runtime_artifacts_match_specs|test_check_repository_generated_outputs_reports_noc_modules_drift|test_check_repository_generated_outputs_reports_ravenoc_modules_drift/'
```

Expected before implementation: FAIL or ERROR. Acceptable failure signals are missing XML attributes, missing `mesh_col`/`mesh_row` in generated NoC XML, unknown `--check`, or undefined `SpecGenerator.check_repository_generated_outputs`.

---

### Task 2: Extend Parsers And Emitters For Interface Metadata

**Files:**
- Modify: `spec_generator/lib/spec_generator.rb`

- [ ] **Step 1: Add metadata keys to schema constants**

Update the constants near the top of `SpecGenerator`:

```ruby
  INTERFACE_METADATA_KEYS = %w[cardinality autocomplete_group topology_rule].freeze
  INTERFACE_CARDINALITIES = %w[one many].freeze
  INTERFACE_TOPOLOGY_RULES = %w[opposite_side].freeze
  EXTENSION_INTERFACE_KEYS = %w[
    label bus role connects_to match cardinality autocomplete_group topology_rule port ports
  ].freeze
  INTERFACE_KEYS = %w[
    label bus role accepts config cardinality autocomplete_group topology_rule port ports
  ].freeze
```

- [ ] **Step 2: Validate base interface metadata**

In `Parser#validate_interfaces`, after the existing role check and before `validate_accepts`, add:

```ruby
        validate_interface_metadata(module_name, interface_name, interface)
```

Add this private method in `Parser`, before `validate_accepts`:

```ruby
    def validate_interface_metadata(module_name, interface_name, interface)
      INTERFACE_METADATA_KEYS.each do |key|
        next unless interface.key?(key)

        unless interface[key].is_a?(String)
          raise SpecError, "#{module_name}.#{interface_name} #{key} must be a string"
        end

        validate_interface_metadata_value(module_name, interface_name, key, interface[key])
      end
    end

    def validate_interface_metadata_value(module_name, interface_name, key, value)
      case key
      when 'cardinality'
        unless INTERFACE_CARDINALITIES.include?(value)
          raise SpecError, "#{module_name}.#{interface_name} cardinality is invalid"
        end
      when 'topology_rule'
        unless INTERFACE_TOPOLOGY_RULES.include?(value)
          raise SpecError, "#{module_name}.#{interface_name} topology_rule is invalid"
        end
      end
    end
```

- [ ] **Step 3: Validate extension interface metadata**

In `ExtensionParser#validate_interfaces`, after the existing `match` loop and before `validate_port_projection`, add:

```ruby
        validate_interface_metadata(module_name, interface_name, interface)
```

Add this private method in `ExtensionParser`, before `validate_port_projection`:

```ruby
    def validate_interface_metadata(module_name, interface_name, interface)
      INTERFACE_METADATA_KEYS.each do |key|
        next unless interface.key?(key)

        unless interface[key].is_a?(String)
          raise SpecError, "#{module_name}.#{interface_name} #{key} must be a string"
        end

        validate_interface_metadata_value(module_name, interface_name, key, interface[key])
      end
    end

    def validate_interface_metadata_value(module_name, interface_name, key, value)
      case key
      when 'cardinality'
        unless INTERFACE_CARDINALITIES.include?(value)
          raise SpecError, "#{module_name}.#{interface_name} cardinality is invalid"
        end
      when 'topology_rule'
        unless INTERFACE_TOPOLOGY_RULES.include?(value)
          raise SpecError, "#{module_name}.#{interface_name} topology_rule is invalid"
        end
      end
    end
```

- [ ] **Step 4: Emit metadata attributes from base NoC interfaces**

In `QtBundleEmitter#interface_lines`, replace the single `lines << "      <interface...">` line with:

```ruby
        interface_attrs = {
          id: interface_name,
          label: interface['label'],
          bus: interface['bus'],
          role: interface['role'],
          connects_to: compatibility.fetch('roles').fetch(interface.fetch('role')).join(','),
          match: compatibility.fetch('match').join(','),
          cardinality: interface['cardinality'],
          autocomplete_group: interface['autocomplete_group'],
          topology_rule: interface['topology_rule']
        }
        lines << "      <interface#{attrs(interface_attrs)}>"
```

- [ ] **Step 5: Emit metadata attributes from RaveNoC extension interfaces**

In `ExtensionBundleEmitter#interface_lines`, replace the single `lines << "      <interface...">` line with:

```ruby
        interface_attrs = {
          id: interface_name,
          label: interface['label'],
          bus: interface['bus'],
          role: interface['role'],
          connects_to: interface['connects_to'],
          match: interface.fetch('match').join(','),
          cardinality: interface['cardinality'],
          autocomplete_group: interface['autocomplete_group'],
          topology_rule: interface['topology_rule']
        }
        lines << "      <interface#{attrs(interface_attrs)}>"
```

- [ ] **Step 6: Run focused parser/emitter tests and verify partial failure**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb \
  --name '/test_rejects_base_interface_metadata_that_is_not_a_string|test_rejects_extension_interface_metadata_that_is_not_a_string|test_repository_specs_generate_runtime_interface_metadata/'
```

Expected: the two rejection tests PASS. `test_repository_specs_generate_runtime_interface_metadata` still FAILS because `spec/noc/noc.yaml` and `spec/noc/ravenoc.yml` do not yet declare all metadata and NoC mesh parameters.

---

### Task 3: Move Runtime Metadata Into Source YAML

**Files:**
- Modify: `spec/noc/noc.yaml`
- Modify: `spec/noc/ravenoc.yml`

- [ ] **Step 1: Add NoC mesh coordinates to `XP.parameters`**

In `spec/noc/noc.yaml`, insert these parameters immediately after `y` under `modules.XP.parameters`:

```yaml
      mesh_col:
        type: int
        default: 0
        configurable: false
        emit: attribute
        description: Logical mesh column.
      mesh_row:
        type: int
        default: 0
        configurable: false
        emit: attribute
        description: Logical mesh row.
```

- [ ] **Step 2: Add NoC endpoint attachment metadata**

In `spec/noc/noc.yaml`, add the following fields to `XP.interfaces.local0`, `local1`, `local2`, and `local3`, immediately after `role: target`:

```yaml
        cardinality: one
        autocomplete_group: endpoint_attachment
```

In `Endpoint.interfaces.noc`, add the same fields immediately after `role: initiator`:

```yaml
        cardinality: one
        autocomplete_group: endpoint_attachment
```

- [ ] **Step 3: Add NoC router-side metadata**

In `spec/noc/noc.yaml`, add the following fields to `XP.interfaces.north`, `east`, `south`, and `west`, immediately after each `role:` line:

```yaml
        cardinality: one
        autocomplete_group: router_side
        topology_rule: opposite_side
```

- [ ] **Step 4: Add RaveNoC endpoint attachment metadata**

In `spec/noc/ravenoc.yml`, add the following fields to `RaveTile.interfaces.local` immediately after `match: []`, and to `RaveEndpoint.interfaces.noc` immediately after `match: []`:

```yaml
        cardinality: one
        autocomplete_group: endpoint_attachment
```

- [ ] **Step 5: Add RaveNoC router-side metadata**

In `spec/noc/ravenoc.yml`, add the following fields to `RaveTile.interfaces.north`, `east`, `south`, and `west` immediately after each `match: []`:

```yaml
        cardinality: one
        autocomplete_group: router_side
        topology_rule: opposite_side
```

- [ ] **Step 6: Run focused generator assertions**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb --name test_repository_specs_generate_runtime_interface_metadata
```

Expected: PASS.

---

### Task 4: Add Repository Drift Check Command

**Files:**
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `spec_generator/README.md`
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Add `tmpdir` support**

At the top of `spec_generator/lib/spec_generator.rb`, add:

```ruby
require 'tmpdir'
```

- [ ] **Step 2: Add generated artifact comparison API**

Add `GENERATED_OUTPUT_ROOTS` near the other constants, then add these module methods after `self.generate_extension`:

```ruby
  GENERATED_OUTPUT_ROOTS = [
    ['plugins/noc/modules.xml', :file],
    ['plugins/noc/graphics', :directory],
    ['plugins/noc/generator/src/ruby/model', :generated_files],
    ['plugins/ravenoc/plugin.json', :file],
    ['plugins/ravenoc/modules.xml', :file],
    ['plugins/ravenoc/graphics', :directory]
  ].freeze

  def self.check_repository_generated_outputs(root: Dir.pwd)
    root = File.expand_path(root)
    mismatches = []
    Dir.mktmpdir('finepaper-spec-gen-check') do |dir|
      generate(
        spec_path: File.join(root, 'spec/noc/noc.yaml'),
        views_dir: File.join(root, 'spec/noc/views'),
        qt_bundle_dir: File.join(dir, 'plugins/noc'),
        ruby_model_dir: File.join(dir, 'plugins/noc/generator/src/ruby/model')
      )

      generate_extension(
        extension_path: File.join(root, 'spec/noc/ravenoc.yml'),
        views_dir: File.join(root, 'spec/noc/views'),
        bundle_dir: File.join(dir, 'plugins/ravenoc')
      )

      GENERATED_OUTPUT_ROOTS.each do |relroot, type|
        relpaths = if type == :generated_files
                     generated_output_relpaths(dir, relroot, :directory)
                   else
                     (generated_output_relpaths(root, relroot, type) +
                      generated_output_relpaths(dir, relroot, type)).uniq.sort
                   end
        relpaths.each do |relpath|
          committed_path = File.join(root, relpath)
          generated_path = File.join(dir, relpath)
          if !File.file?(committed_path)
            mismatches << "missing committed: #{relpath}"
          elsif !File.file?(generated_path)
            mismatches << "missing generated: #{relpath}"
          elsif File.binread(committed_path) != File.binread(generated_path)
            mismatches << "content mismatch: #{relpath}"
          end
        end
      end
    end

    return true if mismatches.empty?

    raise SpecError, "Generated artifacts are out of date:\n#{mismatches.map { |path| "  #{path}" }.join("\n")}"
  end

  def self.generated_output_relpaths(root, relroot, type)
    return [relroot] if type == :file

    base = File.join(root, relroot)
    return [] unless File.directory?(base)

    Dir.glob(File.join(base, '**', '*'), File::FNM_DOTMATCH)
       .select { |path| File.file?(path) }
       .map { |path| path.sub(%r{\A#{Regexp.escape(root)}/}, '') }
  end
```

- [ ] **Step 3: Add `--check` to the CLI**

In `spec_generator/bin/spec-gen`, update the default options hash:

```ruby
  check: false
```

Add this option in the `OptionParser` block:

```ruby
    parser.on('--check', 'Verify committed generated artifacts match specs') { options[:check] = true }
```

Update the execution branch so `--check` runs before extension/spec generation:

```ruby
  if options[:check]
    SpecGenerator.check_repository_generated_outputs(root: Dir.pwd)
    puts 'Generated runtime artifacts are up to date'
  elsif options[:extension_path]
    raise SpecGenerator::SpecError, '--bundle is required with --extension' unless options[:extension_bundle_dir]
```

- [ ] **Step 4: Document the generated artifact boundary**

Append this section to `spec_generator/README.md`:

````markdown

## Generated Runtime Artifacts

The YAML specs and view XML files are the source of truth. These generated files are committed for simple local development and packaging:

- `plugins/noc/modules.xml`
- `plugins/noc/graphics/*.xml`
- `plugins/noc/generator/src/ruby/model/endpoint.rb`
- `plugins/noc/generator/src/ruby/model/xp.rb`
- `plugins/ravenoc/plugin.json`
- `plugins/ravenoc/modules.xml`
- `plugins/ravenoc/graphics/*.xml`

Do not edit generated runtime artifacts by hand. Change `spec/noc/noc.yaml`, `spec/noc/ravenoc.yml`, or `spec/noc/views/*.xml`, then regenerate:

```bash
ruby spec_generator/bin/spec-gen \
  --spec spec/noc/noc.yaml \
  --views spec/noc/views \
  --qt-bundle plugins/noc \
  --ruby-model plugins/noc/generator/src/ruby/model

ruby spec_generator/bin/spec-gen \
  --extension spec/noc/ravenoc.yml \
  --views spec/noc/views \
  --bundle plugins/ravenoc
```

Before committing generated runtime metadata, run:

```bash
ruby spec_generator/bin/spec-gen --check
```
````

- [ ] **Step 5: Run focused drift check tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb \
  --name '/test_cli_check_passes_when_generated_runtime_artifacts_match_specs|test_check_repository_generated_outputs_reports_noc_modules_drift|test_check_repository_generated_outputs_reports_ravenoc_modules_drift/'
```

Expected: the two temp-repo drift tests PASS. The CLI `--check` test PASSes if the committed generated artifacts already match the new generator output; otherwise it FAILs with a `Generated artifacts are out of date` message until Task 5 regenerates the artifacts.

---

### Task 5: Regenerate Runtime Artifacts And Verify Node 1

**Files:**
- Regenerate: `plugins/noc/modules.xml`
- Regenerate: `plugins/noc/graphics/Endpoint.xml`
- Regenerate: `plugins/noc/graphics/XP.xml`
- Regenerate: `plugins/noc/generator/src/ruby/model/endpoint.rb`
- Regenerate: `plugins/noc/generator/src/ruby/model/xp.rb`
- Regenerate: `plugins/ravenoc/plugin.json`
- Regenerate: `plugins/ravenoc/modules.xml`
- Regenerate: `plugins/ravenoc/graphics/RaveEndpoint.xml`
- Regenerate: `plugins/ravenoc/graphics/RaveTile.xml`

- [ ] **Step 1: Regenerate NoC runtime artifacts**

Run:

```bash
ruby spec_generator/bin/spec-gen \
  --spec spec/noc/noc.yaml \
  --views spec/noc/views \
  --qt-bundle plugins/noc \
  --ruby-model plugins/noc/generator/src/ruby/model
```

Expected output:

```text
Generated plugins/noc and plugins/noc/generator/src/ruby/model
```

- [ ] **Step 2: Regenerate RaveNoC runtime artifacts**

Run:

```bash
ruby spec_generator/bin/spec-gen \
  --extension spec/noc/ravenoc.yml \
  --views spec/noc/views \
  --bundle plugins/ravenoc
```

Expected output:

```text
Generated extension bundle plugins/ravenoc
```

- [ ] **Step 3: Run spec generator tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: PASS with zero failures and zero errors.

- [ ] **Step 4: Run the new drift command**

Run:

```bash
ruby spec_generator/bin/spec-gen --check
```

Expected output:

```text
Generated runtime artifacts are up to date
```

- [ ] **Step 5: Run NoC and RaveNoC generator tests**

Run:

```bash
ruby plugins/noc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_generator.rb
ruby plugins/ravenoc/generator/test/test_smoke.rb
```

Expected: each command exits 0 with zero failures and zero errors.

- [ ] **Step 6: Run Qt plugin verification**

Run:

```bash
CCACHE_DISABLE=1 xmake run -P qt plugin_test
```

Expected: exits 0 and reports the plugin test passed.

- [ ] **Step 7: Run whitespace and ownership checks**

Run:

```bash
git diff --check
git status --short
```

Expected: `git diff --check` has no output. `git status --short` lists the Node 1 files plus any pre-existing untracked local helper artifacts such as `.codex/`, `.superpowers/`, and `image.png`:

```text
 M spec_generator/bin/spec-gen
 M spec_generator/lib/spec_generator.rb
 M spec_generator/README.md
 M spec_generator/test/spec_generator_test.rb
 M spec/noc/noc.yaml
 M spec/noc/ravenoc.yml
```

Generated runtime artifacts may not appear in `git status` if they were already byte-identical to the regenerated output. `ruby spec_generator/bin/spec-gen --check` is the authority for this condition.

The plan file may also appear if Task 1.1 was not already archived:

```text
?? docs/superpowers/plans/2026-05-09-node-1-spec-source-of-truth.md
```

---

### Task 6: Archive Node 1

**Files:**
- Commit only Node 1 implementation files and this plan file.

- [ ] **Step 1: Review staged scope before archiving**

Run:

```bash
git status --short
```

Expected: Node 1 files appear as modified or untracked. Pre-existing local helper artifacts may also appear untracked; do not stage them.

- [ ] **Step 2: Stage Node 1 files**

Run:

```bash
git add \
  docs/superpowers/plans/2026-05-09-node-1-spec-source-of-truth.md \
  spec_generator/bin/spec-gen \
  spec_generator/lib/spec_generator.rb \
  spec_generator/README.md \
  spec_generator/test/spec_generator_test.rb \
  spec/noc/noc.yaml \
  spec/noc/ravenoc.yml \
  plugins/noc/modules.xml \
  plugins/noc/graphics/Endpoint.xml \
  plugins/noc/graphics/XP.xml \
  plugins/noc/generator/src/ruby/model/endpoint.rb \
  plugins/noc/generator/src/ruby/model/xp.rb \
  plugins/ravenoc/plugin.json \
  plugins/ravenoc/modules.xml \
  plugins/ravenoc/graphics/RaveEndpoint.xml \
  plugins/ravenoc/graphics/RaveTile.xml
```

- [ ] **Step 3: Verify staged scope**

Run:

```bash
git diff --cached --name-only
```

Expected: output contains only the paths staged in Step 2. It must not contain `.codex`, `.superpowers`, `image.png`, local screenshots, or unrelated helper artifacts.

- [ ] **Step 4: Commit archive marker**

Run:

```bash
git commit -m "archive: complete node-1 spec source of truth"
```

Expected: commit succeeds and contains only the staged Node 1 files.
