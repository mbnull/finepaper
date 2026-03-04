# CLAUDE.md

This file provides guidance to Claude Code when working with this NoC IP core generation framework.

## Quick Start

```bash
# Run generator
ruby bin/generate -i examples/simple_mesh.json -o /tmp/out -t template/

# Run tests
ruby test/test_generator.rb
```

## Architecture

**Pipeline:** JSON → TopologyExpander → DRC → Plugins → ERB → SystemVerilog

### Core Components

- `bin/generate` — CLI entry point; iterates XPs, sets `@xp` on noc via `instance_variable_set`, calls `gen.render` per XP
- `src/ruby/model/` — Data model classes
  - `NocConfig` — Top-level config; `#expose` sets `@noc = self` and returns `binding` for ERB
  - `Xp` — Crosspoint router; `#node_id(noc)` calculates position-based ID
  - `Connection` — Links between XPs with direction
  - `Endpoint` — Network interface (NI) with protocol info
  - `NocConfig#catalog` — Groups endpoints by role (masters/slaves) and protocol
- `src/ruby/parser/` — Input parsers
  - `json_parser.rb` — `JsonParser.parse(path)` → `NocConfig`
  - `verilog_parser.rb` — `VerilogParser.parse(path)` → `ModuleIface` with ports
- `src/ruby/topology/topology_expander.rb` — `TopologyExpander.expand(noc)` generates XPs/connections from `parameters.mesh`
- `src/ruby/drc/drc_runner.rb` — `DrcBase#check(noc)` returns error strings; `DrcRunner` raises on violations
- `src/ruby/plugin/plugin_base.rb` — `PluginBase#process(noc, context)`; `PluginRunner` runs all registered plugins
- `src/ruby/generator/rtl_generator.rb` — `RtlGenerator#render(template, output_path)` evaluates ERB with `noc.expose` binding
- `template/xp.sv.erb` — SystemVerilog XP router template; has access to `@xp`, `@noc`, `@parameters`, `@connections`

## Key Conventions

- DRC checks: subclass `DrcBase`, implement `check(noc)` returning `[]` or error strings
- Plugins: subclass `PluginBase`, implement `process(noc, context)`
- ERB templates access the current XP as `@xp`; call `@xp.neighbors(@noc)` for connected XPs
- Topology: either specify `xps` array explicitly OR use `parameters.mesh` (width/height) for auto-expansion
- `ipcore/` is gitignored — contains reference IP core SV/V files

## Testing

Test suite in `test/test_generator.rb` covers:
- JSON parsing
- Topology expansion (mesh generation)
- DRC validation (unique IDs)
- Verilog port parsing
- Device catalog generation
- Node ID calculation
- RTL generation (structural, golden file regression, verilator lint)
