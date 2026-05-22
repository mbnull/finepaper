# negative_malformed_package

Demonstrates package parser diagnostics for malformed `ipcraft.package.v1`.

```bash
ipcraft-cli validate-project examples/contracts/negative_malformed_package/project.fpproj --packages examples/contracts/negative_malformed_package/package
```

Expected result: `ok: false` with diagnostic `rule_id: "package.missing_required"` because the package omits `version`.
Malformed package parser variants may also report stable package parser diagnostics such as `package.unsupported_schema` or `package.invalid_json`, depending on the first failing parse stage.
