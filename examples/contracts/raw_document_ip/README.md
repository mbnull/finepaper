# raw_document_ip

Demonstrates raw/text configuration documents in `ConfigBundle`.

```bash
ipcraft-cli validate-project examples/contracts/raw_document_ip/project.fpproj --packages examples/contracts/raw_document_ip/package
ipcraft-cli emit-inputs examples/contracts/raw_document_ip/project.fpproj --instance doc0 --out /tmp/ipcraft-doc --packages examples/contracts/raw_document_ip/package
```

Expected result: validation succeeds and emitted inputs contain `input/system.txt`.
