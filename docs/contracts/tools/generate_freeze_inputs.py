#!/usr/bin/env python3
"""Generate the cycle-free Revision 4 Gate 0 review input manifest."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
CONTRACTS = ROOT / "docs" / "contracts"
OUTPUT = CONTRACTS / "freeze-inputs.json"
MAIN_SPEC = ROOT / "docs" / "superpowers" / "specs" / "2026-07-12-default-noc-design-engine-workbench-design.md"
SELF_EXCLUSION = "docs/contracts/freeze-inputs.json is intentionally omitted to avoid recursive self-digest"


def active_adrs() -> list[Path]:
    result = []
    for path in sorted((ROOT / "docs" / "adr").glob("*.md")):
        if "superseded" not in path.read_text(encoding="utf-8")[:2048].lower():
            result.append(path)
    return result


def freeze_paths() -> list[Path]:
    paths = [ROOT / "CONTEXT.md", MAIN_SPEC]
    specs = ROOT / "docs" / "superpowers" / "specs"
    for letter in "abcdef":
        matches = sorted(specs.glob(f"appendix-{letter}-*.md"))
        if len(matches) != 1:
            raise SystemExit(f"Appendix {letter.upper()} must resolve exactly once")
        paths.extend(matches)
    paths.extend(active_adrs())
    for path in sorted(CONTRACTS.rglob("*")):
        if (not path.is_file() or "__pycache__" in path.parts or path.suffix == ".pyc"
                or path in {OUTPUT, CONTRACTS / "CORE-FREEZE.md", CONTRACTS / "GATE-STATUS.md"}):
            continue
        if path.is_symlink():
            raise SystemExit(f"freeze input must not be a symlink: {path.relative_to(ROOT)}")
        paths.append(path)
    relative = [path.relative_to(ROOT).as_posix() for path in paths]
    if len(relative) != len(set(relative)):
        raise SystemExit("duplicate freeze input path")
    return [ROOT / path for path in sorted(relative)]


def role(relative: str) -> str:
    if relative.startswith("docs/contracts/tools/"):
        return "authoring-tool"
    if relative.startswith("docs/contracts/fixtures/"):
        return "generated-fixture"
    return "normative-artifact"


def main() -> int:
    items = []
    for path in freeze_paths():
        relative = path.relative_to(ROOT).as_posix()
        items.append({
            "path": relative,
            "sha256": "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest(),
            "role": role(relative),
        })
    document = {
        "schema": "ipcraft.core-freeze-inputs.v1",
        "normativeRevision": 4,
        "selfExclusion": SELF_EXCLUSION,
        "files": items,
    }
    OUTPUT.write_text(json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"freeze inputs generated: {len(items)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
