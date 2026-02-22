# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Run the generator

```bash
ruby bin/generate -i examples/simple_mesh.json -o /tmp/out -t template/
```

Generates one `.sv` file per XP into the output directory.

## Architecture

**Pipeline:** JSON → Parser → DRC → Plugins → ERB → SystemVerilog

- `bin/generate` — CLI entry point; iterates XPs, sets `@xp` on noc via `instance_variable_set`, calls `gen.render` per XP
- `src/ruby/model/` — `NocConfig`, `Xp`, `Connection`, `Endpoint`; `NocConfig#expose` sets `@noc = self` and returns `binding` for ERB context
- `src/ruby/parser/json_parser.rb` — `JsonParser.parse(path)` → `NocConfig`
- `src/ruby/drc/drc_runner.rb` — `DrcBase#check(noc)` returns error strings; `DrcRunner` raises on violations
- `src/ruby/plugin/plugin_base.rb` — `PluginBase#process(noc, context)`; `PluginRunner` runs all registered plugins
- `src/ruby/generator/rtl_generator.rb` — `RtlGenerator#render(template, output_path)` evaluates ERB with `noc.expose` binding
- `template/xp.sv.erb` — SystemVerilog XP router template; has access to `@xp`, `@noc`, `@parameters`, `@connections`

## Key conventions

- DRC checks: subclass `DrcBase`, implement `check(noc)` returning `[]` or error strings
- Plugins: subclass `PluginBase`, implement `process(noc, context)`
- ERB templates access the current XP as `@xp`; call `@xp.neighbors(@noc)` for connected XPs
- `ipcore/` is gitignored — contains reference IP core SV/V files
