# Failure Report Format

Audit failures should be reported as structured summaries, not hidden test source.

```json
{
  "contract_section": "string",
  "expected_behavior": "string",
  "actual_behavior": "string",
  "minimal_redacted_input": {},
  "stable_diagnostics_observed": [
    {
      "severity": "error",
      "source": "core",
      "rule_id": "example.rule",
      "locations": []
    }
  ]
}
```

Rules:

- Include the contract section that owns the expected behavior.
- Include only minimal redacted input needed to reproduce the ambiguity or failure.
- Do not include hidden test names, private fixture names, fuzz corpus paths, or full test source.
- If expected behavior is not clearly specified, request a public contract update first.
