# Default NoC Core Contract Freeze

## Freeze identity

- Status: **Unfrozen / Revision 5 in progress**
- Normative revision: **Revision 5 (not yet frozen)**
- Freeze date: **2026-07-16**
- Frozen input Git commit: `70f69c2a493205269966b25564e4d9e101bd71fd`
- Frozen input commit timestamp / archive epoch: `1784155668` (`2026-07-16T06:47:48+08:00`)
- Core Engine Host contract: `ipcraft.engine-host.v1`
- Host side-effect contract: `ipcraft.noc-side-effects.v1`
- Review archive member count: `470` (`468` frozen inputs plus this record and `GATE-STATUS.md`)
- `reviewArchiveContentDigest`: `sha256:8cb140ff3a42636e57f8a16765f17ffbdea4f1866ed6a6adcbe53d2d2e09080a`

Revision 4 was unfrozen by `docs/contracts/UNFREEZE-REV4-ADR.md` after review
found that its declared RFC 8785 digest contract was not implemented
consistently by the Python and Qt canonicalizers. The Revision 4 archive remains
a reproducibility artifact, not an approved public wire contract.

The frozen input commit is intentionally the parent of the record/archive commit. The record and archive cannot include or hash the commit that contains themselves without creating a recursive identity. The final record commit is release evidence outside this self-contained archive; it is not a Core contract identity. The durable raw archive identity is stored separately at repository-root `docs.tar.sha256`, outside `docs.tar` to avoid recursion.

The review archive content digest is non-self-referential. It is SHA-256 over UTF-8 lines sorted by portable repository-relative path, each line formatted as `<raw-file-sha256><two-spaces><path>\n`. All members use their raw bytes except this file: before hashing this member, the value on the `reviewArchiveContentDigest` line is replaced by exactly `sha256:` followed by 64 ASCII zeroes. The final digest is then written into that line. This rule makes the content identity independently reproducible without pretending that an archive can contain its own raw digest.

`CORE-FREEZE.md` and `GATE-STATUS.md` are deliberate archive additions outside `freeze-inputs.json`. They record the approval outcome; they do not alter the frozen Core inputs. The pre-record candidate wording in `docs/contracts/README.md` describes the state before this record was produced; this file is the authoritative Gate 0 status record.

## Exact deterministic archive construction

The following GNU tar procedure is normative for rebuilding `docs.tar`. It derives the exact member set from the 468 `freeze-inputs.json` paths plus the two freeze records, sorts portable repository-relative names under the C locale, writes no directory entries, uses POSIX ustar rather than PAX, fixes the input-commit epoch, clears owner names through numeric ownership, and normalizes every regular-file mode to `0644`.

```text
repo="$(git rev-parse --show-toplevel)"
work="${GATE0_ARCHIVE_WORKDIR:?set GATE0_ARCHIVE_WORKDIR to an empty writable directory}"
epoch=1784155668

mkdir -p "$work"
jq -r '.files[].path, "docs/contracts/CORE-FREEZE.md", "docs/contracts/GATE-STATUS.md"' \
  "$repo/docs/contracts/freeze-inputs.json" | LC_ALL=C sort > "$work/members.txt"
test "$(wc -l < "$work/members.txt")" -eq 470

TZ=UTC LC_ALL=C tar --create --file="$work/docs-a.tar" \
  --format=ustar --sort=name --mtime="@$epoch" \
  --owner=0 --group=0 --numeric-owner --mode=0644 --no-recursion \
  -C "$repo" -T "$work/members.txt"
TZ=UTC LC_ALL=C tar --create --file="$work/docs-b.tar" \
  --format=ustar --sort=name --mtime="@$epoch" \
  --owner=0 --group=0 --numeric-owner --mode=0644 --no-recursion \
  -C "$repo" -T "$work/members.txt"

cmp "$work/docs-a.tar" "$work/docs-b.tar"
cp "$work/docs-a.tar" "$repo/docs.tar"
cd "$repo"
sha256sum docs.tar > docs.tar.sha256
sha256sum --check docs.tar.sha256
```

The member list must not contain `.git`, build output, Python bytecode/cache files, `docs.tar`, `docs.tar.sha256`, or `freeze-inputs.json`. Ustar is required specifically to avoid variable PAX headers. `TZ=UTC`, `LC_ALL=C`, the fixed epoch, numeric zero ownership, empty stored owner/group names, `0644` modes, and sorted names make the output independent of the invoking account and local timezone.

## Frozen artifact digests

All values below are raw-file SHA-256 digests.

| Artifact | SHA-256 |
| --- | --- |
| `docs/contracts/schema-catalog.json` | `07ba313df120030eaaaf8e900528fc053e6dcb83e6d12df112159ccb3de52252` |
| `docs/contracts/fixture-catalog.json` | `62c5aee95e2e7025231dcbf504ec2767f835e92ea422ec98f64f8fed93504a6b` |
| `docs/contracts/fixture-error-policy-v1.json` | `f2c541580f0fce6d3c8f22c647d95bf590378406daf8def1f5add501532f6352` |
| `docs/contracts/fixture-coverage-v1.json` | `9e39c372fc176e48cba7dfedcba31fc137840569835be7318ae034a9ca5bcbae` |
| `docs/contracts/error-codes-v1.json` | `cfd1cde2a02c86ab7be6d5ace9e31f6d02ea85be5bea714f7b18c8f170b0a504` |
| `docs/contracts/vectors/core-canonical-projection-v1.json` | `bf570f7a18cc99f58fb3d50a94ccc2598c9285a85976da0235ec3c82c95e4e44` |
| `docs/contracts/schemas/ipcraft.engine-bundle.v1.schema.json` | `73066c81893e9b4be407b980093d761096a89f0e7878eb46b13e8b982ae2fc60` |
| `docs/contracts/fixtures/valid/engine-bundle.json` | `876a861a85d5021e4e995cf1cacfe0990bbe6b104d9f46c5eb5ca81ab4c8716e` |
| `docs/contracts/vectors/default-engine-lock-v1.json` | `24fa76edfdbf48c26be68ae0b31247879f846c87d82479aa3bc01a4c6a5fffb9` |
| `docs/contracts/schemas/ipcraft.noc-side-effects.v1.schema.json` | `d81f99f544a2695ac71960c9a95f3af7383cab87582f9dcb64243fd327d016ae` |
| `docs/contracts/vectors/host-side-effects-v1.json` | `6ca6cbbe2daff2589f86c13ed877e7accee39a1aa95390996ce7d7877fbb7c79` |
| `docs/contracts/patch-validation-context-v1.json` | `d591c93b0773129c51398c88d6be7846745fcb78b1ae798f81f2eaf1ae54ba76` |
| `docs/contracts/freeze-inputs.json` | `271da03cc16eb6c94c80a33e033170f4871910de97004c1a7a70c945aaaac319` |
| `docs/contracts/unicode/simple-case-folding-17.0.0.json` | `2699a1a96e6710dca5a5b49025b614b2f669f31260628390c83f69258faf9ca2` |
| `docs/contracts/unicode/nfc-normalization-17.0.0.json` | `e7da73453c09c52eef83e05d8fba54a50e2c589fbbdfdefb76d683785c99704c` |
| `docs/contracts/unicode/NormalizationTest-17.0.0.txt` | `5019ffd530751a741900c849c0e010332f142a3612234639bd200b82138a87db` |
| `docs/contracts/unicode/UNICODE-LICENSE.txt` | `e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96` |

## Frozen coverage

- Schemas: `19`
- Canonical array rules: `99`
- Fixture catalog entries: `360` (`98` valid, `262` invalid)
- Stable error codes: `75`
- Default Engine behavior: `18` resolution cases, `6` migration cases, `8` freshness cases
- Host side effects: `14` causal cases and `169` rejected mutations
- Frozen input files: `468`

The Default Engine is an independently installable immutable Bundle. Its `bundleManifestDigest` is the only exact implementation identity. `id`, `version`, and `engineCompatibilityVersion` are metadata or migration classification only. A missing, revoked, corrupt, digest-mismatched, Host-incompatible, platform-incompatible, or side-effect-contract-incompatible exact Bundle puts the design into degraded inspect mode. The Host must never fall back to a currently installed or built-in Engine with another digest.

Engine migration uses the topology candidate/confirmation transaction. It atomically updates the dependency lock, Derived State, versioned Host side effects, and provenance. Undo replays the saved inverse transaction and does not execute an Engine.

## Change control

Any change to a frozen Core schema, canonical projection, stable error, fixture policy, Default Engine exact-lock semantics, Engine Host contract, Host side-effect semantics, digest rule, or other file in `freeze-inputs.json` requires all of the following:

1. an explicit unfreeze ADR;
2. a new normative revision when the public meaning or wire contract changes;
3. regeneration of catalogs, vectors, fixtures, Unicode evidence where applicable, `freeze-inputs.json`, and the deterministic review archive;
4. replay of the complete Gate 0 verification suite;
5. replay of every downstream gate whose evidence depends on the changed Core contract.

Gate A product implementation is not part of this Revision 5 correction record.
Gate A remains blocked until the Revision 5 canonicalization, strict JSON
admission, validation-mode, schema-resolution, and regenerated-evidence checks
are complete.
