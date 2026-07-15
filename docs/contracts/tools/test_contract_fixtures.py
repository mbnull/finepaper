#!/usr/bin/env python3
"""Executable regression tests for the populated Gate 0 fixture set."""

from __future__ import annotations

import copy
import json
import tempfile
import unittest
from pathlib import Path

import verify_contract_fixtures as subject
import generate_contract_fixtures


CONTRACTS = Path(__file__).resolve().parents[1]


class ContractFixtureTest(unittest.TestCase):
    def test_committed_fixture_catalog_is_complete_and_classifies_exactly(self) -> None:
        summary = subject.verify_all(CONTRACTS)
        self.assertEqual(summary.schema_roots, 18)
        self.assertGreaterEqual(summary.valid, 18)
        self.assertGreaterEqual(summary.invalid, 30)
        self.assertGreater(summary.schema_phase, 0)
        self.assertGreater(summary.core_semantic_phase, 0)

    def test_schema_validator_rejects_mutated_valid_fixture(self) -> None:
        catalog = subject.load_catalog(CONTRACTS)
        entry = next(item for item in catalog if item["expected"] == "accept")
        document = json.loads((CONTRACTS / entry["path"]).read_text())
        document.pop("schema", None)
        result = subject.classify_document(CONTRACTS, entry["schemaId"], document)
        self.assertEqual(result.phase, "schema")

    def test_wrong_catalog_expectation_boundary_and_code_are_rejected(self) -> None:
        catalog = subject.load_catalog(CONTRACTS)
        rejected = next(item for item in catalog if item["expected"] == "reject")
        for field, value in (
            ("expected", "accept"),
            ("failureBoundary", "generic-structure" if rejected["failureBoundary"] != "generic-structure" else "tool-input"),
            ("errorCode", "provider.timeout"),
        ):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "contracts"
                subject.copy_contract_tree(CONTRACTS, root)
                mutated = copy.deepcopy(catalog)
                target = next(item for item in mutated if item["path"] == rejected["path"])
                target[field] = value
                (root / "fixture-catalog.json").write_text(json.dumps({"schema": "ipcraft.fixture-catalog.v1", "items": mutated}))
                with self.assertRaises(subject.FixtureVerificationError):
                    subject.verify_all(root)

    def test_missing_and_extra_physical_fixture_are_rejected(self) -> None:
        catalog = subject.load_catalog(CONTRACTS)
        victim = catalog[0]["path"]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            (root / victim).unlink()
            with self.assertRaises(subject.FixtureVerificationError):
                subject.verify_all(root)

    def test_fixture_generation_is_byte_reproducible(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            generate_contract_fixtures.generate(root)
            expected = {
                path.relative_to(CONTRACTS).as_posix(): path.read_bytes()
                for path in [CONTRACTS / "fixture-catalog.json", *sorted((CONTRACTS / "fixtures").rglob("*.json"))]
            }
            actual = {
                path.relative_to(root).as_posix(): path.read_bytes()
                for path in [root / "fixture-catalog.json", *sorted((root / "fixtures").rglob("*.json"))]
            }
            self.assertEqual(actual, expected)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            extra = root / "fixtures/valid/uncatalogued.json"
            extra.write_text("{}")
            with self.assertRaises(subject.FixtureVerificationError):
                subject.verify_all(root)


if __name__ == "__main__":
    unittest.main()
