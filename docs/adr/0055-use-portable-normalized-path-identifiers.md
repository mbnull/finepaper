# Use portable normalized path identifiers

**Status:** Accepted

Finepaper V1 uses UTF-8 NFC, `/`-separated portable normalized relative identifiers for Bundle, execution-view, report, and artifact paths. The contract rejects controls, DEL, colon, backslash, absolute and drive-prefixed paths, empty or dot segments, Windows reserved device names, trailing dot/space segments, case-fold collisions, and containment escapes so immutable inputs have one reproducible identity across platforms and cannot acquire alternate-data-stream or reserved-name interpretations.

Package and Tool authors must rename host-native files that do not satisfy the portable contract before those files can enter locked Bundles, execution views, reports, or artifacts.
