# artifact_collection_project

Demonstrates artifact declaration and collection.

```bash
ipcraft-cli collect-artifacts examples/contracts/artifact_collection_project/run --spec examples/contracts/artifact_collection_project/package/ipcraft.json
```

Expected result: files matching `reports/*.log` under the run/output root are returned in an `ArtifactIndex`.
