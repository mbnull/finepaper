---
status: superseded by ADR-0024
---

# Preserve attachment intent when topology changes invalidate it

When a Mesh or Package configuration change removes a referenced Router or Access Slot, the System Design preserves the original attachment as unresolved rather than deleting or automatically relocating the NoC Interface. The preservation decision remains valid, but ADR-0024 supersedes the earlier claim that formal saving is allowed: unresolved state may be edited and recovered but blocks formal Save, Validate, and Generate.
