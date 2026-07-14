# Bind reconciliation to topology input and structure base digests

**Status: Superseded by ADR-0048.**

Reconciliation state is the product of `structureFreshness` (`current|stale`) and `jobState` (`idle|running|failed`), not one enum. Requests bind topology input revision/digest and base structure revision/digest plus Package and Engine identities. Digests use RFC 8785 canonical JSON and SHA-256 lowercase hex. A result commits only if both current input and structure base still match; coalesced or cancelled stale work is otherwise discarded and reopening recomputes fingerprints before declaring structure current.
