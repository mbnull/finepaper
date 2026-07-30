# Endpoint attachment feature

This directory owns the Package-driven rules shared by the GUI shell and the
topology canvas for attaching Endpoints to derived Mesh Routers.

`endpoint_attachment_rules` normalizes automatic, explicit, and legacy
explicit Package policies; infers a projection-only policy when the exact
Package is unavailable; resolves complete-design occupancy and visual port
bindings; validates connection handles and attachment targets; and plans the
pending/attached/detached Endpoint lifecycle. It is a QtCore-only boundary and
does not mutate `NocDesign`.

The invariants are intentionally closed:

- only Endpoint EP output to Router EP input is user-connectable;
- Router direction ports and Router links remain derived from the rectangular
  Mesh and cannot produce mutation commands;
- automatic display slots are never persisted;
- inferred missing-Package policies are always read-only;
- Package attachment capacity is bounded by the public
  `kMaximumEndpointAttachmentsPerRouter` resource limit before any visual-port
  expansion;
- Endpoint and Router canvas positions never enter these rules.

`FinepaperApplication` remains the final validation and mutation authority.
The topology editor owns scene hit-testing and Workspace geometry, while the
GUI shell owns dialogs, status feedback, and command submission.
