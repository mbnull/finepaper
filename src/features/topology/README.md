# Topology feature

This directory owns the desktop interaction and presentation of the NoC
topology: the canvas, Router/Endpoint attachment gestures, layout state, and
Mesh resize UI.

`TopologyWorkspaceStore` is the persistence boundary for visual Router and
Endpoint position overrides plus Router collapse state. It writes one
versioned record per design workspace, strictly validates the record identity
and shape, and only reads unambiguous legacy layout keys. Workspace placement
must never be copied into `NocDesign` or treated as a logical attachment.
Malformed current records fail closed so normal drag/collapse activity cannot
silently overwrite them. The shell presents a persistent, non-modal warning;
the user's explicit **Regularize Layout** action replaces the damaged visual
record. Malformed legacy roots are left untouched and skipped so they cannot
disable the independent current namespace.

Document sessions and persistent workspace identities are separate concepts.
Opening a new document session reloads its workspace even when the Package and
design identifiers match the previous session, and deferred graph operations
are discarded after any graph projection revision.

It does not own design mutation rules, Package schemas, Domain realization, or
RTL generation. Those remain in `application/`, `package/`, and the selected
Package runtime. The feature may be composed by `gui/`, but must not include
headers from `gui/`; shared widgets and workbench contracts belong under
`ui/`.

Large classes in this directory are migration points, not a permanent
architecture. Subsequent behavior changes should extract scene projection,
attachment gestures, and hit-testing behind focused interfaces.
