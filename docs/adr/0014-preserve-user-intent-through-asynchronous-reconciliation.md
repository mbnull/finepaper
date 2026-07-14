# Preserve user intent through asynchronous Engine reconciliation

**Status: Superseded by ADR-0047.**

User configuration changes enter the working DesignSession immediately and remain undoable without waiting for reconciliation. Engine-managed topology is reconciled asynchronously against a specific revision by the Default Engine or an Extension Provider; stale responses are ignored, and failures preserve both user intent and the last successful structure. Users may continue editing Attachments and Domain memberships against that last structure; surviving identities remain valid and removed targets become unresolved after reconciliation. Formal project saving, validation, and generation remain blocked until reconciliation is current; incomplete work is preserved only through a separate recovery record.
