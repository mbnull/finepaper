# NoC Interface Anchors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make NoC editor connection points spec-defined interface anchors instead of hard-coded `XP`/`Endpoint` ports.

**Architecture:** The YAML spec defines module interfaces, labels, bus roles, and visible anchor-port ids. View XML defines pixel anchor positions. Qt loads anchors into `ModuleType`, renders ports at those positions, and validates connections through existing interface compatibility metadata.

**Tech Stack:** Ruby `spec_generator`, Qt/C++ module provider and node editor, xmake Qt tests, Ruby minitest NoC generator tests.

---

## File Structure

- Modify `spec_generator/lib/spec_generator.rb`: remove fixed module-name validation, parse interface labels, emit one editor-visible port per interface, copy `<anchors>` from view XML, and map Ruby backend classes by `graph_group`.
- Modify `spec_generator/bin/spec-gen`: update repository defaults to `spec/noc/noc.yaml`, `spec/noc/views`, `plugins/noc`, and `plugins/noc/generator/src/ruby/model`.
- Modify `spec_generator/README.md`: document current paths and interface anchor generation.
- Modify `spec_generator/test/spec_generator_test.rb`: add renamed-module coverage and assert anchor XML and one-port-per-interface output.
- Modify `spec/noc/noc.yaml`: change visible router interfaces to `north/east/south/west`, use complementary `initiator`/`target` roles for `router_link`, and give interfaces labels.
- Modify `spec/noc/views/XP.xml` and `spec/noc/views/Endpoint.xml`: replace `<interfaces>` placement metadata with pixel `<anchors>`.
- Modify generated bundles `plugins/noc/modules.xml`, `plugins/noc/graphics/*.xml`, `qt/bundles/modules.xml`, and `qt/bundles/graphics/*.xml` by running `spec_generator/bin/spec-gen`.
- Modify `qt/inc/modules/moduleregistry.h`: add `ModuleInterfaceAnchor` metadata.
- Modify `qt/inc/modules/moduletypemetadata.h`: add anchor lookup helpers.
- Modify `qt/src/modules/moduleprovider.cpp`: parse `<anchors>` from module graphics overlays.
- Modify `qt/src/nodeeditor/graphnodegeometry.cpp` and `qt/inc/nodeeditor/graphnodegeometry.h`: position ports and labels from anchors before layout-specific fallbacks.
- Modify `qt/src/nodeeditor/graphnodepainter.cpp`: draw anchor labels from view/spec metadata.
- Modify `qt/src/graph/graph.cpp`: normalize legacy port ids to interface ids during legacy JSON import/export edge handling.
- Modify `qt/test/graph_test.cpp` and `qt/test/projectdocument_test.cpp`: assert interface ids and anchor metadata.

## Tasks

### Task 1: Red Tests For Generator Interface Anchors

**Files:**
- Modify: `spec_generator/test/spec_generator_test.rb`

- [ ] **Step 1: Write failing tests**

Add a test that uses renamed module keys such as `RouterTile` and `NetworkPort`, renamed view XML files, and asserts:

```ruby
assert_includes modules_xml, '<module name="RouterTile"'
assert_includes modules_xml, '<port id="east" direction="output" type="bus" bus_type="router_link" role="router" name="East" description="East router interface" interface="east" />'
assert_includes graphics_xml, '<anchors>'
assert_includes graphics_xml, '<anchor ref="east" x="136" y="58" normal_x="1" normal_y="0" label="East" label_x="112" label_y="58" />'
assert File.file?(File.join(dir, 'framework/src/ruby/model/xp.rb'))
assert File.file?(File.join(dir, 'framework/src/ruby/model/endpoint.rb'))
```

- [ ] **Step 2: Verify red**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: FAIL because `spec_generator` rejects modules that are not exactly `XP` and `Endpoint`, and because anchors are not emitted.

### Task 2: Generator Semantic Module Support

**Files:**
- Modify: `spec_generator/lib/spec_generator.rb`
- Modify: `spec_generator/bin/spec-gen`
- Modify: `spec_generator/README.md`

- [ ] **Step 1: Implement minimal generator changes**

Remove the fixed module-name check. Add interface `label` to the accepted interface keys. In `RubyModelEmitter`, find router and endpoint modules by `graph_group` values `xps` and `endpoints` instead of by names. Keep output filenames `xp.rb` and `endpoint.rb` for the private NoC backend.

- [ ] **Step 2: Emit interface anchor ports**

Change `QtBundleEmitter#port_lines` so it emits one visible port per interface. Use the interface `port` block when present. Validate in the parser that `port.id` matches the interface id for new specs.

- [ ] **Step 3: Copy anchor XML from views**

Extend `View` to carry `anchors_xml`. Parse `<anchors>...</anchors>` from view XML, validate every `<anchor ref="...">` references a declared interface, and write anchors into `graphics/<module>.xml` inside `<module-graphics>`.

- [ ] **Step 4: Verify green**

Run:

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: PASS.

### Task 3: Update NoC Spec And Generated Bundles

**Files:**
- Modify: `spec/noc/noc.yaml`
- Modify: `spec/noc/views/XP.xml`
- Modify: `spec/noc/views/Endpoint.xml`
- Generated: `plugins/noc/modules.xml`
- Generated: `plugins/noc/graphics/XP.xml`
- Generated: `plugins/noc/graphics/Endpoint.xml`
- Generated: `qt/bundles/modules.xml`
- Generated: `qt/bundles/graphics/XP.xml`
- Generated: `qt/bundles/graphics/Endpoint.xml`

- [ ] **Step 1: Convert NoC interfaces**

Use `north/east/south/west` as visible router interface ids. Set `east` and `south` to `role: initiator`, `direction: output`; set `north` and `west` to `role: target`, `direction: input`. Set local endpoint slots `local0..local3` to visible port ids with `direction: input`, and endpoint `noc` to `direction: output`.

- [ ] **Step 2: Add view anchors**

Replace old `<interfaces>` placement blocks with pixel `<anchors>` blocks. Use expanded node coordinates for router and endpoint anchors.

- [ ] **Step 3: Regenerate bundles**

Run:

```bash
ruby spec_generator/bin/spec-gen
python3 qt/tools/convert_module_bundle.py --xml plugins/noc/modules.xml --output-dir qt/bundles
```

Expected: `plugins/noc` and `qt/bundles` contain modules and graphics where visible ports are interface ids and graphics contain anchors.

### Task 4: Qt Loader Anchor Metadata

**Files:**
- Modify: `qt/inc/modules/moduleregistry.h`
- Modify: `qt/inc/modules/moduletypemetadata.h`
- Modify: `qt/src/modules/moduleprovider.cpp`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Write failing Qt loader test**

In `graph_test.cpp`, extend the bundle-loading test to assert `XP` has an `east` anchor with pixel coordinates and that its visible ports include `east`, not `east_in` or `east_out`.

- [ ] **Step 2: Verify red**

Run:

```bash
xmake build graph_test && xmake run graph_test
```

Expected: FAIL because anchors are not parsed into `ModuleType`.

- [ ] **Step 3: Implement anchor metadata**

Add a `ModuleInterfaceAnchor` struct with `interfaceId`, `x`, `y`, optional `normalX`, `normalY`, `label`, `labelX`, and `labelY`. Parse `<anchors><anchor ... /></anchors>` from module graphics overlays into `ModuleType::interfaceAnchors`.

- [ ] **Step 4: Verify green**

Run:

```bash
xmake build graph_test && xmake run graph_test
```

Expected: PASS.

### Task 5: Qt Geometry And Painter Use Anchors

**Files:**
- Modify: `qt/src/nodeeditor/graphnodegeometry.cpp`
- Modify: `qt/inc/nodeeditor/graphnodegeometry.h`
- Modify: `qt/src/nodeeditor/graphnodepainter.cpp`
- Modify: `qt/test/graph_test.cpp`

- [ ] **Step 1: Write failing geometry assertions**

Add focused assertions through available metadata and graph behavior: `east` and `west` are the only visible router-link port ids loaded from the bundle, and their names are interface labels. Geometry itself is exercised by existing node editor tests during Qt test runs.

- [ ] **Step 2: Implement anchor-first geometry**

In `portPosition`, look up an anchor by `port.interfaceId()` or `port.id()` before layout-specific logic. Scale `x/y` from the expanded node geometry to the current rendered node size. In `portTextPosition`, use `label_x/label_y` when present.

- [ ] **Step 3: Draw anchor labels**

In `GraphNodePainter`, draw label text for anchored ports using `portTextPosition`. Keep existing fallback rendering for modules without anchors.

- [ ] **Step 4: Verify green**

Run:

```bash
QT_QPA_PLATFORM=offscreen xmake test
```

Expected: all Qt tests pass.

### Task 6: Legacy Import And Project Interface IDs

**Files:**
- Modify: `qt/src/graph/graph.cpp`
- Modify: `qt/test/graph_test.cpp`
- Modify: `qt/test/projectdocument_test.cpp`

- [ ] **Step 1: Write failing compatibility tests**

Add tests proving legacy `dir: east` imports as a connection from `east` to `west`, old `ep0` endpoint references import as `local0`, and project round-trip preserves interface ids.

- [ ] **Step 2: Verify red**

Run:

```bash
xmake build graph_test projectdocument_test && xmake run graph_test && xmake run projectdocument_test
```

Expected: FAIL because legacy mapping still produces `east_out`, `west_in`, and `ep0`.

- [ ] **Step 3: Implement normalization**

Add helpers in `graph.cpp` to resolve a requested endpoint id to a visible interface port:

```cpp
QString normalizePortId(const Module* module, const QString& requested);
```

The helper first returns exact matches, then maps legacy directional ids through `PortLayout::routerSideId`, then maps `epN` to `localN` when present.

- [ ] **Step 4: Update export edge handling**

When framework JSON is exported, router-to-endpoint links must be recognized even if the connection is stored as endpoint `noc` to router `localN`. Populate the router endpoint list regardless of source-target ordering.

- [ ] **Step 5: Verify green**

Run:

```bash
QT_QPA_PLATFORM=offscreen xmake test
```

Expected: all Qt tests pass.

### Task 7: Full Verification

**Files:**
- No new files.

- [ ] **Step 1: Run generator tests**

```bash
ruby spec_generator/test/spec_generator_test.rb
```

Expected: PASS.

- [ ] **Step 2: Run NoC generator tests**

```bash
ruby plugins/noc/generator/test/test_generator.rb
```

Expected: PASS.

- [ ] **Step 3: Run Qt tests**

```bash
QT_QPA_PLATFORM=offscreen xmake test
```

Expected: 100% tests pass.

- [ ] **Step 4: Check diff hygiene**

```bash
git diff --check
```

Expected: no output.

## Self-Review

- Spec coverage: interface semantics, pixel anchors, fallback behavior, legacy mapping, and NoC correctness are covered by Tasks 1-7.
- Placeholder scan: no placeholders remain.
- Type consistency: `ModuleInterfaceAnchor`, `interfaceAnchors`, and `normalizePortId` names are used consistently.
