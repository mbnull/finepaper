# OpenNoC Plugin Adaptation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a mesh-only `finepaper.opennoc` IP-core package that uses OpenNoC's upstream Python mesh generator from Finepaper without changing Qt frontend behavior.

**Architecture:** Add source metadata under `ipcores/opennoc`, generate runtime metadata under `generated/ipcores/finepaper.opennoc`, and implement a Ruby adapter that consumes `finepaper-ipcore-graph-v1`, projects it to OpenNoC mesh JSON, invokes upstream `mesh_gen.py`, and copies output-local RTL artifacts. Qt should discover and expose the package through existing plugin, module, topology preset, property panel, DRC, and generation paths.

**Tech Stack:** Ruby/Minitest for spec generation and generator tests, YAML/XML runtime metadata, Python/Jinja upstream OpenNoC generator, Verilator smoke verification, existing Qt/C++ plugin registry tests only for metadata discovery.

---

## File Structure

Create:

- `ipcores/opennoc/ipcore.yml`: OpenNoC package metadata, global parameters, buses, topology preset, and six module declarations.
- `ipcores/opennoc/views/OpenNoCXP.xml`: XP node graphics and anchors.
- `ipcores/opennoc/views/OpenNoCRNF.xml`: RNF agent graphics and anchor.
- `ipcores/opennoc/views/OpenNoCRNI.xml`: RNI agent graphics and anchor.
- `ipcores/opennoc/views/OpenNoCHNF.xml`: HNF agent graphics and anchor.
- `ipcores/opennoc/views/OpenNoCHNI.xml`: HNI agent graphics and anchor.
- `ipcores/opennoc/views/OpenNoCSNF.xml`: SNF agent graphics and anchor.
- `ipcores/opennoc/generator/bin/generate`: Ruby CLI for generation.
- `ipcores/opennoc/generator/bin/drc`: Ruby CLI for DRC.
- `ipcores/opennoc/generator/src/ruby/opennoc_generator.rb`: graph parsing, validation, projection, upstream invocation, file copying, and manifest/filelist rendering.
- `ipcores/opennoc/generator/template/opennoc_filelist.f.erb`: output-local Verilog filelist.
- `ipcores/opennoc/generator/template/verify.sh.erb`: Verilator lint script.
- `ipcores/opennoc/generator/examples/mesh_2x2.json`: valid Finepaper graph fixture.
- `ipcores/opennoc/generator/test/test_generator.rb`: generator and DRC unit tests.
- `ipcores/opennoc/generator/test/test_smoke.rb`: optional Verilator smoke over the real vendor source.
- `generated/ipcores/finepaper.opennoc/plugin.json`: generated runtime manifest.
- `generated/ipcores/finepaper.opennoc/modules.xml`: generated module bundle.
- `generated/ipcores/finepaper.opennoc/graphics/*.xml`: generated graphics overlays.

Modify:

- `spec_generator/bin/spec-gen`: include OpenNoC in repository generation.
- `spec_generator/lib/spec_generator.rb`: include OpenNoC in generated-output drift checks and source copying.
- `spec_generator/test/spec_generator_test.rb`: add OpenNoC metadata/runtime generation tests.
- `spec_generator/README.md`: document OpenNoC source/generation commands.
- `qt/test/plugin_test.cpp`: add runtime metadata discovery coverage for `finepaper.opennoc`; do not modify Qt frontend source files.
- `.gitmodules`: add OpenNoC submodule if using git submodule acquisition.

Do not modify:

- `qt/src/panels/*`
- `qt/inc/panels/*`
- `qt/src/nodeeditor/*`
- `qt/inc/nodeeditor/*`
- any other Qt frontend behavior file for OpenNoC-specific support.

---

### Task 1: Register OpenNoC In Spec Generator Repository Flow

**Files:**
- Modify: `spec_generator/test/spec_generator_test.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `spec_generator/lib/spec_generator.rb`
- Test: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Write a failing repository generation test**

Add this test near `test_generates_ravenoc_ipcore_runtime_bundle`:

```ruby
def test_generates_opennoc_ipcore_runtime_bundle
  Dir.mktmpdir do |dir|
    write_opennoc_source(dir)

    SpecGenerator.generate_ipcore(
      ipcore_path: File.join(dir, 'ipcores/opennoc/ipcore.yml'),
      views_dir: File.join(dir, 'ipcores/opennoc/views'),
      runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.opennoc')
    )

    plugin_json = JSON.parse(File.read(File.join(dir, 'generated/ipcores/finepaper.opennoc/plugin.json')))
    assert_equal 'finepaper.opennoc', plugin_json.fetch('id')
    assert_equal 'OpenNoC', plugin_json.fetch('name')
    assert_equal '1.0', plugin_json.fetch('version')
    assert_equal 'noc', plugin_json.fetch('kind')
    assert_equal '../../../ipcores/opennoc', plugin_json.fetch('source_root')
    assert_equal 4, plugin_json.fetch('instance_parameters').size
    assert_equal 128, plugin_json.fetch('instance_parameters').fetch('req_flit_width').fetch('default')
    assert_equal 'generator/bin/generate', plugin_json.fetch('generator').fetch('args').first
    assert_equal 'generator/bin/drc', plugin_json.fetch('drc').fetch('args').first
    assert_equal 1, plugin_json.fetch('topology_presets').size
    assert_equal 'OpenNoCXP', plugin_json.fetch('topology_presets').first.fetch('router_module')

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
```

Add the source writer helper that will fail until Task 2 creates the referenced OpenNoC source files:

```ruby
def write_opennoc_source(root)
  write_file(root, 'ipcores/opennoc/ipcore.yml', opennoc_ipcore_yaml)
  %w[OpenNoCXP OpenNoCRNF OpenNoCRNI OpenNoCHNF OpenNoCHNI OpenNoCSNF].each do |name|
    write_file(root, "ipcores/opennoc/views/#{name}.xml", opennoc_view_xml(name))
  end
end
```

- [ ] **Step 2: Run the failing test**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb --name test_generates_opennoc_ipcore_runtime_bundle
```

Expected: FAIL with `NameError` for `opennoc_ipcore_yaml` or a missing source helper.

- [ ] **Step 3: Add OpenNoC to repository generation**

In `spec_generator/bin/spec-gen`, extend `generate_repository_ipcores`:

```ruby
SpecGenerator.generate_ipcore(
  ipcore_path: 'ipcores/opennoc/ipcore.yml',
  views_dir: 'ipcores/opennoc/views',
  runtime_bundle_dir: 'generated/ipcores/finepaper.opennoc'
)
```

In `spec_generator/lib/spec_generator.rb`, add OpenNoC generated output roots:

```ruby
['generated/ipcores/finepaper.opennoc/plugin.json', :file],
['generated/ipcores/finepaper.opennoc/modules.xml', :file],
['generated/ipcores/finepaper.opennoc/graphics', :directory]
```

Also extend `check_repository_generated_outputs`:

```ruby
copy_ipcore_source(root, dir, 'opennoc')
```

and after the RaveNoC generation block:

```ruby
generate_ipcore(
  ipcore_path: File.join(dir, 'ipcores/opennoc/ipcore.yml'),
  views_dir: File.join(dir, 'ipcores/opennoc/views'),
  runtime_bundle_dir: File.join(dir, 'generated/ipcores/finepaper.opennoc')
)
```

- [ ] **Step 4: Run the specific test again**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb --name test_generates_opennoc_ipcore_runtime_bundle
```

Expected: still FAIL because `opennoc_ipcore_yaml` and views are not implemented yet.

- [ ] **Step 5: Commit the spec-generator wiring after Task 2 passes**

Do not commit while tests are failing. After Task 2 passes, commit these files with:

```bash
git add spec_generator/bin/spec-gen spec_generator/lib/spec_generator.rb spec_generator/test/spec_generator_test.rb
git commit -m "test: add OpenNoC runtime metadata generation"
```

---

### Task 2: Add OpenNoC IP-Core Metadata And Views

**Files:**
- Create: `ipcores/opennoc/ipcore.yml`
- Create: `ipcores/opennoc/views/OpenNoCXP.xml`
- Create: `ipcores/opennoc/views/OpenNoCRNF.xml`
- Create: `ipcores/opennoc/views/OpenNoCRNI.xml`
- Create: `ipcores/opennoc/views/OpenNoCHNF.xml`
- Create: `ipcores/opennoc/views/OpenNoCHNI.xml`
- Create: `ipcores/opennoc/views/OpenNoCSNF.xml`
- Modify: `spec_generator/test/spec_generator_test.rb`
- Generate: `generated/ipcores/finepaper.opennoc/*`
- Test: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Add failing source fixture helpers**

In `spec_generator/test/spec_generator_test.rb`, add `opennoc_ipcore_yaml` and `opennoc_view_xml` helpers near the existing YAML fixture helpers. Use the same content shape as the real source file:

```ruby
def opennoc_ipcore_yaml
  File.read(File.expand_path('../../ipcores/opennoc/ipcore.yml', __dir__))
end

def opennoc_view_xml(name)
  File.read(File.expand_path("../../ipcores/opennoc/views/#{name}.xml", __dir__))
end
```

- [ ] **Step 2: Run the failing OpenNoC spec-generator test**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb --name test_generates_opennoc_ipcore_runtime_bundle
```

Expected: FAIL because `ipcores/opennoc/ipcore.yml` does not exist.

- [ ] **Step 3: Create `ipcores/opennoc/ipcore.yml`**

Create a source package YAML with this structure:

```yaml
schema: finepaper.ipcore.v1
id: finepaper.opennoc
name: OpenNoC
version: '1.0'
kind: noc
runtime:
  generator:
    command: ruby
    input_format: ipcore_graph_v1
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
    input_format: ipcore_graph_v1
    args:
      - generator/bin/drc
      - -i
      - "{input}"
instance_parameters:
  req_flit_width: { type: int, default: 128, min: 1, max: 1024, label: REQ flit width, description: OpenNoC REQ channel flit width used by verification. }
  rsp_flit_width: { type: int, default: 64, min: 1, max: 1024, label: RSP flit width, description: OpenNoC RSP channel flit width used by verification. }
  dat_flit_width: { type: int, default: 256, min: 1, max: 2048, label: DAT flit width, description: OpenNoC DAT channel flit width used by verification. }
  snp_flit_width: { type: int, default: 128, min: 1, max: 1024, label: SNP flit width, description: OpenNoC SNP channel flit width used by verification before the router appends target id bits. }
topology_presets:
  - id: mesh
    label: Mesh
    kind: mesh
    router_module: OpenNoCXP
    id_pattern: xp_{row}_{col}
    ports: { east: east, west: west, north: north, south: south }
    parameters:
      rows: { label: Rows, default: 2, min: 1, max: 8 }
      cols: { label: Columns, default: 2, min: 1, max: 8 }
buses:
  opennoc_mesh_link:
    description: OpenNoC XP-to-XP mesh link.
    compatibility:
      roles:
        initiator: [target]
        target: [initiator]
      match: []
    config: {}
    signals:
      - name: chi_channels
        direction: initiator_to_target
        width: OPENNOC_CHI_LINK
  opennoc_chi_attachment:
    description: OpenNoC CHI agent attachment to an XP P0/P1 slot.
    compatibility:
      roles:
        initiator: [target]
        target: [initiator]
      match: []
    config: {}
    signals:
      - name: chi
        direction: initiator_to_target
        width: OPENNOC_CHI_ATTACHMENT
modules:
  OpenNoCXP:
    palette_label: OpenNoC XP
    graph_group: xps
    description: OpenNoC mesh crosspoint generated through the upstream mesh generator.
    identity:
      external_id_prefix: xp
      display_prefix: XP
      width: 2
      supports_mesh_coordinates: true
    parameters:
      display_name: { type: string, default: OpenNoC XP, label: Display name, description: Name shown on the canvas. }
      external_id: { type: string, default: xp_00, label: External ID, description: Stable generated artifact identifier and upstream XP instance name. }
      x: { type: int, default: 0, configurable: false, description: Canvas X position. }
      y: { type: int, default: 0, configurable: false, description: Canvas Y position. }
      mesh_col: { type: int, default: 0, configurable: false, description: Logical OpenNoC mesh column. }
      mesh_row: { type: int, default: 0, configurable: false, description: Logical OpenNoC mesh row. }
    interfaces:
      east:
        label: East
        bus: opennoc_mesh_link
        role: initiator
        connects_to: target
        match: []
        cardinality: one
        autocomplete_group: router_side
        topology_rule: opposite_side
        port: { id: east, direction: inout, type: bus, bus_type: opennoc_mesh_link, role: router, name: East, description: East OpenNoC mesh link }
      west:
        label: West
        bus: opennoc_mesh_link
        role: target
        connects_to: initiator
        match: []
        cardinality: one
        autocomplete_group: router_side
        topology_rule: opposite_side
        port: { id: west, direction: inout, type: bus, bus_type: opennoc_mesh_link, role: router, name: West, description: West OpenNoC mesh link }
      north:
        label: North
        bus: opennoc_mesh_link
        role: target
        connects_to: initiator
        match: []
        cardinality: one
        autocomplete_group: router_side
        topology_rule: opposite_side
        port: { id: north, direction: inout, type: bus, bus_type: opennoc_mesh_link, role: router, name: North, description: North OpenNoC mesh link }
      south:
        label: South
        bus: opennoc_mesh_link
        role: initiator
        connects_to: target
        match: []
        cardinality: one
        autocomplete_group: router_side
        topology_rule: opposite_side
        port: { id: south, direction: inout, type: bus, bus_type: opennoc_mesh_link, role: router, name: South, description: South OpenNoC mesh link }
      p0:
        label: P0
        bus: opennoc_chi_attachment
        role: target
        connects_to: initiator
        match: []
        cardinality: one
        autocomplete_group: endpoint_attachment
        port: { id: p0, direction: input, type: bus, bus_type: opennoc_chi_attachment, role: attachment, name: P0, description: OpenNoC XP local P0 CHI slot }
      p1:
        label: P1
        bus: opennoc_chi_attachment
        role: target
        connects_to: initiator
        match: []
        cardinality: one
        autocomplete_group: endpoint_attachment
        port: { id: p1, direction: input, type: bus, bus_type: opennoc_chi_attachment, role: attachment, name: P1, description: OpenNoC XP local P1 CHI slot }
```

Append five agent modules using this template, replacing `TYPE`, `LABEL`, `PREFIX`, and `DESCRIPTION`:

```yaml
  OpenNoCTYPE:
    palette_label: LABEL
    graph_group: opennoc_agents
    description: DESCRIPTION
    identity:
      external_id_prefix: PREFIX
      display_prefix: LABEL
      width: 2
      supports_mesh_coordinates: false
    parameters:
      display_name: { type: string, default: LABEL, label: Display name, description: Name shown on the canvas. }
      external_id: { type: string, default: PREFIX_00, label: External ID, description: Stable generated artifact identifier. }
      x: { type: int, default: 0, configurable: false, description: Canvas X position. }
      y: { type: int, default: 0, configurable: false, description: Canvas Y position. }
    interfaces:
      chi:
        label: CHI
        bus: opennoc_chi_attachment
        role: initiator
        connects_to: target
        match: []
        cardinality: one
        autocomplete_group: endpoint_attachment
        port: { id: chi, direction: output, type: bus, bus_type: opennoc_chi_attachment, role: attachment, name: CHI, description: OpenNoC CHI attachment }
```

Use exact replacements:

```text
TYPE=RNF LABEL=RNF PREFIX=rnf DESCRIPTION=OpenNoC RNF attachment represented as an external CHI requester.
TYPE=RNI LABEL=RNI PREFIX=rni DESCRIPTION=OpenNoC RNI agent backed by upstream RTL when selected.
TYPE=HNF LABEL=HNF PREFIX=hnf DESCRIPTION=OpenNoC HNF agent backed by upstream RTL when selected.
TYPE=HNI LABEL=HNI PREFIX=hni DESCRIPTION=OpenNoC HNI agent backed by upstream RTL when selected.
TYPE=SNF LABEL=SNF PREFIX=snf DESCRIPTION=OpenNoC SNF agent backed by upstream RTL when selected.
```

- [ ] **Step 4: Create view XML files**

Use this shape for `OpenNoCXP.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<graphics>
  <node width="160" height="128" color="#e6edf3" border="#24292f" />
  <caption x="16" y="20" />
  <anchors>
    <anchor ref="north" x="80" y="0" normal_x="0" normal_y="-1" label="N" label_x="88" label_y="18" />
    <anchor ref="east" x="160" y="64" normal_x="1" normal_y="0" label="E" label_x="134" label_y="58" />
    <anchor ref="south" x="80" y="128" normal_x="0" normal_y="1" label="S" label_x="88" label_y="112" />
    <anchor ref="west" x="0" y="64" normal_x="-1" normal_y="0" label="W" label_x="18" label_y="58" />
    <anchor ref="p0" x="42" y="128" normal_x="0" normal_y="1" label="P0" label_x="30" label_y="106" />
    <anchor ref="p1" x="118" y="128" normal_x="0" normal_y="1" label="P1" label_x="106" label_y="106" />
  </anchors>
</graphics>
```

Use this shape for each agent view, replacing the caption label through the module display name rather than static XML text:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<graphics>
  <node width="132" height="72" color="#f6f8fa" border="#57606a" />
  <caption x="14" y="20" />
  <anchors>
    <anchor ref="chi" x="66" y="72" normal_x="0" normal_y="1" label="CHI" label_x="50" label_y="50" />
  </anchors>
</graphics>
```

- [ ] **Step 5: Generate OpenNoC runtime bundle**

Run:

```bash
ruby spec_generator/bin/spec-gen --ipcore ipcores/opennoc/ipcore.yml --runtime-bundle generated/ipcores/finepaper.opennoc
```

Expected: PASS and print `Generated IP core runtime bundle generated/ipcores/finepaper.opennoc`.

- [ ] **Step 6: Run spec generator tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb --name test_generates_opennoc_ipcore_runtime_bundle
ruby spec_generator/test/spec_generator_test.rb
```

Expected: PASS.

- [ ] **Step 7: Run drift check**

Run:

```bash
ruby spec_generator/bin/spec-gen --check
```

Expected: PASS and print `Generated IP core runtime artifacts are up to date`.

- [ ] **Step 8: Commit metadata and generated runtime**

Run:

```bash
git add ipcores/opennoc/ipcore.yml ipcores/opennoc/views generated/ipcores/finepaper.opennoc spec_generator/bin/spec-gen spec_generator/lib/spec_generator.rb spec_generator/test/spec_generator_test.rb
git commit -m "feat: add OpenNoC IP-core metadata"
```

---

### Task 3: Add OpenNoC Generator Test Fixtures

**Files:**
- Create: `ipcores/opennoc/generator/test/test_generator.rb`
- Create: `ipcores/opennoc/generator/examples/mesh_2x2.json`
- Test: `ipcores/opennoc/generator/test/test_generator.rb`

- [ ] **Step 1: Create a valid 2x2 graph fixture**

Create `ipcores/opennoc/generator/examples/mesh_2x2.json` with this content:

```json
{
  "schema": "finepaper-ipcore-graph-v1",
  "name": "opennoc_mesh_2x2",
  "ipcore": "finepaper.opennoc",
  "instance": "opennoc_0",
  "ipcore_state": [
    {
      "ipcore": "finepaper.opennoc",
      "instance": "opennoc_0",
      "schema": "finepaper.opennoc-project-state-v1",
      "state": {
        "kind": "noc",
        "type": "OpenNoC",
        "global_parameters": {
          "req_flit_width": 128,
          "rsp_flit_width": 64,
          "dat_flit_width": 256,
          "snp_flit_width": 128
        }
      }
    }
  ],
  "modules": [
    { "id": "XP0_0", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCXP", "parameters": { "external_id": "XP0_0", "mesh_col": 0, "mesh_row": 0, "x": 0, "y": 0 }, "ports": [] },
    { "id": "XP1_0", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCXP", "parameters": { "external_id": "XP1_0", "mesh_col": 1, "mesh_row": 0, "x": 180, "y": 0 }, "ports": [] },
    { "id": "XP0_1", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCXP", "parameters": { "external_id": "XP0_1", "mesh_col": 0, "mesh_row": 1, "x": 0, "y": 160 }, "ports": [] },
    { "id": "XP1_1", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCXP", "parameters": { "external_id": "XP1_1", "mesh_col": 1, "mesh_row": 1, "x": 180, "y": 160 }, "ports": [] },
    { "id": "rnf_0", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCRNF", "parameters": { "external_id": "RNF0", "x": -120, "y": 0 }, "ports": [] },
    { "id": "rni_0", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCRNI", "parameters": { "external_id": "RNI0", "x": -120, "y": 80 }, "ports": [] },
    { "id": "hnf_0", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCHNF", "parameters": { "external_id": "HNF0", "x": 300, "y": 0 }, "ports": [] },
    { "id": "hni_0", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCHNI", "parameters": { "external_id": "HNI0", "x": -120, "y": 160 }, "ports": [] },
    { "id": "snf_0", "ipcore": "finepaper.opennoc", "instance": "opennoc_0", "type": "OpenNoCSNF", "parameters": { "external_id": "SNF0", "x": 300, "y": 160 }, "ports": [] }
  ],
  "connections": [
    { "id": "XP0_0_east_to_XP1_0_west", "source": { "module": "XP0_0", "port": "east" }, "target": { "module": "XP1_0", "port": "west" } },
    { "id": "XP0_1_east_to_XP1_1_west", "source": { "module": "XP0_1", "port": "east" }, "target": { "module": "XP1_1", "port": "west" } },
    { "id": "XP0_0_south_to_XP0_1_north", "source": { "module": "XP0_0", "port": "south" }, "target": { "module": "XP0_1", "port": "north" } },
    { "id": "XP1_0_south_to_XP1_1_north", "source": { "module": "XP1_0", "port": "south" }, "target": { "module": "XP1_1", "port": "north" } },
    { "id": "rnf_0_to_XP0_0_p0", "source": { "module": "rnf_0", "port": "chi" }, "target": { "module": "XP0_0", "port": "p0" } },
    { "id": "rni_0_to_XP0_0_p1", "source": { "module": "rni_0", "port": "chi" }, "target": { "module": "XP0_0", "port": "p1" } },
    { "id": "hnf_0_to_XP1_0_p0", "source": { "module": "hnf_0", "port": "chi" }, "target": { "module": "XP1_0", "port": "p0" } },
    { "id": "hni_0_to_XP0_1_p0", "source": { "module": "hni_0", "port": "chi" }, "target": { "module": "XP0_1", "port": "p0" } },
    { "id": "snf_0_to_XP1_1_p0", "source": { "module": "snf_0", "port": "chi" }, "target": { "module": "XP1_1", "port": "p0" } }
  ]
}
```

- [ ] **Step 2: Write failing generator tests**

Create `ipcores/opennoc/generator/test/test_generator.rb`:

```ruby
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
```

- [ ] **Step 3: Run the failing generator tests**

Run:

```bash
ruby ipcores/opennoc/generator/test/test_generator.rb
```

Expected: FAIL because `generator/bin/generate`, `generator/bin/drc`, and `opennoc_generator.rb` do not exist.

---

### Task 4: Implement OpenNoC DRC And Mesh Projection

**Files:**
- Create: `ipcores/opennoc/generator/bin/generate`
- Create: `ipcores/opennoc/generator/bin/drc`
- Create: `ipcores/opennoc/generator/src/ruby/opennoc_generator.rb`
- Test: `ipcores/opennoc/generator/test/test_generator.rb`

- [ ] **Step 1: Add Ruby CLI wrappers**

Create `ipcores/opennoc/generator/bin/generate`:

```ruby
#!/usr/bin/env ruby

$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'optparse'
require 'opennoc_generator'

options = {
  template_dir: File.expand_path('../template', __dir__),
  vendor_dir: File.expand_path('../../vendor/OpenNoC', __dir__)
}

OptionParser.new do |opts|
  opts.on('-i INPUT') { |value| options[:input] = value }
  opts.on('-o OUTPUT') { |value| options[:output] = value }
  opts.on('-t TEMPLATE_DIR') { |value| options[:template_dir] = value }
  opts.on('--vendor PATH') { |value| options[:vendor_dir] = value }
end.parse!

begin
  OpenNoCGenerator.new(
    input_path: options.fetch(:input),
    output_dir: options.fetch(:output),
    template_dir: options.fetch(:template_dir),
    vendor_dir: options.fetch(:vendor_dir)
  ).generate
rescue KeyError => error
  warn "error: missing required option #{error.key}"
  exit 1
rescue OpenNoCGenerator::GenerationError => error
  warn "error: #{error.message}"
  exit 1
end
```

Create `ipcores/opennoc/generator/bin/drc`:

```ruby
#!/usr/bin/env ruby

$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'optparse'
require 'opennoc_generator'

options = {
  template_dir: File.expand_path('../template', __dir__),
  vendor_dir: File.expand_path('../../vendor/OpenNoC', __dir__)
}

OptionParser.new do |opts|
  opts.on('-i INPUT') { |value| options[:input] = value }
  opts.on('-o OUTPUT') { |value| options[:output] = value }
  opts.on('-t TEMPLATE_DIR') { |value| options[:template_dir] = value }
  opts.on('--vendor PATH') { |value| options[:vendor_dir] = value }
end.parse!

begin
  OpenNoCGenerator.new(
    input_path: options.fetch(:input),
    output_dir: options.fetch(:output, Dir.pwd),
    template_dir: options.fetch(:template_dir),
    vendor_dir: options.fetch(:vendor_dir)
  ).validate
  puts 'OpenNoC DRC passed'
rescue KeyError => error
  warn "error: missing required option #{error.key}"
  exit 1
rescue OpenNoCGenerator::GenerationError => error
  warn "error: #{error.message}"
  exit 1
end
```

Set both executable:

```bash
chmod +x ipcores/opennoc/generator/bin/generate ipcores/opennoc/generator/bin/drc
```

- [ ] **Step 2: Add graph parsing constants**

Create `ipcores/opennoc/generator/src/ruby/opennoc_generator.rb` with these constants and initializer:

```ruby
require 'erb'
require 'fileutils'
require 'json'
require 'open3'
require 'tmpdir'

class OpenNoCGenerator
  class GenerationError < StandardError; end

  GRAPH_SCHEMA = 'finepaper-ipcore-graph-v1'.freeze
  IPCORE_ID = 'finepaper.opennoc'.freeze

  XP_TYPE = 'OpenNoCXP'.freeze
  AGENT_TYPE_TO_ENUM = {
    'OpenNoCRNF' => 'RNF',
    'OpenNoCRNI' => 'RNI',
    'OpenNoCHNF' => 'HNF',
    'OpenNoCHNI' => 'HNI',
    'OpenNoCSNF' => 'SNF'
  }.freeze
  AGENT_RTL_DIR = {
    'OpenNoCRNI' => 'rtl/src/rni',
    'OpenNoCHNF' => 'rtl/src/hnf',
    'OpenNoCHNI' => 'rtl/src/hni',
    'OpenNoCSNF' => 'rtl/src/snf'
  }.freeze
  DEFAULTS = {
    'req_flit_width' => 128,
    'rsp_flit_width' => 64,
    'dat_flit_width' => 256,
    'snp_flit_width' => 128
  }.freeze

  attr_reader :input_path, :output_dir, :template_dir, :vendor_dir

  def initialize(input_path:, output_dir:, template_dir:, vendor_dir:)
    @input_path = input_path
    @output_dir = output_dir
    @template_dir = template_dir
    @vendor_dir = vendor_dir
  end
end
```

- [ ] **Step 3: Implement `read_graph`, `validate`, and parameter parsing**

Add:

```ruby
def validate
  graph = read_graph
  build_model(graph)
  true
end

private

def read_graph
  data = JSON.parse(File.read(input_path))
  raise GenerationError, "expected schema #{GRAPH_SCHEMA}" unless data['schema'] == GRAPH_SCHEMA
  raise GenerationError, "expected ipcore #{IPCORE_ID}" unless data['ipcore'] == IPCORE_ID
  data
rescue Errno::ENOENT
  raise GenerationError, "input graph not found: #{input_path}"
rescue JSON::ParserError => error
  raise GenerationError, "invalid JSON input: #{error.message}"
end

def global_parameters(graph)
  state = graph.fetch('ipcore_state', []).find { |record| record['ipcore'] == IPCORE_ID }
  raise GenerationError, 'missing ipcore_state' unless state
  parameters = state.fetch('state', {}).fetch('global_parameters', nil)
  raise GenerationError, 'ipcore_state.state.global_parameters must be an object' unless parameters.is_a?(Hash)
  DEFAULTS.merge(parameters)
end

def positive_integer!(parameters, name)
  value = parameters.fetch(name)
  raise GenerationError, "#{name} must be a positive integer" unless value.is_a?(Integer) && value.positive?
  value
end
```

- [ ] **Step 4: Implement coordinate and mesh validation**

Implement methods equivalent to RaveNoC's robust pattern:

```ruby
def build_model(graph)
  parameters = global_parameters(graph)
  DEFAULTS.keys.each { |name| positive_integer!(parameters, name) }
  modules = graph.fetch('modules', [])
  xps = modules.select { |mod| mod['ipcore'] == IPCORE_ID && mod['type'] == XP_TYPE }
  raise GenerationError, 'expected at least one OpenNoCXP module' if xps.empty?
  coordinates = rectangular_coordinates(logical_xp_coordinates(xps)) ||
                rectangular_coordinates(connection_xp_coordinates(xps, graph.fetch('connections', []))) ||
                rectangular_coordinates(canvas_xp_coordinates(xps)) ||
                raise(GenerationError, "OpenNoCXP graph must be rectangular, found #{xps.size} XPs")
  coordinate_by_id = coordinates.to_h { |id, x, y| [id, [x, y]] }
  validate_coordinate_range!(coordinate_by_id)
  validate_mesh_connections!(graph, coordinate_by_id)
  attachments = attachment_bindings(graph, coordinate_by_id)
  mesh_json = mesh_json_for(graph, coordinate_by_id, attachments)
  {
    graph: graph,
    parameters: parameters,
    coordinate_by_id: coordinate_by_id,
    rows: coordinate_by_id.values.map(&:last).max + 1,
    cols: coordinate_by_id.values.map(&:first).max + 1,
    attachments: attachments,
    mesh_json: mesh_json
  }
end
```

Use exact mesh link keys:

```ruby
def mesh_link_key(source_module, source_port, target_module, target_port)
  return nil unless source_module && target_module
  case [source_port, target_port]
  when ['east', 'west']
    { from: source_module, to: target_module, axis: :east }
  when ['west', 'east']
    { from: target_module, to: source_module, axis: :east }
  when ['south', 'north']
    { from: source_module, to: target_module, axis: :south }
  when ['north', 'south']
    { from: target_module, to: source_module, axis: :south }
  end
end
```

Error text required by tests:

```ruby
raise GenerationError, "missing mesh link #{from} #{axis} #{to}"
raise GenerationError, "duplicate mesh link #{from} #{axis} #{to}"
raise GenerationError, "invalid mesh link #{connection_id}"
```

- [ ] **Step 5: Implement attachment validation**

Implement `attachment_bindings` so it:

- accepts only `agent.chi -> XP.p0/p1` or reversed order;
- records `slot` as `p0` or `p1`;
- maps type through `AGENT_TYPE_TO_ENUM`;
- rejects duplicate XP slot bindings;
- rejects each agent with zero or more than one XP slot connection.

Use this binding shape:

```ruby
{
  agent_id: module_artifact_id(agent),
  agent_module_id: agent.fetch('id'),
  agent_type: agent.fetch('type'),
  upstream_type: AGENT_TYPE_TO_ENUM.fetch(agent.fetch('type')),
  xp_module_id: xp_id,
  xp_id: module_artifact_id(xp),
  slot: slot
}
```

Required error messages:

```ruby
"#{agent_type} #{agent_id} must connect to exactly one XP slot"
"multiple OpenNoC agents connect to #{xp_id}.#{slot}"
"invalid OpenNoC agent connection #{connection_id}"
```

- [ ] **Step 6: Implement mesh JSON projection**

Project each XP in row-major order:

```ruby
def mesh_json_for(graph, coordinate_by_id, attachments)
  modules_by_id = graph.fetch('modules', []).to_h { |mod| [mod.fetch('id'), mod] }
  attachments_by_slot = attachments.to_h { |binding| [[binding.fetch(:xp_module_id), binding.fetch(:slot)], binding] }
  coordinate_by_id.sort_by { |_, (x, y)| [y, x] }.to_h do |xp_module_id, (x, y)|
    xp = modules_by_id.fetch(xp_module_id)
    [
      module_artifact_id(xp),
      {
        'X' => x,
        'Y' => y,
        'P0' => attachments_by_slot[[xp_module_id, 'p0']]&.fetch(:upstream_type) || 'NONE',
        'P1' => attachments_by_slot[[xp_module_id, 'p1']]&.fetch(:upstream_type) || 'NONE'
      }
    ]
  end
end
```

Sanitize IDs with:

```ruby
def safe_sv_identifier(value)
  identifier = value.to_s.gsub(/[^a-zA-Z0-9_$]/, '_')
  identifier = "m_#{identifier}" unless identifier.match?(/\A[a-zA-Z_]/)
  identifier
end
```

- [ ] **Step 7: Run DRC-focused tests**

Run:

```bash
ruby ipcores/opennoc/generator/test/test_generator.rb --name test_drc_accepts_valid_mesh
ruby ipcores/opennoc/generator/test/test_generator.rb --name test_drc_rejects_unconnected_agent
ruby ipcores/opennoc/generator/test/test_generator.rb --name test_drc_rejects_missing_mesh_link
```

Expected: PASS.

- [ ] **Step 8: Commit DRC and projection**

Run:

```bash
git add ipcores/opennoc/generator/bin ipcores/opennoc/generator/src/ruby/opennoc_generator.rb ipcores/opennoc/generator/test/test_generator.rb ipcores/opennoc/generator/examples/mesh_2x2.json
git commit -m "feat: add OpenNoC mesh DRC"
```

---

### Task 5: Implement Upstream Invocation And Output Artifacts

**Files:**
- Modify: `ipcores/opennoc/generator/src/ruby/opennoc_generator.rb`
- Create: `ipcores/opennoc/generator/template/opennoc_filelist.f.erb`
- Create: `ipcores/opennoc/generator/template/verify.sh.erb`
- Test: `ipcores/opennoc/generator/test/test_generator.rb`

- [ ] **Step 1: Add vendor requirements**

Add constants:

```ruby
REQUIRED_VENDOR_FILES = [
  'LICENSE',
  'tools/mesh_generator/mesh_gen.py',
  'tools/mesh_generator/template/mesh_wrapper.j2',
  'tools/mesh_generator/chi_xp_node.sv',
  'rtl/misc/chi_xp_channel.v',
  'rtl/include/chie_defines.v',
  'rtl/include/rni_param.v',
  'rtl/include/hnf_param.v',
  'rtl/include/hni_param.v',
  'rtl/include/snf_param.v',
  'rtl/src/rni/rni.v',
  'rtl/src/hnf/hnf.v',
  'rtl/src/hni/hni.v',
  'rtl/src/snf/snf.v'
].freeze

BASE_COPY_FILES = [
  'LICENSE',
  'tools/mesh_generator/chi_xp_node.sv',
  'rtl/misc/chi_xp_channel.v'
].freeze
```

Implement:

```ruby
def validate_vendor!
  missing = REQUIRED_VENDOR_FILES.find { |relative| !File.file?(File.join(vendor_dir, relative)) }
  return unless missing
  raise GenerationError,
        "OpenNoC vendor source is missing or incomplete. Run: git submodule update --init --recursive. Missing: #{missing}"
end
```

- [ ] **Step 2: Implement `generate`**

Add:

```ruby
def generate
  graph = read_graph
  model = build_model(graph)
  validate_vendor!
  FileUtils.mkdir_p(output_dir)
  write_mesh_json(model)
  wrapper = run_upstream_mesh_generator(model)
  copy_generated_wrapper(wrapper)
  copy_vendor_sources(model)
  render_artifacts(model, wrapper)
  write_manifest(model, wrapper)
  puts "Generated OpenNoC mesh integration in #{output_dir}"
end
```

- [ ] **Step 3: Write mesh JSON and call upstream generator**

Implement:

```ruby
def write_mesh_json(model)
  File.write(File.join(output_dir, 'opennoc_mesh.json'), JSON.pretty_generate(model.fetch(:mesh_json)) + "\n")
end

def run_upstream_mesh_generator(model)
  wrapper = "mesh_wrapper_#{model.fetch(:cols)}x#{model.fetch(:rows)}.sv"
  Dir.mktmpdir('finepaper-opennoc') do |dir|
    config = File.join(dir, 'opennoc_mesh.json')
    File.write(config, JSON.pretty_generate(model.fetch(:mesh_json)) + "\n")
    generator_dir = File.join(vendor_dir, 'tools/mesh_generator')
    stdout, stderr, status = Open3.capture3('python3', 'mesh_gen.py', '-f', config, chdir: generator_dir)
    raise GenerationError, "OpenNoC mesh generator failed: #{stderr.empty? ? stdout : stderr}" unless status.success?
    generated = File.join(generator_dir, wrapper)
    raise GenerationError, "OpenNoC mesh generator did not produce #{wrapper}" unless File.file?(generated)
    FileUtils.cp(generated, File.join(dir, wrapper))
    FileUtils.rm_f(generated)
    FileUtils.cp(File.join(dir, wrapper), File.join(output_dir, wrapper))
  end
  wrapper
end
```

- [ ] **Step 4: Copy selected source files**

Implement:

```ruby
def copy_vendor_sources(model)
  copy_relative_files(BASE_COPY_FILES)
  copy_tree('rtl/include')
  copy_tree('rtl/misc')
  model.fetch(:attachments).map { |binding| binding.fetch(:agent_type) }.uniq.each do |agent_type|
    relative_dir = AGENT_RTL_DIR[agent_type]
    copy_tree(relative_dir) if relative_dir
  end
end

def copy_relative_files(files)
  files.each do |relative|
    source = File.join(vendor_dir, relative)
    target = File.join(output_dir, relative)
    FileUtils.mkdir_p(File.dirname(target))
    FileUtils.cp(source, target)
  end
end

def copy_tree(relative_dir)
  source = File.join(vendor_dir, relative_dir)
  return unless File.directory?(source)
  target = File.join(output_dir, relative_dir)
  FileUtils.mkdir_p(File.dirname(target))
  FileUtils.rm_rf(target)
  FileUtils.cp_r(source, target)
end
```

- [ ] **Step 5: Create filelist template**

Create `ipcores/opennoc/generator/template/opennoc_filelist.f.erb`:

```erb
<%= File.join(output_dir, 'tools/mesh_generator/chi_xp_node.sv') %>
<%= File.join(output_dir, 'rtl/misc/chi_xp_channel.v') %>
<% copied_agent_dirs.each do |dir| -%>
<% Dir.glob(File.join(output_dir, dir, '**', '*.{v,sv}')).sort.each do |path| -%>
<%= path %>
<% end -%>
<% end -%>
<%= File.join(output_dir, wrapper) %>
```

- [ ] **Step 6: Create verify template**

Create `ipcores/opennoc/generator/template/verify.sh.erb`:

```erb
#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
verilator --lint-only --sv \
  -Wno-PINMISSING \
  -GREQ_FLIT_WIDTH=<%= parameters.fetch('req_flit_width') %> \
  -GRSP_FLIT_WIDTH=<%= parameters.fetch('rsp_flit_width') %> \
  -GDAT_FLIT_WIDTH=<%= parameters.fetch('dat_flit_width') %> \
  -GSNP_FLIT_WIDTH=<%= parameters.fetch('snp_flit_width') %> \
  -f opennoc_filelist.f \
  --top-module <%= File.basename(wrapper, '.sv') %>
```

- [ ] **Step 7: Render templates and manifest**

Add:

```ruby
def render_artifacts(model, wrapper)
  parameters = model.fetch(:parameters)
  copied_agent_dirs = model.fetch(:attachments)
                           .map { |binding| AGENT_RTL_DIR[binding.fetch(:agent_type)] }
                           .compact
                           .uniq
  render('opennoc_filelist.f.erb', File.join(output_dir, 'opennoc_filelist.f'), binding)
  render('verify.sh.erb', File.join(output_dir, 'verify.sh'), binding)
  FileUtils.chmod(0o755, File.join(output_dir, 'verify.sh'))
end

def render(template_name, target, template_binding)
  template = ERB.new(File.read(File.join(template_dir, template_name)), trim_mode: '-')
  File.write(target, template.result(template_binding))
end

def write_manifest(model, wrapper)
  manifest = {
    ipcore: IPCORE_ID,
    topology: 'mesh',
    rows: model.fetch(:rows),
    cols: model.fetch(:cols),
    parameters: model.fetch(:parameters),
    wrapper: wrapper,
    agents: model.fetch(:attachments).map do |binding|
      {
        id: binding.fetch(:agent_id),
        type: binding.fetch(:agent_type),
        upstream_type: binding.fetch(:upstream_type),
        xp: binding.fetch(:xp_id),
        slot: binding.fetch(:slot),
        rtl: AGENT_RTL_DIR.fetch(binding.fetch(:agent_type), 'external')
      }
    end,
    verify: './verify.sh'
  }
  File.write(File.join(output_dir, 'manifest.json'), JSON.pretty_generate(manifest) + "\n")
end
```

- [ ] **Step 8: Run artifact tests**

Run:

```bash
ruby ipcores/opennoc/generator/test/test_generator.rb --name test_generator_writes_mesh_json_filelist_verify_and_manifest
ruby ipcores/opennoc/generator/test/test_generator.rb --name test_generator_rejects_missing_vendor
ruby ipcores/opennoc/generator/test/test_generator.rb
```

Expected: PASS.

- [ ] **Step 9: Commit generator artifacts**

Run:

```bash
git add ipcores/opennoc/generator
git commit -m "feat: generate OpenNoC mesh artifacts"
```

---

### Task 6: Add OpenNoC Vendor Source And Smoke Test

**Files:**
- Modify: `.gitmodules`
- Create: `ipcores/opennoc/vendor/OpenNoC` submodule
- Create: `ipcores/opennoc/generator/test/test_smoke.rb`
- Test: `ipcores/opennoc/generator/test/test_smoke.rb`

- [ ] **Step 1: Add OpenNoC as a pinned submodule**

Run:

```bash
git submodule add https://github.com/RV-BOSC/OpenNoC.git ipcores/opennoc/vendor/OpenNoC
git -C ipcores/opennoc/vendor/OpenNoC checkout 4f57ddacfd9c6bd28e673ffa65e45333042be61e
```

Expected: submodule is present and pinned. If the checkout commit is not available, use the current upstream `HEAD` and record the exact commit in the smoke test output.

- [ ] **Step 2: Write smoke test**

Create `ipcores/opennoc/generator/test/test_smoke.rb`:

```ruby
$LOAD_PATH.unshift File.expand_path('../src/ruby', __dir__)

require 'minitest/autorun'
require 'open3'
require 'rbconfig'
require 'tmpdir'

class OpenNoCSmokeTest < Minitest::Test
  GENERATOR = File.expand_path('../bin/generate', __dir__)
  EXAMPLE = File.expand_path('../examples/mesh_2x2.json', __dir__)
  VENDOR = File.expand_path('../../vendor/OpenNoC', __dir__)

  def test_real_vendor_generation_and_optional_verilator_smoke
    skip 'OpenNoC vendor source is not initialized' unless File.file?(File.join(VENDOR, 'tools/mesh_generator/mesh_gen.py'))

    Dir.mktmpdir do |dir|
      stdout, stderr, status = Open3.capture3(RbConfig.ruby, GENERATOR, '-i', EXAMPLE, '-o', dir)
      assert status.success?, stderr
      assert_includes stdout, 'Generated OpenNoC mesh integration'
      assert File.file?(File.join(dir, 'mesh_wrapper_2x2.sv'))

      verilator = system('command -v verilator >/dev/null 2>&1')
      skip 'verilator is not installed' unless verilator

      stdout, stderr, status = Open3.capture3('./verify.sh', chdir: dir)
      assert status.success?, "stdout:\n#{stdout}\nstderr:\n#{stderr}"
    end
  end
end
```

- [ ] **Step 3: Run smoke test**

Run:

```bash
ruby ipcores/opennoc/generator/test/test_smoke.rb
```

Expected: PASS if Verilator accepts the current upstream output, SKIP only when vendor or Verilator is missing. If Verilator fails due to upstream lint outside the known `PINMISSING` warning, add the narrowest documented suppression to `verify.sh.erb` and update the smoke test assertion comments.

- [ ] **Step 4: Commit vendor and smoke**

Run:

```bash
git add .gitmodules ipcores/opennoc/vendor/OpenNoC ipcores/opennoc/generator/test/test_smoke.rb ipcores/opennoc/generator/template/verify.sh.erb
git commit -m "test: add OpenNoC vendor smoke"
```

---

### Task 7: Add Runtime Metadata Discovery Coverage Without Qt Frontend Changes

**Files:**
- Modify: `qt/test/plugin_test.cpp`
- Test: `plugin_test`

- [ ] **Step 1: Add metadata-only plugin test**

Add a test after `testRepositoryRaveNoCIpCoreMetadataLoads()`:

```cpp
void testRepositoryOpenNoCIpCoreMetadataLoads() {
    const QString pluginRoot = repositoryPluginPath(QStringLiteral("generated/ipcores/finepaper.opennoc"));
    const QList<PluginDescriptor> plugins = PluginRegistry::discover({pluginRoot});

    require(plugins.size() == 1, "OpenNoC IP core should be discovered");
    require(plugins.first().id == QStringLiteral("finepaper.opennoc"),
            "OpenNoC IP core id should load");
    require(plugins.first().runtimeRootPath == pluginRoot,
            "OpenNoC runtime root should be generated bundle directory");
    require(plugins.first().sourceRootPath == repositoryPluginPath(QStringLiteral("ipcores/opennoc")),
            "OpenNoC source root should resolve to concrete IP source package");
    require(plugins.first().kind == QStringLiteral("noc"),
            "OpenNoC IP core kind should load");
    require(plugins.first().instanceParameters.contains(QStringLiteral("req_flit_width")),
            "OpenNoC req flit width should load");
    require(std::get<int>(plugins.first().instanceParameters.value(QStringLiteral("req_flit_width")).defaultValue) == 128,
            "OpenNoC req flit width default should load");
    require(plugins.first().generator.command == QStringLiteral("ruby"),
            "OpenNoC IP core should use Ruby generator");
    require(plugins.first().generator.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "OpenNoC generator should request IP-core graph input");
    require(plugins.first().drc.command == QStringLiteral("ruby"),
            "OpenNoC IP core should use Ruby DRC");
    require(plugins.first().drc.inputFormat == QStringLiteral("ipcore_graph_v1"),
            "OpenNoC DRC should request IP-core graph input");
    require(plugins.first().topologyPresets.size() == 1,
            "OpenNoC IP core should expose one topology preset");
    require(plugins.first().topologyPresets.first().routerModule == QStringLiteral("OpenNoCXP"),
            "OpenNoC mesh preset should create OpenNoCXP routers");

    ModuleRegistry registry(ModuleRegistry::LoadMode::Empty);
    registry.loadPlugins(plugins);

    const QStringList types = registry.availableTypesForIpcore(QStringLiteral("finepaper.opennoc"));
    require(types == QStringList({
                QStringLiteral("OpenNoCHNF"),
                QStringLiteral("OpenNoCHNI"),
                QStringLiteral("OpenNoCRNF"),
                QStringLiteral("OpenNoCRNI"),
                QStringLiteral("OpenNoCSNF"),
                QStringLiteral("OpenNoCXP")
            }),
            "OpenNoC active IP should list XP plus five agent module types");

    const ModuleType* xpType = registry.getType(QStringLiteral("OpenNoCXP"));
    require(xpType != nullptr, "OpenNoCXP module type should load");
    require(xpType->ipcoreId == QStringLiteral("finepaper.opennoc"),
            "OpenNoCXP should keep IP core ownership");
    require(xpType->graphGroup == QStringLiteral("xps"),
            "OpenNoCXP should participate as the router graph group");
    require(xpType->interfaceMetadata.value(QStringLiteral("east")).topologyRule == QStringLiteral("opposite_side"),
            "OpenNoCXP east should declare opposite_side topology rule");
    require(xpType->interfaceMetadata.value(QStringLiteral("p0")).cardinality == QStringLiteral("one"),
            "OpenNoCXP p0 should declare one attachment");

    const ModuleType* rniType = registry.getType(QStringLiteral("OpenNoCRNI"));
    require(rniType != nullptr, "OpenNoCRNI module type should load");
    require(rniType->interfaceMetadata.value(QStringLiteral("chi")).autocompleteGroup == QStringLiteral("endpoint_attachment"),
            "OpenNoCRNI CHI interface should use endpoint attachment autocomplete");
}
```

Call it in `main()`:

```cpp
testRepositoryOpenNoCIpCoreMetadataLoads();
```

- [ ] **Step 2: Build and run plugin test**

Run:

```bash
xmake build plugin_test
xmake run plugin_test
```

Expected: PASS and output contains `plugin_test passed`.

- [ ] **Step 3: Verify no Qt frontend source changed**

Run:

```bash
git diff --name-only | rg '^qt/(src|inc)/(panels|nodeeditor|app|widgets)/' || true
```

Expected: no output from OpenNoC work. If unrelated pre-existing files appear, do not edit or revert them; note they are unrelated in the final summary.

- [ ] **Step 4: Commit metadata test**

Run:

```bash
git add qt/test/plugin_test.cpp
git commit -m "test: cover OpenNoC runtime metadata"
```

---

### Task 8: Final Verification And Documentation

**Files:**
- Modify: `spec_generator/README.md`
- Test: full relevant suite

- [ ] **Step 1: Update README**

Add OpenNoC generated artifacts to `spec_generator/README.md`:

```markdown
- `generated/ipcores/finepaper.opennoc/plugin.json`
- `generated/ipcores/finepaper.opennoc/modules.xml`
- `generated/ipcores/finepaper.opennoc/graphics/*.xml`
```

Add regeneration command:

```bash
ruby spec_generator/bin/spec-gen \
  --ipcore ipcores/opennoc/ipcore.yml \
  --runtime-bundle generated/ipcores/finepaper.opennoc
```

Add a note:

```markdown
OpenNoC generation keeps the upstream Python mesh generator under `ipcores/opennoc/vendor/OpenNoC` and wraps it from the Finepaper Ruby generator. The first version supports mesh only.
```

- [ ] **Step 2: Run spec and generator tests**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
ruby spec_generator/bin/spec-gen --check
ruby ipcores/opennoc/generator/test/test_generator.rb
ruby ipcores/opennoc/generator/test/test_smoke.rb
ruby ipcores/ravenoc/generator/test/test_generator.rb
ruby ipcores/finepaper-noc/generator/test/test_generator.rb
```

Expected: all PASS, with `test_smoke.rb` allowed to SKIP only for missing Verilator or missing vendor.

- [ ] **Step 3: Run Qt metadata tests**

Run:

```bash
xmake build plugin_test
xmake build ipcoregraphexporter_test
xmake build validation_test
xmake run plugin_test
xmake run ipcoregraphexporter_test
xmake run validation_test
```

Expected: all PASS.

- [ ] **Step 4: Check no OpenNoC-specific Qt frontend edits**

Run:

```bash
git diff --name-only HEAD | rg '^qt/(src|inc)/(panels|nodeeditor|app|widgets)/' || true
```

Expected: no output attributable to OpenNoC implementation. If dirty unrelated Qt files were present before starting, leave them untouched and exclude them from OpenNoC commits.

- [ ] **Step 5: Commit docs and final fixes**

Run:

```bash
git add spec_generator/README.md
git commit -m "docs: document OpenNoC runtime generation"
```

If final verification required small fixes in OpenNoC generator or metadata, include those exact files in a separate focused commit:

```bash
git add ipcores/opennoc generated/ipcores/finepaper.opennoc spec_generator
git commit -m "fix: stabilize OpenNoC integration"
```

- [ ] **Step 6: Final status check**

Run:

```bash
git status --short
```

Expected: no OpenNoC-related uncommitted files. Pre-existing unrelated dirty files may remain and must be reported separately.
