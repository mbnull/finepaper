# Graph Data Layer Design

## Summary

Finepaper should treat the graph as a generic IP planning data model. The core graph owns nodes, interfaces, connections, parameters, and transactions. NoC-specific meaning such as mesh routers, endpoints, cardinal links, endpoint slots, and framework JSON groups must live in schema metadata, rule policies, projection adapters, and view policies.

`XP`, `Endpoint`, `east`, `west`, `north`, `south`, and `local0` are current NoC spec names. They must not be required by the graph core. A fork can rename these concepts in the spec without changing core graph operations.

## Goals

- Make `Graph` a data-structure operation layer: add, remove, clone, query, validate, and serialize generic graph elements.
- Move NoC connection semantics out of `Graph::isValidConnection()` into schema-driven graph rules.
- Move framework JSON `xps` and `endpoints` import/export out of `Graph` into a NoC projection adapter.
- Keep Qt node rendering driven by view metadata: pixel anchor coordinates, labels, layout policy, resize constraints, and fallback edge layout.
- Represent one IP-XACT-style `busInterface` as one editor interface anchor. A bidirectional interface uses `Port::Direction::InOut` or compatible role metadata, not paired `*_in` and `*_out` pseudo-ports.
- Preserve current project files and NoC generator behavior during migration.

## Non-Goals

- Replacing the Ruby NoC generator model in this change.
- Defining a full public plugin SDK for third-party graph rules.
- Breaking `.fpproj` or current NoC framework JSON compatibility.
- Removing current Qt visual behavior before a metadata-backed replacement is in place.

## Current Hardcoding

These are the seams that should be extracted:

- `qt/src/graph/graph.cpp`
  - `isMeshRouterModule()` and `isEndpointModule()` encode NoC groups in the graph core.
  - `normalizePortId()` maps `east_in`, `east_out`, `epN`, and `localN`.
  - `assignEndpointFallbackPosition()` knows endpoint slots and router dimensions.
  - `guessedRouterPorts()` assumes cardinal router sides.
  - `isRouterLink()` and `connectionUsesRouterSide()` enforce mesh semantics.
  - `loadFromJson()` and `toJsonDocument()` know framework JSON `xps` and `endpoints`.
- `qt/inc/common/portlayout.h`
  - Fixed endpoint/router counts and cardinal names are shared as general helpers.
  - Endpoint and router recognition use port id prefixes and English names.
- `qt/src/commands/arrangecommand.cpp`
  - Automatic placement is NoC mesh-specific.
- `qt/src/nodeeditor/graphnodegeometry.cpp`, `qt/src/nodeeditor/graphnodepainter.cpp`, `qt/src/nodeeditor/nodeeditorwidget.cpp`, and `qt/src/nodeeditor/straightconnectionpainter.cpp`
  - View behavior checks `mesh_router`, `endpoint`, router ports, endpoint ports, and cardinal sides directly.
- `qt/src/validation/drcrunner.cpp`
  - DRC message mapping assumes XP and Endpoint names.

## Target Architecture

### Core Data Layer

`Graph` remains the QObject owner of modules and connections, but its rules become generic:

- module id uniqueness
- connection id uniqueness
- module and port existence
- self-loop rejection
- basic direction compatibility
- bus/interface compatibility through metadata
- per-interface cardinality constraints from schema

NoC-specific behavior is delegated through injected services or static service calls that consume schema metadata.

### Schema Layer

`GraphSchema` is a read-only view over `ModuleRegistry`, `ModuleType`, `ModuleInterfaceMetadata`, and future rule metadata. It answers questions such as:

- Which module type owns an interface?
- Which bus family and role does the interface expose?
- Which interface roles are compatible?
- How many connections may use this interface?
- Does this interface belong to a topology group such as router-link or local-attachment?
- What legacy ids normalize to this interface?

The first implementation can wrap existing metadata rather than invent a new parser.

### Query Layer

`GraphQueries` contains pure lookup helpers. It must not mutate the graph and must not know NoC names except through schema legacy aliases.

Expected responsibilities:

- find a module by id
- find a port by id or interface id
- normalize external endpoint ids through schema aliases
- test whether a connection endpoint uses a specific module/interface
- enumerate occupied interfaces
- clone modules and connections into a temporary graph for validation

### Rule Layer

`GraphRuleEngine` validates a candidate connection against a graph snapshot and a schema. It returns a structured result with a code and message, while `Graph::isValidConnection()` remains a boolean compatibility wrapper.

Generic built-in rules:

- both modules exist
- both ports/interfaces exist
- source and target are not on the same module
- source supports output and target supports input, with `InOut` accepted in either position
- bus family matches
- interface roles and match fields are compatible
- endpoint occupancy respects interface cardinality

NoC rules become schema/policy rules:

- router-to-router interfaces must connect to compatible opposite interfaces
- router local attachment interfaces accept endpoint NoC interfaces
- local attachment interfaces have capacity one unless metadata says otherwise
- legacy `east_in`, `east_out`, and `epN` ids normalize through metadata aliases

### Projection Layer

`GraphProjection` converts between the generic graph and external formats. The graph core should not know those formats.

Initial projections:

- editor project JSON used by `.fpproj`
- NoC framework JSON with `xps`, `endpoints`, and router links
- XML export generated from editor JSON

The NoC framework projection owns terms such as `xps`, `endpoints`, endpoint lists, and mesh coordinate inference.

### View Layer

Qt reads module view metadata and exposes a generic node view model:

- anchor id
- pixel anchor position
- preferred normal vector
- label and label position
- collapsed-state visibility
- auto-layout group
- fallback edge layout policy
- resize constraints

The graph model does not decide where an anchor is drawn. It only stores the node, interface, and connection data required by the editor.

## Data Model

The current `Module`, `Port`, `Connection`, and `PortRef` can stay as storage primitives. The new abstraction adds service structs around them:

```cpp
struct GraphEndpoint {
    QString moduleId;
    QString interfaceId;
    QString portId;
};

struct GraphValidationResult {
    enum class Code {
        Valid,
        SameModule,
        MissingModule,
        MissingPort,
        DirectionMismatch,
        BusMismatch,
        InterfaceMismatch,
        InterfaceCapacityExceeded,
        PolicyRejected
    };

    Code code = Code::Valid;
    QString message;

    bool valid() const { return code == Code::Valid; }
};

struct GraphConnectionCandidate {
    PortRef source;
    PortRef target;
};
```

`PortRef` remains the persisted connection endpoint for compatibility. `GraphEndpoint` is the resolved view after schema lookup.

## Rule Metadata

The NoC spec can express the current rules without embedding them in C++ graph code:

```yaml
connection_rules:
  router_link:
    match:
      bus: router_link
      endpoint_count: 2
    cardinal:
      north: south
      south: north
      east: west
      west: east
    capacity:
      per_interface: 1
  endpoint_attachment:
    match:
      source_role: attachment
      target_role: endpoint
    capacity:
      per_interface: 1
```

Legacy aliases should be metadata, not C++ string rules:

```yaml
interfaces:
  east:
    aliases: [east_in, east_out]
  local0:
    aliases: [ep0]
```

If the current generator cannot emit this metadata in the first patch, the Qt side can introduce an internal `NoCGraphPolicy` adapter that computes the same rules from existing `graph_group`, `role`, `bus_type`, `interface`, and view anchor fields. That adapter is transitional and must be isolated from `Graph`.

## Serialization

`.fpproj` should continue to store:

- graph modules
- graph connections
- module parameters
- view state such as position, size, and collapse state

Malformed project JSON must still be rejected before mutating the live graph.

NoC framework JSON should move to a projection:

```cpp
class NoCGraphProjection {
public:
    static bool importFrameworkJson(const QJsonObject& object,
                                    Graph& graph,
                                    QString* errorMessage);
    static QJsonDocument exportFrameworkJson(const Graph& graph,
                                             const QString& designName,
                                             QHash<QString, QString>* externalToInternalIds);
};
```

`Graph::loadFromJson()` and `Graph::toJsonDocument()` can delegate during migration, then later shrink to editor graph serialization only.

## Qt View Behavior

Qt should resolve visual connection points in this order:

1. Authored anchor for the interface id.
2. Authored anchor for the visible port id.
3. Layout policy for the module type.
4. Generic fallback edge layout.

Fallback edge layout must use interface count and labels from metadata. It should not infer router semantics from names like `east` or `west`.

Endpoint auto-flip is view behavior. When a single-interface endpoint is connected to a larger module, its visible anchor should face the connected module based on node positions and preferred normals. The graph connection remains the same.

## Migration Plan

1. Extract pure graph query helpers from `graph.cpp`.
2. Add `GraphRuleEngine` and make `Graph::isValidConnection()` delegate to it.
3. Move NoC mesh rules into a `NoCGraphPolicy` with a schema-backed interface.
4. Move framework JSON import/export into `NoCGraphProjection`.
5. Move endpoint fallback placement and router port guessing into the NoC projection or arrangement policy.
6. Replace direct `PortLayout` calls in Qt geometry and painter with metadata-backed view policy calls.
7. Keep `PortLayout` as a compatibility layer only for legacy ids until spec aliases replace it.
8. Update validation and DRC mapping to use graph groups and projection maps instead of XP/Endpoint names.

## Testing Requirements

- Unit tests for `GraphQueries`:
  - exact port lookup
  - interface id lookup
  - legacy alias normalization
  - occupancy detection for `Input`, `Output`, and `InOut`
- Unit tests for `GraphRuleEngine`:
  - generic port and direction rejection
  - bus mismatch rejection
  - interface role compatibility
  - `InOut` bidirectional acceptance
  - per-interface capacity rejection
  - temporary graph validation before live mutation
- Projection tests:
  - legacy NoC framework JSON imports to the same graph as today
  - exported framework JSON matches current generator expectations
  - endpoint connection ordering does not matter
- Qt tests:
  - node editor geometry uses anchors through metadata
  - fallback layout works for modules without view anchors
  - endpoint anchor flips visually based on connected node position
- Project tests:
  - malformed graph arrays are rejected
  - failed connection validation does not clear or partially replace the live document

## Acceptance Criteria

- `qt/src/graph/graph.cpp` no longer contains NoC terms such as `mesh_router`, `endpoints`, `east`, `west`, `north`, `south`, `ep0`, `xps`, or framework endpoint layout code except through calls to projection or policy classes.
- `Graph::isValidConnection()` delegates to a rule engine.
- NoC framework import/export is implemented in a NoC projection file.
- Qt visual behavior is driven by view metadata or a named view policy, not by graph core code.
- Existing Qt, project, spec generator, and NoC generator tests pass.
