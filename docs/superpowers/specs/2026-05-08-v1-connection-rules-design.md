# V1 Connection Rules And Project Boundary Design

## Context

Finepaper has not shipped a first stable project format yet. Several current paths exist to keep early prototypes working: legacy NoC JSON import/export, derived `ip_instance` compatibility JSON, router/endpoint-specific connection completion in the node editor, and domain-specific connection checks inside `Graph`.

Keeping those temporary paths while adding plugin-owned connection rules would make both `Graph` and plugins more complex. The v1 direction is to cut prototype compatibility paths and define a clean boundary:

- `Graph` owns topology storage.
- `ConnectionRuleService` owns editor-time connection resolution and rejection reasons.
- Plugin metadata declares lightweight connection rules.
- Ruby/plugin DRC remains the final correctness authority before generation.

## Goals

- Make the frontend able to ask which connections are possible without embedding IP rules in UI code.
- Keep `Graph` from becoming a universal IP/domain model.
- Avoid forcing plugins to duplicate Ruby DRC logic in C++.
- Remove temporary compatibility layers before v1 instead of preserving them.
- Use one connection rule path for hover hints, drop completion, command insertion, and project load validation.
- Keep generated output reproducible by saving the v1 `.fpproj` snapshot beside generated artifacts.

## Non-Goals

- Do not preserve pre-v1 `.fpproj` or legacy NoC JSON compatibility.
- Do not keep emitting legacy `ip_instance` compatibility JSON.
- Do not implement a full C++ DRC engine in plugins.
- Do not make `NodeEditorWidget` decide domain-specific connection rules.
- Do not make `Graph` understand RaveNoC, mesh, router side, endpoint attachment, or plugin-specific topology semantics.

## V1 Project Model

The v1 project format supports only the editor project shape:

- project metadata and schema version.
- plugin dependency records.
- `graph.modules`.
- `graph.connections`.
- opaque `plugin_state`.

Removed from v1:

- legacy NoC framework `xps` / `endpoints` import.
- legacy graph-level connection derivation for endpoint records.
- legacy `ip_instances` migration.
- generated or DRC input compatibility field `ip_instance`.
- graph save/load paths that accept prototype JSON shapes outside `.fpproj`.

If a user opens an unsupported pre-v1 file, the app should fail with a clear message that the project format is unsupported. It should not silently migrate or partially import.

## Ownership Boundaries

### Graph

`Graph` owns only topology storage and structural safety:

- module and connection ownership.
- signal emission for topology changes.
- module/port existence checks.
- self-loop rejection.
- duplicate connection rejection.
- basic endpoint reference integrity during insertion.

`Graph` should not own:

- bus/interface compatibility.
- router side rules.
- endpoint attachment rules.
- node-body connection completion.
- plugin-specific cardinality.
- plugin state.
- project file migration.

`Graph::isValidConnection()` should either become a structural guard or delegate to a rule service supplied by the caller. The long-term target is that all semantic connection decisions are outside `Graph`.

### ConnectionRuleService

`ConnectionRuleService` owns editor-time connection semantics. It is the only service the UI and command/project paths ask for connection decisions.

Responsibilities:

- Resolve user connection gestures into concrete connection options.
- Evaluate direction, bus, interface, role, match fields, and cardinality from metadata.
- Return structured rejection reasons and user-facing messages.
- Return alternatives when a gesture targets a node body or collapsed node.
- Apply plugin-declared lightweight rules.
- Keep the same result shape for interactive and non-interactive callers.

The service must be deterministic. Given the same graph, project state, plugin metadata, and candidate, it returns the same options in the same priority order.

### UI

The node editor owns interaction only:

- capture drag start and hover target.
- ask `ConnectionRuleService` for options.
- highlight allowed ports/nodes.
- show or log rejection reasons when useful.
- auto-connect only when the service returns exactly one allowed option.
- show a chooser when the service returns multiple options.

The UI must not inspect bus roles, router sides, endpoint classes, or plugin project state to decide whether a connection is legal.

### Plugin Metadata

Plugins declare lightweight editor rules in metadata/spec files. The core app interprets them.

Examples of declarative rule data:

- interface bus.
- interface role.
- compatible roles.
- required match fields.
- accepted field values.
- parameter bindings for field values.
- cardinality.
- UI autocomplete groups.
- simple topology relation such as opposite side.

This keeps plugin complexity bounded. Plugins should not need to write arbitrary C++ connection validation for common cases.

### Ruby DRC

Ruby/plugin DRC remains the final authority for generation correctness.

It checks:

- global topology completeness.
- parameter combinations.
- generator-supported constraints.
- full plugin-specific semantic rules.
- any rule too complex for lightweight editor metadata.

The editor rule service is a fast feedback layer, not a replacement for DRC.

## Connection Candidate Model

Connection checks should model user intent and resolved port metadata, not just text IDs.

The rule service input has two phases:

1. Raw gesture input from UI or non-interactive caller.
2. Resolved semantic candidates built by core metadata lookup.

Conceptual shape:

```cpp
enum class ConnectionRequestKind {
    PortToPort,
    PortToNode,
    NodeToPort,
    Programmatic,
    ProjectLoad
};

struct ConnectionEndpointRequest {
    QString moduleId;
    std::optional<QString> portId;
    QPointF scenePos;
    bool fromNodeBody = false;
    bool hiddenPortsAllowed = false;
};

struct ConnectionRequest {
    ConnectionRequestKind kind;
    ConnectionEndpointRequest start;
    ConnectionEndpointRequest end;
    bool interactive = true;
    bool allowAutoComplete = true;
    bool allowAlternatives = true;
};
```

The service resolves those requests into semantic port snapshots:

```cpp
struct PortSemanticInfo {
    PortRef ref;
    QString moduleType;
    QString pluginId;
    QString graphGroup;
    QString editorLayout;
    QString portName;
    QString direction;
    QString busType;
    QString portRole;
    QString interfaceId;
    QString interfaceBus;
    QString interfaceRole;
    QStringList compatibleRoles;
    QHash<QString, QStringList> matchFieldValues;
    bool supportsInput = false;
    bool supportsOutput = false;
    bool occupiedAsSource = false;
    bool occupiedAsTarget = false;
    bool visibleInUi = true;
};
```

The plugin boundary should prefer value snapshots like `PortSemanticInfo` over raw `Module*` or `Port*` pointers. Core internals may use pointers while resolving, but plugin-facing APIs should not depend on graph object lifetimes.

## Connection Result Model

The frontend needs options and reasons, not only true/false.

```cpp
enum class ConnectionCheckStatus {
    Allowed,
    NeedsSelection,
    Rejected
};

struct ConnectionResolvedOption {
    PortRef source;
    PortRef target;
    QString label;
    int priority = 0;
};

struct ConnectionCheckResult {
    ConnectionCheckStatus status;
    QVector<ConnectionResolvedOption> options;
    QString reasonCode;
    QString message;
};
```

Result semantics:

- `Allowed`: at least one option is legal. One option can be auto-connected. Multiple options require a chooser.
- `NeedsSelection`: the request can be completed, but there is no unique option.
- `Rejected`: no legal option exists. `reasonCode` and `message` explain why.

Common reason codes:

- `missing_module`
- `missing_port`
- `self_loop`
- `direction_mismatch`
- `bus_mismatch`
- `interface_role_mismatch`
- `interface_field_mismatch`
- `port_occupied`
- `duplicate_connection`
- `topology_rule_mismatch`
- `plugin_rule_rejected`
- `ambiguous_connection`

## Rule Evaluation Order

ConnectionRuleService evaluates rules in this order:

1. Resolve modules from request.
2. Resolve explicit ports or candidate ports.
3. Reject missing modules/ports and self-loops.
4. Normalize direction into canonical source/target orientation.
5. Apply direction compatibility.
6. Apply bus/interface/role/match-field compatibility.
7. Apply cardinality and occupied-port checks.
8. Apply lightweight plugin-declared rules.
9. Remove duplicate existing connections.
10. Sort allowed options by priority and UI distance.
11. Return `Allowed`, `NeedsSelection`, or `Rejected`.

Hard core failures such as missing module, missing port, and self-loop cannot be overridden by plugins. Plugin-declared rules can reject or rank options that passed core metadata checks.

## Declarative Plugin Rule Extensions

The module/spec metadata should grow connection-focused fields instead of requiring arbitrary C++ validation for common rules.

Example shape:

```xml
<interface id="east"
           label="East"
           bus="ravenoc_router_link"
           role="initiator"
           connects_to="target"
           match=""
           cardinality="one"
           autocomplete_group="router_side"
           topology_rule="opposite_side" />
```

Field meanings:

- `cardinality`: `one`, `many`, or future explicit numeric limits.
- `autocomplete_group`: groups ports for body-drop completion.
- `topology_rule`: simple core-interpreted relation such as `opposite_side`.
- `match`: fields whose values must overlap between interfaces.
- `acceptedValues` and parameter bindings continue to provide field values.

If a plugin needs a rule that cannot be expressed declaratively, that is a signal to first evaluate whether it belongs in editor-time feedback or only in DRC.

## Cleanup Scope

The v1 cleanup should remove or replace these current temporary paths:

- Legacy `ip_instance` compatibility emission in generated/DRC JSON.
- Legacy `ip_instances` migration in project reader.
- Legacy NoC framework `xps` / `endpoints` import in graph JSON.
- Domain-specific mesh/router checks inside `Graph::isValidConnection()`.
- Router/endpoint draft completion logic embedded in `NodeEditorWidget`.
- Port alias compatibility for old endpoint/router names.
- Any project writer behavior that emits pre-v1 compatibility fields.

Current examples and tests should be updated to v1 `.fpproj` and plugin graph JSON only.

## User Experience

During interactive connection:

- Hovering over a valid specific port highlights that port.
- Hovering over a node body highlights candidate ports if auto-completion is possible.
- Releasing on a single option creates the connection.
- Releasing on multiple options opens a small chooser.
- Releasing on no option cancels the draft and may surface a concise reason.

For non-interactive load/import:

- Invalid stored connections reject the project load with the connection id and reason.
- The loader must not silently drop invalid connections.

For generate/validate:

- The app writes the v1 project snapshot to the output directory.
- The app runs plugin DRC before or during generation as the final authority.

## Acceptance Criteria

- `Graph` no longer contains RaveNoC/NoC/router/endpoint-specific connection semantics.
- `NodeEditorWidget` no longer contains router/endpoint-specific connection completion logic.
- A single `ConnectionRuleService` is used by UI connection creation, command insertion, and project load validation.
- The service returns structured options and rejection reasons.
- Plugin connection rules are declared through metadata for v1 common cases.
- Ruby/plugin DRC remains the final correctness check for validation/generation.
- Legacy pre-v1 project and graph compatibility paths are removed or fail explicitly.
- Generated output directories contain a v1 `.fpproj` snapshot.
