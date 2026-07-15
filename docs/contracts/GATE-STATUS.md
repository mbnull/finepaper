# Default NoC Delivery Gate Status

## Gate 0 — Core Contract Freeze

- Status: **Complete / Frozen**
- Normative revision: **Revision 4**
- Frozen input commit: `70f69c2a493205269966b25564e4d9e101bd71fd`
- Scope: schemas, canonical models and digests, fixtures and error policy, exact Default Engine lock, versioned Host side effects, recovery/history contracts, and review evidence only
- Product implementation: **not included**

The latest frozen input closure sequence is:

- `70f69c2` — harden Gate 0 review evidence
- `f2a5a31` — finalize Gate 0 freeze inputs
- `3e0cbfd` — enforce Gate 0 Core contracts

### Clean build evidence

The build was configured with compiler caching disabled and a dedicated output tree outside the repository:

```text
env CCACHE_DISABLE=1 xmake config -P qt -c -o /tmp/finepaper-gate0-freeze-build --ccache=n
env CCACHE_DISABLE=1 xmake build -P qt qt
env CCACHE_DISABLE=1 xmake build -P qt <each Gate target and validation_test>
```

Result: full Qt target, all seven non-default Gate targets, and `validation_test` built successfully from the dedicated clean output tree.

### Seven required Gate 0 executable checks

Each command exited `0` and printed its target name followed by `passed`:

```text
env CCACHE_DISABLE=1 xmake run -P qt noc_contract_schema_meta_test
env CCACHE_DISABLE=1 xmake run -P qt noc_canonical_digest_vectors_test
env CCACHE_DISABLE=1 xmake run -P qt noc_contract_fixture_catalog_test
env CCACHE_DISABLE=1 IPCRAFT_CONTRACT_PYTHON=/usr/bin/python3 xmake run -P qt noc_review_bundle_completeness_test
env CCACHE_DISABLE=1 xmake run -P qt noc_core_canonical_models_schema_test
env CCACHE_DISABLE=1 xmake run -P qt noc_default_engine_lock_contract_test
env CCACHE_DISABLE=1 xmake run -P qt noc_host_side_effect_contract_test
```

The existing validation regression also passed when run from its required repository-aware working directory:

```text
env CCACHE_DISABLE=1 xmake run -P qt -w /home/bnl/dev/finepaper/.worktrees/noc-gate0-core-freeze/qt validation_test
```

The explicit working directory is necessary because this legacy test locates `ipcores/` by walking upward from its current directory. Xmake otherwise starts the binary from the external `/tmp` target directory. This is test-environment evidence, not a Core or product-code change.

### Python authoring and regeneration checks

The following checks exited `0`; remaining archive-phase Python commands use `-B` or `PYTHONDONTWRITEBYTECODE=1`:

```text
python3 docs/contracts/tools/verify_canonical_rules.py
python3 docs/contracts/tools/verify_canonical_vectors.py
python3 docs/contracts/tools/test_contract_fixtures.py
python3 docs/contracts/tools/verify_contract_fixtures.py
python3 docs/contracts/tools/test_fixture_catalog_hardening.py
python3 docs/contracts/tools/verify_fixture_catalog.py
python3 docs/contracts/tools/verify_engine_side_effect_contracts.py
python3 docs/contracts/tools/verify_engine_side_effect_vectors.py
python3 docs/contracts/tools/verify_unicode_regeneration.py --case-folding /usr/share/texlive/texmf-dist/tex/generic/unicode-data/CaseFolding.txt --unicode-data /usr/share/texlive/texmf-dist/tex/generic/unicode-data/UnicodeData.txt --composition-exclusions /tmp/CompositionExclusions-17.0.0.txt --normalization-test docs/contracts/unicode/NormalizationTest-17.0.0.txt
```

Generator byte comparisons succeeded for canonical vectors, Default Engine/Host side-effect vectors, all contract fixtures and coverage data, the stable error catalog, Unicode 17 artifacts, and the 468-file freeze-input manifest. Strict JSON parsing of every `docs/contracts/**/*.json` file and `git diff --check` also passed.

## Gate A — Headless Core Implementation

- Status: **Not started**
- Authorization condition: Gate A may start only from this frozen Revision 4 Core contract and must not silently change any frozen input.
- Current record contains no Gate A product implementation.

Gate D remains the separate Extension ABI freeze; this Gate 0 record does not freeze the Provider wire ABI.
