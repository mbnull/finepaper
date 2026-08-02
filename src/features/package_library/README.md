# Package Library feature boundary

This folder owns the NoC Library presentation and runtime-availability view.

- `PackageLibraryPanel` receives value-only state. It never borrows a
  `PackageDefinition`, `NocDesign`, or catalog-owned schema.
- `package_library_projection` is the only adapter from Package metadata to
  the compact text shown by the panel. A future topology-provider model can
  replace the current Mesh summary here without rewriting the widget.
- `RuntimePackageCache` stores only Package keys for one catalog revision.
  Routine canvas/design projection does not read Package files. Reload,
  Install, Create, and run preflight are the explicit refresh boundaries.

Application services remain responsible for catalog transactions and design
mutation. The panel emits typed user intent and does not serialize protocol
objects or infer editing capability from the new-design Package selection.
