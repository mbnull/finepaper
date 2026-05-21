# negative_flow_missing_executable

Demonstrates flow process diagnostics.

```bash
ipcraft-cli run-flow examples/contracts/negative_flow_missing_executable/project.fpproj --flow generate --instance missing0 --out /tmp/ipcraft-missing --packages examples/contracts/negative_flow_missing_executable/package
```

Expected result: `ok: false` with `flow.executable_missing`.
