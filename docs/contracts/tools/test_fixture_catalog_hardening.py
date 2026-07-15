#!/usr/bin/env python3
"""Regression tests for Gate 0 fixture-catalog hardening."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path

import verify_fixture_catalog as subject


CONTRACTS = Path(__file__).resolve().parents[1]


class FixtureCatalogHardeningTest(unittest.TestCase):
    def test_case_fold_table_rejects_arbitrary_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "unicode").mkdir()
            document = json.loads((CONTRACTS / subject.UNICODE_CASE_FOLD_PATH).read_text())
            document["mappings"][700]["target"] = "0061"
            document["mappingsSha256"] = subject.canonical_digest(document["mappings"])
            (root / subject.UNICODE_CASE_FOLD_PATH).write_text(json.dumps(document))
            with self.assertRaises(subject.VerificationError):
                subject.load_simple_case_folding_table(root)

    def test_case_fold_table_rejects_deletion_and_insertion(self) -> None:
        source = json.loads((CONTRACTS / subject.UNICODE_CASE_FOLD_PATH).read_text())
        for mutate in (lambda values: values.pop(900), lambda values: values.insert(900, {"source": "10FFFF", "target": "0061"})):
            with self.subTest(mutate=mutate):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    (root / "unicode").mkdir()
                    document = json.loads(json.dumps(source))
                    mutate(document["mappings"])
                    (root / subject.UNICODE_CASE_FOLD_PATH).write_text(json.dumps(document))
                    with self.assertRaises(subject.VerificationError):
                        subject.load_simple_case_folding_table(root)

    def test_vector_evidence_rejects_unlisted_fabricated_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "vectors").mkdir()
            (root / "vectors/fake.json").write_text(json.dumps({"cases": [{"id": "fabricated"}]}))
            with self.assertRaises(subject.VerificationError):
                subject.verify_evidence("vectors/fake.json#fabricated", root, "evidence")

    def test_vector_evidence_accepts_frozen_recognized_case(self) -> None:
        subject.verify_evidence(
            "vectors/default-engine-lock-v1.json#engine-lock-exact-available", CONTRACTS, "evidence"
        )

    def test_vector_evidence_rejects_wrong_envelope_and_case_array(self) -> None:
        filename = "host-side-effects-v1.json"
        source = json.loads((CONTRACTS / "vectors" / filename).read_text())
        case_id = source["cases"][0]["caseId"]
        for mutate in (
            lambda document: document.__setitem__("schema", "fabricated.vector.v1"),
            lambda document: document.__setitem__("cases", {"caseId": case_id}),
        ):
            with self.subTest(mutate=mutate):
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    (root / "vectors").mkdir()
                    document = json.loads(json.dumps(source))
                    mutate(document)
                    (root / "vectors" / filename).write_text(json.dumps(document))
                    with self.assertRaises(subject.VerificationError):
                        subject.verify_evidence(
                            f"vectors/{filename}#{case_id}", root, "evidence"
                        )

    def test_wrong_known_error_code_is_rejected(self) -> None:
        self.assertIn("provider.timeout", subject.error_codes(CONTRACTS))
        allowed = subject.load_error_policy(CONTRACTS)
        with self.assertRaises(subject.VerificationError):
            subject.verify_error_policy(
                "ipcraft.project-design.v1", "schema", "provider.timeout", allowed, "fixture"
            )

    def test_schema_catalog_rejects_parent_traversal(self) -> None:
        nfc = subject.load_unicode_normalization_data(CONTRACTS)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "schemas").mkdir()
            valid = root / "schemas/valid.schema.json"
            valid.write_text(json.dumps({"$id": "valid"}))
            (root / "schema-catalog.json").write_text(json.dumps({
                "schema": "ipcraft.contract-schema-catalog.v1",
                "items": [{"id": "valid", "path": "schemas/valid.schema.json", "freezeGate": "Gate 0"}],
            }))
            self.assertEqual(subject.catalog_ids(root, nfc), {"valid"})
            outside = root / "outside.schema.json"
            outside.write_text(json.dumps({"$id": "escape"}))
            catalog = {
                "schema": "ipcraft.contract-schema-catalog.v1",
                "items": [{"id": "escape", "path": "schemas/../outside.schema.json", "freezeGate": "Gate 0"}],
            }
            (root / "schema-catalog.json").write_text(json.dumps(catalog))
            with self.assertRaisesRegex(subject.VerificationError, "dot path segment"):
                subject.catalog_ids(root, nfc)

    def test_schema_catalog_rejects_absolute_and_symlink_paths(self) -> None:
        nfc = subject.load_unicode_normalization_data(CONTRACTS)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "schemas").mkdir()
            target = root / "outside.schema.json"
            target.write_text(json.dumps({"$id": "escape"}))
            for catalog_path in (str(target), "schemas/escape.schema.json"):
                with self.subTest(path=catalog_path):
                    link = root / "schemas/escape.schema.json"
                    if link.exists() or link.is_symlink():
                        link.unlink()
                    if catalog_path.startswith("schemas/"):
                        os.symlink(target, link)
                    catalog = {
                        "schema": "ipcraft.contract-schema-catalog.v1",
                        "items": [{"id": "escape", "path": catalog_path, "freezeGate": "Gate 0"}],
                    }
                    (root / "schema-catalog.json").write_text(json.dumps(catalog))
                    with self.assertRaises(subject.VerificationError):
                        subject.catalog_ids(root, nfc)

    def test_unicode17_nfc_reorders_new_combining_mark(self) -> None:
        tables = subject.load_unicode_normalization_data(CONTRACTS)
        original = "a\u1acf\u0323"
        self.assertEqual(subject.nfc_normalize(original, tables), "\u1ea1\u1acf")

    def test_unicode17_nfc_composes_hangul(self) -> None:
        tables = subject.load_unicode_normalization_data(CONTRACTS)
        self.assertEqual(subject.nfc_normalize("\u1100\u1161\u11a8", tables), "\uac01")

    def test_unicode17_nfc_data_rejects_rehashed_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "unicode").mkdir()
            document = json.loads((CONTRACTS / subject.UNICODE_NFC_PATH).read_text())
            document["combiningClasses"][500]["class"] = 1
            document["combiningClassesSha256"] = subject.canonical_digest(document["combiningClasses"])
            (root / subject.UNICODE_NFC_PATH).write_text(json.dumps(document))
            (root / "unicode/UNICODE-LICENSE.txt").write_bytes(
                (CONTRACTS / "unicode/UNICODE-LICENSE.txt").read_bytes()
            )
            with self.assertRaises(subject.VerificationError):
                subject.load_unicode_normalization_data(root)


if __name__ == "__main__":
    unittest.main()
