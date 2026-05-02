# RaveNoC Plugin Adaptation Design

## Purpose

Finepaper will add RaveNoC as the first external third-party IP plugin that proves the end-to-end IP creation flow:

1. describe an IP core in plugin-owned metadata,
2. edit IP parameters in the Qt UI,
3. save and restore the project,
4. generate integration artifacts from the plugin,
5. verify the generated output with Verilator syntax/lint checks.

RaveNoC is a NoC, but this work must not create a new NoC-specific framework path. It is a demonstration of the more general IP creation plugin model. Future plugins should be able to represent NoCs, DMA blocks, UARTs, cache/coherency units, accelerator wrappers, and other IP without teaching the graph core about those domains.

## Current Context

The existing NoC demo still has several hard-coded assumptions:

- `Graph::toJsonDocument(GraphJsonFlavor::Framework)` exports legacy `xps`, `endpoints`, and `connections` arrays and drops non-XP/Endpoint modules.
- `Graph::loadFromJson()` imports only the legacy NoC JSON shape.
- `BasicValidator` contains XP-specific rules such as `isolated_xp`.
- Node editor presentation still has special behavior for `mesh_router` and `endpoints`.
- `spec_generator` intentionally targets the current NoC subset and emits NoC backend Ruby models.

Those assumptions are acceptable for the old XP/EP demo, but RaveNoC must use a generic plugin export contract so the next external IP does not require more core special cases.

## Selected Approach

Create a standalone `finepaper.ravenoc` plugin.

The plugin will own:

- RaveNoC module metadata,
- optional graphics for a single configurable IP node,
- a generator command,
- Verilator smoke verification scripts,
- a git submodule pointing at the upstream RaveNoC source.

Finepaper core will add the smallest generic export needed for plugin generators. The legacy NoC JSON export remains available for the existing `finepaper.noc` plugin, but new plugins can request or receive a generic graph export containing modules, parameters, ports, connections, plugin ids, and module type names.

## RaveNoC Source Policy

RaveNoC source will be added as a git submodule under:

```text
plugins/ravenoc/vendor/ravenoc
```

The submodule will point to `https://github.com/aignacio/ravenoc.git` and be pinned to a specific commit. This keeps the demo reproducible while preserving upstream provenance and making future updates explicit.

## Plugin Shape

The initial RaveNoC plugin layout will be:

```text
plugins/ravenoc/
  plugin.json
  modules.xml
  graphics/
    RaveNoC.xml
  generator/
    bin/generate
    src/ruby/...
    template/
      ravenoc_config.svh.erb
      ravenoc_top.sv.erb
      ravenoc_filelist.f.erb
      verify.sh.erb
  vendor/
    ravenoc/  # git submodule
```

The plugin id will be `finepaper.ravenoc`.

The first module type will be `RaveNoC`. It represents one configured RaveNoC top-level fabric, not individual routers or endpoints. It should not use the current XP/EP graph groups. A neutral group such as `ip_core` or `noc_fabric` is acceptable for display/filtering metadata, but core logic must not depend on the string.

## RaveNoC Module Metadata

The first `RaveNoC` module exposes parameters that map to RaveNoC defines and top-level parameters:

- `rows`: maps to `NOC_CFG_SZ_ROWS`.
- `cols`: maps to `NOC_CFG_SZ_COLS`.
- `flit_data_width`: maps to `FLIT_DATA_WIDTH`.
- `flit_type_width`: maps to `FLIT_TP_WIDTH`.
- `flit_buffer_depth`: maps to `FLIT_BUFF`.
- `virtual_channels`: maps to `N_VIRT_CHN`.
- `routing_algorithm`: maps to `ROUTING_ALG`, with `xy` and `yx` choices.
- `priority`: maps to `H_PRIORITY`, with low/high priority choices.
- `max_packet_flits`: maps to `MAX_SZ_PKT`.
- `axi_addr_width`: maps to `AXI_ADDR_WIDTH`.
- `axi_data_width`: maps to `AXI_DATA_WIDTH`, defaulting to the flit data width unless explicitly configured.
- `axi_cdc_required`: maps to the `AXI_CDC_REQ` top parameter, represented as a bit mask string or generated all-ones/all-zeroes policy in the first version.
- `bypass_cdc`: included in the generated wrapper/smoke path for simulation convenience.

The first version will represent RaveNoC as a single configurable block with generic ports for:

- AXI MOSI array input,
- AXI MISO array output,
- AXI clocks and resets,
- NoC clock and reset,
- IRQ array,
- CDC bypass array.

These ports are metadata for graph/UI consistency and future connection support. The first generator can generate a self-contained wrapper even when no external graph connections exist.

## Generic Generator Export

The RaveNoC generator must not depend on the legacy `xps`/`endpoints` schema. Finepaper will provide a generic plugin graph JSON for generator input. The minimal shape is:

```json
{
  "schema": "finepaper-plugin-graph-v1",
  "name": "design",
  "modules": [
    {
      "id": "node_id",
      "plugin": "finepaper.ravenoc",
      "type": "RaveNoC",
      "parameters": {
        "rows": 2,
        "cols": 2
      }
    }
  ],
  "connections": [
    {
      "id": "connection_id",
      "source": { "module": "node_a", "port": "out" },
      "target": { "module": "node_b", "port": "in" }
    }
  ]
}
```

For this first RaveNoC plugin, the generator validates that exactly one `RaveNoC` module is present. If more are present, it returns a clear error. If other plugin-owned modules are present, existing single-plugin generation rules already reject the graph.

## Generated Artifacts

Given a RaveNoC module, the generator writes:

- `ravenoc_config.svh`: generated macro overrides for RaveNoC configuration.
- `ravenoc_demo_top.sv`: a wrapper that includes/imports RaveNoC packages and instantiates `ravenoc`.
- `ravenoc_filelist.f`: filelist containing generated wrapper/config and RaveNoC submodule RTL files.
- `verify.sh`: convenience script that runs Verilator syntax/lint on the generated filelist.
- `manifest.json`: a small generated-output manifest with plugin id, source commit, parameter values, and verification command.

The wrapper will use the RaveNoC source layout observed in the upstream repository:

- `src/include/ravenoc_axi_fnc.svh`
- `src/include/ravenoc_defines.svh`
- `bus_arch_sv_pkg/amba_axi_pkg.sv`
- `src/include/ravenoc_structs.svh`
- `src/include/ravenoc_pkg.sv`
- `src/ni/*.sv`
- `src/router/*.sv`
- `src/ravenoc.sv`

If the upstream submodule is missing or not initialized, the generator fails with an actionable error telling the user to run `git submodule update --init --recursive`.

## Verilator Smoke

The verification target is a syntax/lint smoke, not the full RaveNoC cocotb regression. It proves that Finepaper generated a coherent RaveNoC integration artifact.

The generated `verify.sh` should run a command equivalent to:

```bash
verilator --lint-only --sv -f ravenoc_filelist.f --top-module ravenoc_demo_top
```

Known upstream lint noise can be suppressed with narrowly scoped Verilator flags only when needed. The initial suppression policy should be conservative and documented in the generated script.

## Qt UI Behavior

The RaveNoC module uses the existing property panel and module registry paths. The UI should not require a RaveNoC-specific widget.

The node view should be a normal plugin-owned IP node. It should avoid `mesh_router` and `endpoint` layouts unless those layouts are later generalized as plugin-declared layout policies. A simple graphics XML or fallback layout is enough for the first version.

The property panel should expose enum choices and numeric parameters from metadata. Generator-side validation will enforce constraints that the current UI cannot fully enforce.

## Validation

First-version validation lives primarily in the plugin generator:

- `rows` and `cols` must form a legal RaveNoC mesh. Upstream asserts require at least two columns when rows is one, and at least two rows when columns is one.
- `flit_buffer_depth` must be a power of two.
- `virtual_channels` must be positive.
- `routing_algorithm` must map to `XYAlg` or `YXAlg`.
- `priority` must map to `ZeroLowPrior` or `ZeroHighPrior`.
- data widths must be positive integer values.

Finepaper core validation should not add RaveNoC-specific rules.

## Project Save/Load

`.fpproj` is the preferred project persistence format for RaveNoC. It already stores plugin id, module type, parameters, and explicit connections. This avoids the legacy NoC JSON import path.

Legacy `.json` import/export remains for the current NoC demo. RaveNoC does not need legacy JSON import.

## Testing Strategy

Implementation should be test-driven.

Expected tests:

- Plugin discovery test loads `finepaper.ravenoc`.
- Module registry test loads `RaveNoC` metadata without XP/Endpoint graph groups.
- Generic graph export test proves a non-XP module appears in generator input with parameters.
- RaveNoC generator unit tests validate generated config, filelist, wrapper, manifest, and clear errors for missing submodule or illegal parameters.
- Verilator smoke test runs against a small generated 2x2 RaveNoC configuration when Verilator is available.

Tests that require Verilator may be written as smoke scripts or Ruby tests that skip with a clear message if `verilator` is not installed. In the current environment Verilator is available, so the implementation should run the smoke.

## Architecture Debt Tracked By This Work

This feature intentionally exposes the difference between the old NoC demo path and the future IP plugin architecture.

Do not solve all of this in the first RaveNoC adaptation, but do not make it worse:

- Move new generator inputs toward generic graph export.
- Keep RaveNoC-specific validation in the plugin generator.
- Avoid adding new RaveNoC branches in `Graph`, `NodeEditorWidget`, or `BasicValidator`.
- Avoid using XP/Endpoint graph groups for the RaveNoC module.
- Treat the existing NoC plugin as a legacy/specialized plugin that can later migrate to the generic export contract.

Future work should define a general IP creation plugin framework where plugins can declare:

- module types,
- parameter schemas,
- interface schemas,
- source acquisition policy,
- generator commands,
- validation/smoke commands,
- optional layout policies,
- optional projection adapters from graph data to IP-specific generated artifacts.

## Success Criteria

The feature is successful when:

1. A clean checkout with submodules initialized discovers `finepaper.ravenoc`.
2. The UI can create a `RaveNoC` node and edit its parameters through the existing property panel.
3. A `.fpproj` containing the node round-trips without losing plugin/type/parameter data.
4. Generate writes generic graph JSON for the plugin and invokes the RaveNoC generator.
5. The generator produces RaveNoC config, wrapper, filelist, verify script, and manifest.
6. Verilator syntax/lint smoke passes for the generated 2x2 default RaveNoC configuration.
7. No new core code path special-cases RaveNoC or assumes all future external IPs are NoCs.
