# negative_extension_required

Demonstrates explicit extension enablement enforcement.

```bash
ipcraft-cli validate-project examples/contracts/negative_extension_required/project.fpproj --packages examples/contracts/negative_extension_required/package
```

Expected result: `ok: false` with `package.extension_required` because `config_schema.tables` lacks `ipcraft.config.tables`.
