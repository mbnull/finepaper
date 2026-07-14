# Separate Session, topology-input, and structure revisions

**Status: Superseded by ADR-0048.**

Any user command increments `sessionRevision`, but only topology-driving configuration increments `topologyInputRevision` and changes the normalized `topologyInputDigest`. Reconciliation results are applicable when their input digest still matches, regardless of unrelated Interface or presentation edits; accepted Engine-Managed Structure Patches increment `structureRevision`, must not overwrite user-owned relations, and turn references to removed targets into unresolved intent.
