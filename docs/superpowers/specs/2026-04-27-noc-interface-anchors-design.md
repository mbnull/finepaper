# NoC Interface Anchors Design

## Summary

Finepaper will treat editor connection points as interface anchors. An anchor is the visual and graph-level handle for a module interface, aligned with IP-XACT `busInterface` semantics. It is not an HDL port and not a pair of legacy directional wires.

The current `XP` and `Endpoint` module names are temporary spec-defined names. Qt and `spec_generator` must not require those exact names. The NoC backend may keep private `Xp` and `Endpoint` Ruby classes for now, but the mapping into those classes must come from semantic metadata such as `graph_group`, not from module type names.

## Goals

- Let `spec/noc/noc.yaml` define module names, interface ids, interface labels, bus roles, and compatibility.
- Let `spec/noc/views/*.xml` define node geometry and pixel-based anchor positions.
- Render and connect Qt nodes through one visible anchor per interface.
- Model router directions such as `east` as one interface anchor, not `east_in` and `east_out`.
- Keep NoC links compatible with IP-XACT by using complementary roles such as `initiator` and `target`.
- Preserve legacy JSON and old bundle compatibility by mapping old port ids back to their owning interface where possible.
- Keep fallback behavior: missing anchor positions are laid out automatically on a default edge, with labels from the spec.

## Non-Goals

- Building the future generic IP planning fork in this repository.
- Implementing full IP-XACT export in this change; that remains covered by the existing IP-XACT export design.
- Removing the private Ruby NoC generator model names in this phase.
- Replacing the legacy framework JSON shape with a new generic graph interchange in this phase.

## Spec Model

The semantic spec owns interface identity:

```yaml
modules:
  RouterTile:
    graph_group: xps
    interfaces:
      east:
        label: East
        bus: router_link
        role: initiator
        port:
          id: east
          direction: output
          type: bus
          bus_type: router_link
          role: router
          name: East
          description: East router interface
      west:
        label: West
        bus: router_link
        role: target
        port:
          id: west
          direction: input
          type: bus
          bus_type: router_link
          role: router
          name: West
          description: West router interface
```

The `port` block in this phase is the editor-visible interface anchor projection. It must use the interface id as the visible connection id for new specs. Old generated or imported bundles may still carry `east_in` and `east_out`; loaders should normalize through the `interface` attribute when present.

`router_link` should use complementary roles rather than a custom `peer` role. A UI connection remains one bidirectional logical link because the bus abstraction can carry signals in both directions. Qt does not expose individual logical signals or RTL pins.

## View Model

View XML owns visual geometry:

```xml
<module-view schema="v1" module="RouterTile">
  <graphics layout="mesh_router" node_color="#7cb9e8" supports_collapse="true">
    <expanded min_width="136" height="116" caption_left="30" caption_top="6" port_inset="16" />
    <collapsed min_width="104" height="92" caption_left="30" caption_top="26" endpoint_inset="18" />
    <arrangement endpoint_offset_x="156" mesh_spacing_x="220" mesh_spacing_y="168" />
  </graphics>
  <anchors>
    <anchor ref="north" x="68" y="0" normal_x="0" normal_y="-1" label="North" label_x="68" label_y="18" />
    <anchor ref="east" x="136" y="58" normal_x="1" normal_y="0" label="East" label_x="112" label_y="58" />
    <anchor ref="south" x="68" y="116" normal_x="0" normal_y="1" label="South" label_x="68" label_y="98" />
    <anchor ref="west" x="0" y="58" normal_x="-1" normal_y="0" label="West" label_x="24" label_y="58" />
  </anchors>
</module-view>
```

Coordinates are pixel coordinates in the expanded node baseline coordinate system. Qt scales anchors on both axes if the rendered node grows beyond the declared geometry because of a long caption or label.

Collapsed mesh-router views are intentionally handled by editor layout rules rather than separate XML semantics in this phase. A collapsed router places router interfaces on the four cardinal edges and hides local endpoint anchors; expanded routers use the authored anchors.

`normal_x` and `normal_y` describe the preferred line exit direction. They are drawing hints, not compatibility rules.

## Qt Runtime Behavior

Qt loads interface metadata and anchor metadata into `ModuleType`.

When a node has an anchor for a visible port/interface id:

- `GraphNodeGeometry::portPosition()` uses the anchor pixel position.
- `GraphNodeGeometry::portTextPosition()` uses `label_x` and `label_y` when provided.
- `GraphNodePainter` displays the interface label.
- Connection validation continues through interface bus, role, accepted values, and match fields.

When no anchor exists:

- Qt falls back to the existing generic edge layout.
- The fallback label is the interface or port name from the spec.

## Project And Legacy Compatibility

The current project document can continue storing endpoint ids in its existing `port` field during this phase, but for new NoC bundles that value is an interface id such as `east`, `west`, or `local0`.

Legacy ids are accepted:

- `east_in` and `east_out` normalize to `east` when the loaded module has an `east` interface anchor.
- `north_in` and `north_out` normalize to `north`.
- Existing legacy framework JSON that contains `dir: east` maps to `east -> west`.
- Old router endpoint slots `ep0..ep3` map to `local0..local3` when those interfaces exist.

## Testing Requirements

- `spec_generator` must accept renamed NoC module keys and still emit backend Ruby model files from `graph_group` semantics.
- Generated bundle XML must contain one visible `port` per interface anchor.
- Generated graphics XML must contain `<anchors>`.
- Qt module loading must parse anchors and expose them in `ModuleType`.
- Qt graph tests must prove router links connect through interface ids such as `east` and `west`.
- Project save/load must preserve interface ids.
- Existing NoC generator tests must still pass.
