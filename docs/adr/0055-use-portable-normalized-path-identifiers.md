# Use portable normalized path identifiers

**Status:** Accepted

Finepaper V1 uses UTF-8 NFC, `/`-separated portable normalized relative identifiers for Bundle, execution-view, report, and artifact paths. The contract rejects controls, DEL, colon, backslash, absolute and drive-prefixed paths, empty or dot segments, Windows reserved device names, trailing dot/space segments, case-fold collisions, and containment escapes so immutable inputs have one reproducible identity across platforms and cannot acquire alternate-data-stream or reserved-name interpretations.

V1 pins NFC itself to the Unicode 17.0.0 canonical decomposition, canonical combining class, composition-exclusion, and Hangul algorithms captured in [`docs/contracts/unicode/nfc-normalization-17.0.0.json`](../contracts/unicode/nfc-normalization-17.0.0.json). The committed official [`NormalizationTest-17.0.0.txt`](../contracts/unicode/NormalizationTest-17.0.0.txt) is the conformance source. Validators must use that pinned data and must not delegate normative path identity to a host Unicode database.

After pinned NFC validation, collision comparison uses the Unicode 17.0.0 simple C/S case-fold mappings in [`docs/contracts/unicode/simple-case-folding-17.0.0.json`](../contracts/unicode/simple-case-folding-17.0.0.json). Validators apply that committed one-code-point mapping table directly and leave unlisted code points unchanged. They must not use full/Turkic mappings, multi-code-point expansions, host `casefold`/lowercase heuristics, or silently newer Unicode data. Both tables pin their official source SHA-256 values, full-table counts and cycle-free array digests; the authoring verifier regenerates and byte-compares them. Unicode data is redistributed under the committed [Unicode License V3](../contracts/unicode/UNICODE-LICENSE.txt). Moving to a later Unicode version is an explicit contract migration with new committed data/version and affected freeze verification; it is never an ambient runtime upgrade.

Package and Tool authors must rename host-native files that do not satisfy the portable contract before those files can enter locked Bundles, execution views, reports, or artifacts.
