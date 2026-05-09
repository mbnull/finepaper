# V1 Architecture Readiness Notes

Date: 2026-05-09

## Archive Baseline

- Node 1: `d685a76` `archive: complete node-1 spec source of truth`
- Node 2: `186dcc3` `archive: complete node-2 ipcore vocabulary migration`
- Node 3: `8fcab74` `archive: complete node-3 project ip services`
- Node 4: `6068a0a` `archive: complete node-4 ip catalog ui`
- Node 5: `c0adb8c` `archive: complete node-5 scoped workspace tools`
- Node 6: `0272da1` `archive: complete node-6 connection semantics split`
- Node 7: `018ff77` `archive: complete node-7 ipcore generation boundary`
- Node 8: `2a357e8` `archive: complete node-8 historical cleanup`

## Mainline Gate

`v1architecturegate_test` verifies the repository Finepaper NoC flow:

- discover `finepaper.noc` from `generated/ipcores`;
- add/select one NoC IP instance;
- expose active workspace modules and topology presets;
- create a 2x2 mesh graph;
- save/read/load `.fpproj` with IP-instance state;
- export `finepaper-ipcore-graph-v1`;
- run DRC and generation through the selected IP core command descriptors;
- write a generated `.fpproj` snapshot beside RTL output.

## Verification Evidence

- `CCACHE_DISABLE=1 xmake run -P qt v1architecturegate_test`: passed.
- `CCACHE_DISABLE=1 xmake test -P qt`: 18/18 Qt tests passed.
- `CCACHE_DISABLE=1 xmake -P qt -r qt`: release app target built successfully.
- `ruby spec_generator/test/spec_generator_test.rb`: 22 runs, 152 assertions, 0 failures, 0 errors.
- `ruby spec_generator/bin/spec-gen --check`: generated IP core runtime artifacts are up to date.
- `ruby ipcores/finepaper-noc/generator/test/test_generator.rb`: 57 runs, 290 assertions, 0 failures, 0 errors.
- `ruby ipcores/ravenoc/generator/test/test_generator.rb`: 15 runs, 115 assertions, 0 failures, 0 errors.
- `ruby ipcores/ravenoc/generator/test/test_smoke.rb`: 1 run, 4 assertions, 0 failures, 0 errors.
- Hard live-code stale scan: only `qt/test/ipcatalogpanel_test.cpp` absence assertions for removed `activeIpCombo` and `paletteDock` were reported.
- Legacy project-field stale scan: only explicit pre-v1 rejection and writer-not-emitting coverage in `qt/src/project/projectreader.cpp` and `qt/test/projectdocument_test.cpp` was reported.
- `git diff --check`: no whitespace errors.

## Residual Notes

- Runtime plugin infrastructure names remain intentionally: `PluginRegistry`, `PluginDescriptor`, `PluginCommandDescriptor`, `plugin.json`, native plugin metadata, and `FINEPAPER_PLUGIN_PATH`.
- Explicit pre-v1 `.fpproj` rejection guards remain intentionally in `ProjectReader` and `projectdocument_test`.
- No compatibility promise is made for pre-v1 project files.
- Local helper artifacts `.codex/`, `.superpowers/`, and `image.png` were not staged for the Node 9 archive.
