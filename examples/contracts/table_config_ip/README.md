# table_config_ip

Demonstrates table configuration in `ConfigBundle`.

```bash
ipcraft-cli validate-project examples/contracts/table_config_ip/project.fpproj --packages examples/contracts/table_config_ip/package
ipcraft-cli emit-inputs examples/contracts/table_config_ip/project.fpproj --instance table0 --out /tmp/ipcraft-table --packages examples/contracts/table_config_ip/package
```

Expected result: validation succeeds and emitted inputs contain `input/regions.json`.
