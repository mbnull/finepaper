# noc_cutover_project

Demonstrates the V1 hard-cutover pattern for a formerly graph/NoC-shaped project. Internal topology is `graph_config`; canvas coordinates are `layout`; opaque legacy data is `native`.

```bash
ipcraft-cli inspect-project examples/contracts/noc_cutover_project/project.fpproj
ipcraft-cli validate-project examples/contracts/noc_cutover_project/project.fpproj --packages examples/contracts/noc_cutover_project/package
```

Expected result: `ok: true`; no old `ipcraft.noc.project.v1` runtime export is required.
