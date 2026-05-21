# simple_parameter_ip

Demonstrates a parameter-only IP package and static project validation.

```bash
ipcraft-cli inspect-project examples/contracts/simple_parameter_ip/project.fpproj
ipcraft-cli validate-project examples/contracts/simple_parameter_ip/project.fpproj --packages examples/contracts/simple_parameter_ip/package
ipcraft-cli emit-inputs examples/contracts/simple_parameter_ip/project.fpproj --instance simple0 --out /tmp/ipcraft-simple --packages examples/contracts/simple_parameter_ip/package
```

Expected result: JSON envelope `ipcraft.cli.result.v1` with `ok: true`; emitted inputs include `ipcraft.emitted-inputs.v1`.
