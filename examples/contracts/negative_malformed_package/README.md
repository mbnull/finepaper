# negative_malformed_package

Demonstrates package parser diagnostics for malformed `ipcraft.package.v1`.

```bash
ipcraft-cli validate-project examples/contracts/negative_malformed_package/project.fpproj --packages examples/contracts/negative_malformed_package/package
```

Expected result: `ok: false` with `package.missing_required`.
