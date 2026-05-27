# negative_path_escape

Demonstrates package-local path confinement.

```bash
ipcraft-cli validate-project examples/contracts/negative_path_escape/project.fpproj --packages examples/contracts/negative_path_escape/package
```

Expected result: `ok: false` with `package.path_escape`.
