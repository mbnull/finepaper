# failing_validator_project

Demonstrates that external validation failures are flow-driven, not part of default static validation.

```bash
ipcraft-cli validate-project examples/contracts/failing_validator_project/project.fpproj --packages examples/contracts/failing_validator_project/package
ipcraft-cli run-flow examples/contracts/failing_validator_project/project.fpproj --flow validate --instance fail0 --out /tmp/ipcraft-fail --packages examples/contracts/failing_validator_project/package
```

Expected result: `validate-project` returns `ok: true`; `run-flow` returns `ok: false` with `flow.exec_failed`.
