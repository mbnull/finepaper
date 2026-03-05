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

## Configuration System

**Schema-driven config with reflection, defaults, type checking, and DRC validation.**

### Schema Definition

Components define config schemas via `config_schema` class method:

```ruby
class Xp
  def self.config_schema
    {
      routing_algorithm: { type: :string, default: 'xy' },
      vc_count: { type: :integer, default: 2 },
      buffer_depth: { type: :integer, default: 8 }
    }
  end
end
```

### Config Fields

**XP config:**
- `routing_algorithm` (string, default: 'xy') — routing algorithm: 'xy', 'west_first', 'adaptive'
- `vc_count` (integer, default: 2) — virtual channel count, must be 1-8
- `buffer_depth` (integer, default: 8) — buffer depth, must be > 0

**Endpoint config:**
- `buffer_depth` (integer, default: 16) — buffer depth, must be > 0
- `qos_enabled` (boolean, default: false) — QoS support flag

### Parsing & Validation

- `JsonParser.parse_config(json, schema)` validates types, rejects unknown fields, applies defaults
- Constructor merges defaults with user config: `schema.transform_values { |v| v[:default] }.merge(config)`
- DRC checks validate config values against constraints

## Key Conventions

- DRC checks: subclass `DrcBase`, implement `check(noc)` returning `[]` or error strings
- Plugins: subclass `PluginBase`, implement `process(noc, context)`
- ERB templates access the current XP as `@xp`; call `@xp.neighbors(@noc)` for connected XPs
- Topology: either specify `xps` array explicitly OR use `parameters.mesh` (width/height) for auto-expansion
- Config schemas: define via `config_schema` class method; parser validates types and rejects unknown fields
- Multi-endpoint XPs: NI template generates `NUM_ENDPOINTS` parameter and per-endpoint ports
- `ipcore/` is gitignored — contains reference IP core SV/V files

## DRC Checks

**XP validation:**
- `UniqueXpIds` — no duplicate XP IDs
- `ValidXpConfig` — config values match schema types
- `XpRoutingAlgorithm` — routing_algorithm in ['xy', 'west_first', 'adaptive']
- `XpVirtualChannels` — vc_count in range 1-8
- `XpBufferDepth` — buffer_depth > 0

**Endpoint validation:**
- `UniqueEndpointIds` — no duplicate endpoint IDs
- `ValidEndpointConfig` — config values match schema types
- `EndpointBufferDepth` — buffer_depth > 0
- `EndpointProtocol` — protocol not empty

## Testing

Test suite in `test/test_generator.rb` covers:
- JSON parsing with defaults and required field validation
- Schema reflection (`config_schema` for Xp and Endpoint)
- Config parsing with type checking and unknown field rejection
- Topology expansion (mesh generation, 2x2 and 3x3)
- DRC validation (unique IDs, config types, routing algorithms, VC count, buffer depths, protocols)
- Verilog port parsing
- Device catalog generation
- Node ID calculation
- Multi-endpoint NI generation (NUM_ENDPOINTS parameter, per-endpoint ports)
- RTL generation (structural, golden file regression, verilator lint)
