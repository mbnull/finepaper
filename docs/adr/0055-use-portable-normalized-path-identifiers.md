# Use portable normalized path identifiers

**Status:** Accepted

Finepaper V1 uses UTF-8 NFC, `/`-separated portable normalized relative identifiers for Bundle, execution-view, report, and artifact paths. The contract rejects controls, DEL, colon, backslash, absolute and drive-prefixed paths, empty or dot segments, Windows reserved device names, trailing dot/space segments, case-fold collisions, and containment escapes so immutable inputs have one reproducible identity across platforms and cannot acquire alternate-data-stream or reserved-name interpretations.

V1 pins collision comparison to the Unicode 17.0.0 simple C/S case-fold mappings in [`docs/contracts/unicode/simple-case-folding-17.0.0.json`](../contracts/unicode/simple-case-folding-17.0.0.json). Validators first require NFC, then apply that committed one-code-point mapping table directly and leave unlisted code points unchanged. They must not use full/Turkic mappings, multi-code-point expansions, host `casefold`/lowercase heuristics, or a host Unicode database that can change independently. Moving to a later Unicode mapping version is an explicit contract migration with a new committed table/version and affected freeze verification; it is never an ambient runtime upgrade.

Package and Tool authors must rename host-native files that do not satisfy the portable contract before those files can enter locked Bundles, execution views, reports, or artifacts.
