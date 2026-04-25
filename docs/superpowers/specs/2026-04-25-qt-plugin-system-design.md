# Qt Plugin System Design

## Summary

Finepaper will move the Qt editor from a hard-coded NoC bundle/framework model to a startup-loaded IP plugin model. The first implementation supports directory-based plugins discovered before the main UI is built. The architecture also reserves an explicit native plugin boundary for future C++ dynamic library extensions, but native libraries are not loaded in this phase.

The existing XP/Endpoint NoC support becomes the first built-in plugin. Its current module bundle, graphics overlays, and Ruby generator template move under a `plugins/noc/` directory so the current workflow remains available while the core editor stops depending on `qt/bundles` and `../framework` as special cases.

## Goals

- Load IP capabilities from plugin directories at application startup.
- Support multiple plugin manifests in the registry model, even though the first UI/generation flow is still conservative.
- Keep module definitions and UI layout data plugin-owned.
- Replace the global framework generator path with generator metadata declared by each plugin.
- Preserve the existing NoC editor behavior through a bundled NoC plugin.
- Upgrade the Qt editor build configuration from C++20 to C++23.
- Keep the first implementation testable without adding runtime hot-reload or native plugin ABI risk.

## Non-Goals

- Runtime plugin installation, unloading, or refresh.
- Loading C++ `.so`, `.dll`, or `.dylib` native plugin binaries in this phase.
- Full multi-plugin generation orchestration.
- Rewriting the node editor interaction model to support every possible IP topology.
- Replacing the existing XML module bundle or graphics overlay formats.
- Changing the Ruby generator internals beyond moving the current sample generator under the NoC plugin.

## Plugin Discovery

Plugin discovery is startup-only. The editor scans plugin roots before `ModuleRegistry` imports module types.

Discovery priority:

1. Directories listed in `FINEPAPER_PLUGIN_PATH`, using the platform path-list separator.
2. Repository-local `plugins/` found from the current working directory or application directory.
3. Optional legacy fallback paths for `qt/bundles` only while tests and older launches are migrated.

Each plugin root contains one directory per plugin. A directory is a plugin when it contains `plugin.json`. Invalid manifests are skipped with a warning and do not stop application startup unless no module types can be loaded.

## Directory Plugin Manifest

The first manifest schema is intentionally small:

```json
{
  "id": "finepaper.noc",
  "name": "NoC",
  "version": "1.0",
  "modules": "modules.xml",
  "graphics": "graphics",
  "generator": {
    "command": "ruby",
    "args": [
      "generator/bin/generate",
      "-i",
      "{input}",
      "-o",
      "{output}",
      "-t",
      "generator/template"
    ]
  },
  "native": {
    "enabled": false,
    "library": ""
  }
}
```

Manifest paths are resolved relative to the plugin directory. `modules` points to either XML or JSON module definitions supported by the existing `ModuleProvider` layer. `graphics` points to per-module graphics overlays. `generator.command` and `generator.args` describe the external command used by the editor when generating output. `{input}` and `{output}` placeholders are substituted with the exported design JSON path and selected output directory.

The `native` object is parsed and retained as metadata only. A manifest with `native.enabled: true` is accepted, but the native library is not loaded until a later implementation adds a stable C++ interface.

## Registry Model

Add a `PluginRegistry` responsible for discovering manifests and exposing loaded plugin metadata. `ModuleRegistry` will import module types from every valid plugin manifest by composing the existing `XmlModuleTypeSource` or `JsonModuleTypeSource` with a plugin-local graphics overlay.

Each `ModuleType` gains a `pluginId` field. This lets the editor know which plugin owns a module instance without relying on type names such as `XP` or graph groups such as `xps`. Existing graph group metadata remains available because current NoC behavior still uses it for mesh-specific layout and export paths.

Type names must remain unique for the first implementation. If two plugins declare the same module type name, the later type is skipped and a warning is logged. A future namespace-aware UI can lift this restriction.

## Built-In NoC Plugin

Create `plugins/noc/` as the bundled NoC plugin:

- `plugin.json`
- `modules.xml`
- `graphics/XP.xml`
- `graphics/Endpoint.xml`
- `generator/bin/generate`
- `generator/src/ruby/**`
- `generator/template/**`
- `generator/test/**`
- `generator/examples/**`

The files come from the current `qt/bundles` and `framework` directories. The original `framework` directory can remain during the transition, but Qt generation should use the plugin generator command instead of `FrameworkPaths::resolveFrameworkPath()`.

The NoC plugin is the default behavior because it is available from the repository-local `plugins/` root.

## Generation Flow

`MainWindow::generateVerilog()` will delegate generator selection and command construction to a small plugin-aware service rather than resolving `../framework` directly.

First implementation behavior:

- Export the graph to framework-flavored JSON as today.
- Determine the set of plugin IDs used by modules in the graph.
- If no modules exist, show a user-visible error.
- If exactly one plugin is used and it declares a generator, run that plugin command.
- If multiple plugins are used, show a clear error that multi-plugin generation is not enabled yet.
- If the selected plugin has no generator, show a clear error.

This keeps the architecture compatible with future multi-plugin designs while avoiding an incomplete generation orchestrator.

## UI Layout

Plugin-owned UI layout uses the current graphics overlay mechanism. The manifest `graphics` field points to the graphics directory, and `XmlModuleGraphicsOverlay` applies layout data for the module types declared by that plugin.

The first implementation does not introduce a new UI layout schema. Existing fields such as `layout`, `node_color`, expanded/collapsed metrics, and arrangement settings remain the supported layout contract.

## Native Plugin Extension Point

The native plugin boundary is reserved but inactive:

- `plugin.json` can describe a native library.
- `PluginDescriptor` stores the native metadata.
- No `QPluginLoader` call is made in this phase.
- No C++ ABI is promised yet.

A later phase can add an interface for custom validation, module factories, editor tools, or generation orchestration without changing the directory manifest structure.

## C++23 Upgrade

The Qt app and test targets in `qt/xmake.lua` will use `set_languages("c++23")`. The implementation should not depend on new C++23 library features unless already supported by the configured compiler and Qt toolchain.

## Error Handling

- Missing plugin roots are not fatal.
- Invalid manifests are skipped with warnings.
- A plugin without module definitions is skipped.
- Duplicate module type names are skipped after the first loaded definition.
- Generator command failures reuse the existing log panel and message box behavior.
- Placeholder substitution only supports `{input}` and `{output}` in this phase; unknown placeholders remain literal so configuration mistakes are visible in logs.

## Testing

Add focused Qt tests for:

- Loading a valid plugin manifest.
- Resolving plugin-relative module and graphics paths.
- Registering module types with `pluginId`.
- Loading multiple plugin directories at startup.
- Rejecting duplicate module type names without losing the first type.
- Building generator command arguments with `{input}` and `{output}` substitutions.

Run existing Qt graph and validation tests after the migration. Run the NoC Ruby generator tests from the moved plugin generator directory to confirm the bundled generator still behaves as the current sample framework does.

## Migration Notes

The first implementation should avoid large UI rewrites. The main migration is ownership:

- Core editor owns plugin discovery and graph editing infrastructure.
- Plugins own IP module definitions, graphics layout metadata, and generator commands.
- The NoC plugin owns the current Ruby sample generator.

Hard-coded NoC behavior can remain where it describes the current editor layout and export format, but new plugin discovery and generation code should not introduce new `XP`, `Endpoint`, or `../framework` assumptions.
