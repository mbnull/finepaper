# OpenNoC Plugin Adaptation Design

## Purpose

Finepaper will add OpenNoC as a first-class IP-core package that validates the current metadata-driven IP integration path without changing the Qt frontend.

The first version targets OpenNoC mesh only. It keeps OpenNoC's upstream Python/Jinja mesh generator as the network wrapper generator and adds a thin Finepaper adapter around it. Users edit an internal OpenNoC graph in Finepaper, run the existing generate action, and receive a self-contained Verilog integration output containing the generated mesh wrapper and required upstream RTL.

This is intentionally similar in scope to the RaveNoC adaptation, but it models OpenNoC as editable internal topology rather than a single black-box fabric.

## Current Context

Finepaper already has the generic pieces needed for this integration:

- `ipcores/<package>/ipcore.yml` is the source-of-truth package metadata.
- `spec_generator` emits committed runtime bundles under `generated/ipcores/<ipcore-id>/`.
- Qt discovers runtime `plugin.json`, loads `modules.xml`, displays active workspace modules, creates graph nodes from metadata, and renders property fields from parameter metadata.
- `TopologyPresetBuilder` already creates metadata-driven mesh presets.
- `IpCoreGraphExporter` already writes `finepaper-ipcore-graph-v1` JSON for generator and DRC commands.

The first OpenNoC version must not add OpenNoC-specific Qt UI branches. If this version requires Qt frontend changes to expose the modules, parameters, ports, topology preset, or generator action, that is evidence that the frontend/data boundary is still too coupled and should be treated as a generic metadata gap, not an OpenNoC feature branch.

## Selected Approach

Create a new `finepaper.opennoc` IP-core package.

The package will:

- declare OpenNoC mesh router and agent modules in `ipcore.yml`;
- provide module view XML for editor anchors;
- vendor OpenNoC under the package source tree;
- provide Ruby generator and DRC entrypoints matching Finepaper's `ipcore_graph_v1` contract;
- have the Ruby generator produce OpenNoC mesh JSON and call upstream `tools/mesh_generator/mesh_gen.py`;
- copy required generated and upstream Verilog files into the selected output directory;
- emit `opennoc_filelist.f`, `verify.sh`, and `manifest.json`.

Do not implement ring support in this version. The upstream ring generator is intentionally left for a later spec.

## Source Policy

OpenNoC source will be added under:

```text
ipcores/opennoc/vendor/OpenNoC
```

The preferred acquisition method is a pinned git submodule pointing to:

```text
https://github.com/RV-BOSC/OpenNoC.git
```

The generator must validate that required upstream files exist and fail with an actionable message when the vendor source is missing or incomplete.

OpenNoC is licensed under Mulan PSL v2. Generated outputs that copy OpenNoC files must preserve existing file headers and include the upstream `LICENSE` in the output bundle.

## Package Layout

The editable source package will use this layout:

```text
ipcores/opennoc/
  ipcore.yml
  views/
    OpenNoCXP.xml
    OpenNoCRNF.xml
    OpenNoCRNI.xml
    OpenNoCHNF.xml
    OpenNoCHNI.xml
    OpenNoCSNF.xml
  generator/
    bin/
      generate
      drc
    src/ruby/
      opennoc_generator.rb
    test/
      test_generator.rb
      test_smoke.rb
    examples/
      mesh_2x2.json
    template/
      opennoc_filelist.f.erb
      verify.sh.erb
  vendor/
    OpenNoC/
```

`spec_generator` will emit:

```text
generated/ipcores/finepaper.opennoc/
  plugin.json
  modules.xml
  graphics/
    OpenNoCXP.xml
    OpenNoCRNF.xml
    OpenNoCRNI.xml
    OpenNoCHNF.xml
    OpenNoCHNI.xml
    OpenNoCSNF.xml
```

The repository generated-output drift check must include the new runtime bundle.

## Module Model

The first version declares six OpenNoC module types:

- `OpenNoCXP`
- `OpenNoCRNF`
- `OpenNoCRNI`
- `OpenNoCHNF`
- `OpenNoCHNI`
- `OpenNoCSNF`

These are independent module types, not one generic agent with a type parameter. This keeps the workspace module list, future category display, and future per-agent configuration surfaces clean.

### OpenNoCXP

`OpenNoCXP` represents one OpenNoC mesh crosspoint/router.

It provides:

- `east`, `west`, `north`, `south` mesh ports;
- `p0`, `p1` local CHI attachment slots;
- `display_name`, `external_id`, `x`, `y`, `mesh_col`, and `mesh_row` parameters.

The mesh topology preset creates only `OpenNoCXP` nodes and their mesh links. Users can then drag agent IP modules into the same workspace and connect them to `p0` or `p1`.

### OpenNoC Agent Modules

The five agent modules represent OpenNoC-attached CHI agents:

- `OpenNoCRNF` maps to upstream `RNF`.
- `OpenNoCRNI` maps to upstream `RNI`.
- `OpenNoCHNF` maps to upstream `HNF`.
- `OpenNoCHNI` maps to upstream `HNI`.
- `OpenNoCSNF` maps to upstream `SNF`.

Each agent exposes one CHI attachment port that connects to `OpenNoCXP.p0` or `OpenNoCXP.p1`. Each agent must connect to exactly one XP slot before generation.

First-version agent parameters should stay minimal:

- editor identity: `display_name`, `external_id`, `x`, `y`;
- optional node id fields only where needed for generated manifests or future wrappers.

Full HNF/HNI/RNI/SNF/RNF configuration pages are future work and should reuse these module types, not replace them.

## Buses And Connection Rules

Declare two buses:

- `opennoc_mesh_link` for XP-to-XP mesh links.
- `opennoc_chi_attachment` for agent-to-XP local slot attachments.

`OpenNoCXP.east/west/north/south` use `opennoc_mesh_link` and the existing `opposite_side` topology rule.

`OpenNoCXP.p0/p1` use `opennoc_chi_attachment` with one-to-one cardinality. Agent attachment ports use the opposite role. The generator validates the attached module type and maps it to the upstream `P0/P1` enum.

## Topology Preset

`finepaper.opennoc` declares one mesh preset:

```yaml
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
```

The first version uses the upstream tool's documented coordinate limit of three bits, so rows and columns are capped at 8 unless implementation proves a wider safe bound.

## Generator Input

The generator consumes `finepaper-ipcore-graph-v1`.

It uses:

- `modules[]` to find `OpenNoCXP` and agent module records;
- `connections[]` to infer mesh adjacency and `p0/p1` agent attachments;
- `ipcore_state[].state.global_parameters` for flit widths and optional package-wide defaults.

Global parameters for the first version:

- `req_flit_width`, default 128.
- `rsp_flit_width`, default 64.
- `dat_flit_width`, default 256.
- `snp_flit_width`, default 128.

These values are used by verification and written into the manifest. The upstream generated wrapper remains parameterized.

## DRC

OpenNoC DRC validates:

- schema is `finepaper-ipcore-graph-v1`;
- selected IP core is `finepaper.opennoc`;
- at least one `OpenNoCXP` exists;
- XP coordinates form a complete rectangle;
- coordinates are integer and within `0..7` for both axes;
- mesh links match the rectangular mesh exactly;
- duplicate mesh links are rejected;
- non-adjacent mesh links are rejected;
- each `p0` and `p1` slot has at most one agent connection;
- each dragged agent has exactly one XP slot connection;
- agent-to-agent and agent-to-mesh-side links are rejected;
- only `OpenNoCRNF/RNI/HNF/HNI/SNF` may connect to `p0/p1`;
- ring topology is rejected with a clear "mesh only" error.

Coordinate inference should follow the robust RaveNoC pattern:

1. prefer explicit `mesh_col` and `mesh_row`;
2. infer from mesh connections when explicit coordinates are unavailable;
3. fall back to normalized canvas `x/y` placement.

The final generated upstream JSON is always rectangular.

## OpenNoC Mesh JSON Projection

The adapter projects the graph to the upstream shape:

```json
{
  "XP0_0": { "X": 0, "Y": 0, "P0": "RNF", "P1": "NONE" },
  "XP1_0": { "X": 1, "Y": 0, "P0": "HNF", "P1": "NONE" },
  "XP0_1": { "X": 0, "Y": 1, "P0": "HNI", "P1": "NONE" },
  "XP1_1": { "X": 1, "Y": 1, "P0": "SNF", "P1": "NONE" }
}
```

XP names come from `external_id` when present, otherwise from the exported module id. Names must be valid SystemVerilog identifiers after the same sanitization used for generated artifacts.

Unconnected XP slots are emitted as `NONE`.

## Upstream Generator Invocation

The Ruby generator writes the projected mesh JSON into a temporary working area, then invokes:

```bash
python3 mesh_gen.py -f <generated-mesh-config.json>
```

The command must run with current working directory:

```text
ipcores/opennoc/vendor/OpenNoC/tools/mesh_generator
```

The upstream script currently loads templates through a relative `template` path, so invoking it from another directory is not valid.

The adapter copies the generated `mesh_wrapper_<cols>x<rows>.sv` into the Finepaper output directory after the upstream process exits successfully.

## Generated Artifacts

Given a valid graph, the generator writes:

- `opennoc_mesh.json`: the upstream mesh generator input produced from the Finepaper graph.
- `mesh_wrapper_<cols>x<rows>.sv`: upstream generated mesh wrapper.
- `tools/mesh_generator/chi_xp_node.sv`: copied support module.
- `rtl/misc/chi_xp_channel.v`: copied support module.
- `rtl/misc/`: copied common support files needed by selected OpenNoC IP blocks.
- `rtl/include/`: copied OpenNoC include files.
- `rtl/src/rni/`: copied only when `OpenNoCRNI` appears.
- `rtl/src/hnf/`: copied only when `OpenNoCHNF` appears.
- `rtl/src/hni/`: copied only when `OpenNoCHNI` appears.
- `rtl/src/snf/`: copied only when `OpenNoCSNF` appears.
- `LICENSE`: copied upstream license.
- `opennoc_filelist.f`: generated filelist for the output bundle.
- `verify.sh`: convenience Verilator lint command.
- `manifest.json`: generated output manifest.

`OpenNoCRNF` is special in the first version. The upstream OpenNoC repository exposes `RNF` as a mesh wrapper port type, but the inspected tree does not include `rtl/src/rnf`. Therefore `OpenNoCRNF` is generated as an external RNF attachment in the wrapper and manifest. If upstream later provides RNF RTL, the generator can copy it like the other agent directories.

Do not copy upstream testbenches or `rtl/case` by default.

## Filelist And Verification

`opennoc_filelist.f` should use output-local paths only. It must not reference the vendor directory directly, because generated output should be portable after generation.

`verify.sh` should run a syntax/lint smoke using Verilator when available. The wrapper has top parameters without defaults, so the verify command must pass concrete values, for example:

```bash
verilator --lint-only --sv \
  -Wno-PINMISSING \
  -GREQ_FLIT_WIDTH=128 \
  -GRSP_FLIT_WIDTH=64 \
  -GDAT_FLIT_WIDTH=256 \
  -GSNP_FLIT_WIDTH=128 \
  -f opennoc_filelist.f \
  --top-module mesh_wrapper_2x2
```

`-Wno-PINMISSING` is allowed only for the generated wrapper path because upstream intentionally leaves unused local slot pins unconnected when `P0` or `P1` is `NONE`. Any additional warning suppression must be justified in the generator smoke test.

## Manifest

`manifest.json` records:

- `ipcore`: `finepaper.opennoc`;
- `generator`: adapter version and upstream OpenNoC commit when available;
- `topology`: `mesh`;
- `rows` and `cols`;
- flit widths;
- XP records with coordinates, artifact names, and `p0/p1` mappings;
- agent records with module type, artifact id, connected XP, connected slot, and copied RTL directory if any;
- generated wrapper name;
- verification command.

## Qt Frontend Policy

No Qt frontend files should be changed for this first version.

Expected UI behavior through existing metadata:

- OpenNoC appears in the IP catalog as a `kind: noc` IP core.
- Selecting/adding OpenNoC creates the project IP instance and global parameters.
- The active workspace module list shows `OpenNoCXP` plus the five agent module types.
- Users drag modules from the active workspace list.
- The existing mesh topology preset creates XP nodes and links.
- The property panel exposes global and module parameters from metadata.
- The existing generate and DRC actions invoke the package runtime commands.

Future subcategory grouping inside the active module list, such as "Routers" and "Agents", should be designed as a generic metadata-driven frontend enhancement. It is not required here.

## Create Wizard Future Work

The next phase is a create wizard that guides initial OpenNoC setup:

- choose topology type;
- choose rows and columns;
- choose global flit widths;
- optionally seed common agent placements;
- create the initial XP mesh and agent modules.

This first version must preserve the data model needed by that wizard. The wizard should call the same topology preset and module creation paths instead of introducing another generator-specific graph path.

## Testing Strategy

Implementation should be test-driven.

Expected tests:

- `spec_generator` emits `finepaper.opennoc` runtime metadata.
- Generated `plugin.json` has Ruby generator and DRC commands using `ipcore_graph_v1`.
- Generated `modules.xml` includes all six OpenNoC module types, ports, graph groups, parameters, and labels.
- `spec_generator --check` includes OpenNoC generated artifacts.
- Generator test projects a 2x2 Finepaper graph into expected OpenNoC mesh JSON.
- Generator test calls a fake upstream mesh generator or controlled fixture and copies the produced wrapper.
- Generator test copies selected HNF/HNI/RNI/SNF RTL directories and treats RNF as external.
- Generator test writes output-local filelist, verify script, license, and manifest.
- DRC accepts valid 2x2 mesh with legal agents.
- DRC rejects missing mesh links, duplicate links, non-rectangular meshes, out-of-range coordinates, invalid agent connections, and unconnected dragged agents.
- Missing vendor source fails with a clear setup message.
- Smoke test runs `verify.sh` when Verilator is available and skips with a clear message when it is not.

## Success Criteria

The feature is successful when:

1. A clean checkout with OpenNoC vendor initialized discovers `finepaper.opennoc`.
2. The Qt frontend shows OpenNoC modules and parameters without Qt code changes.
3. A user can create a mesh of `OpenNoCXP` nodes using the existing mesh topology preset.
4. A user can drag `OpenNoCRNF/RNI/HNF/HNI/SNF` modules and connect them to XP `p0/p1`.
5. OpenNoC DRC reports clear graph errors before generation.
6. Generation calls upstream `mesh_gen.py`, not a reimplementation.
7. The output directory contains generated wrapper, copied support RTL, copied selected agent RTL, filelist, verify script, license, mesh JSON, and manifest.
8. The generated output uses output-local paths and can be moved independently of the Finepaper source tree.
9. Existing RaveNoC and `finepaper.noc` tests still pass.
10. No Qt frontend file is modified for OpenNoC-specific behavior.
