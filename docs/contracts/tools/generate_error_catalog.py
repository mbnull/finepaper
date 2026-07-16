#!/usr/bin/env python3
"""Mechanically render the frozen V1 error catalog entry envelope."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_PATH = ROOT / "docs" / "contracts" / "error-codes-v1.json"
NON_BLOCKING = {
    "attachment.target_removed",
    "domain.non_default_deleted",
    "engine_migration.dependency_replaced",
    "package_relation.endpoint_unresolved",
}
DIRECT_OWNER_PREFIXES = {
    "command", "contract", "dependency", "diagnostic", "engine", "host",
    "output", "package", "patch", "pipeline", "project", "provider", "recovery", "tool",
}
HOST_SIDE_EFFECT_PREFIXES = {"attachment", "domain", "engine_migration", "package_relation"}


def expected_owner(code: str) -> str:
    prefix = code.split(".", 1)[0]
    if prefix in HOST_SIDE_EFFECT_PREFIXES:
        return "host-side-effects"
    if prefix in DIRECT_OWNER_PREFIXES:
        return prefix
    raise SystemExit(f"error code has no closed owner rule: {code}")


def message_template(code: str) -> str:
    words = code.replace(".", " ").replace("_", " ")
    return words[:1].upper() + words[1:] + "."


def render(source: Path) -> bytes:
    document = json.loads(source.read_text(encoding="utf-8"))
    codes = []
    for raw in document["codes"]:
        code = raw["code"]
        codes.append({
            "code": code,
            "owner": expected_owner(code),
            "blocking": code not in NON_BLOCKING,
            "messageTemplate": message_template(code),
        })
    codes.sort(key=lambda item: item["code"])
    if len({item["code"] for item in codes}) != len(codes):
        raise SystemExit("duplicate error code")
    result = {"schema": "ipcraft.error-catalog.v1", "version": "1", "codes": codes}
    return (json.dumps(result, indent=2, ensure_ascii=False) + "\n").encode()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_PATH)
    args = parser.parse_args()
    payload = render(DEFAULT_PATH)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f"error catalog generated: {len(json.loads(payload)['codes'])} codes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
