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
    REQUIRED_VALID = {
        "fixtures/valid/minimal-1x1.json",
        "fixtures/valid/mesh-2x2-attached.json",
        "fixtures/valid/mesh-2x2-package-extension.json",
    }
    REQUIRED_INVALID = {
        "fixtures/invalid/project-wrong-schema-id.json",
        "fixtures/invalid/project-legacy-root.json",
        "fixtures/invalid/project-missing-noc-profile.json",
        "fixtures/invalid/project-zero-components.json",
        "fixtures/invalid/project-two-components.json",
        "fixtures/invalid/project-nonempty-connections.json",
        "fixtures/invalid/project-duplicate-id.json",
        "fixtures/invalid/project-slot-missing-router.json",
        "fixtures/invalid/project-attachment-missing-slot.json",
        "fixtures/invalid/project-attachment-occupied-slot.json",
        "fixtures/invalid/project-attachment-contract-mismatch.json",
        "fixtures/invalid/project-attachment-role-mismatch.json",
        "fixtures/invalid/project-attachment-capability-mismatch.json",
        "fixtures/invalid/project-missing-domain-membership.json",
        "fixtures/invalid/project-disconnected-domain.json",
        "fixtures/invalid/project-core-field-wrong-type.json",
        "fixtures/invalid/project-opaque-outside-extension.json",
        "fixtures/invalid/project-runtime-lock-incomplete.json",
    }

    def test_committed_fixture_catalog_is_complete_and_classifies_exactly(self) -> None:
        summary = subject.verify_all(CONTRACTS)
        self.assertEqual(summary.schema_roots, 18)
        self.assertGreaterEqual(summary.valid, 18)
        self.assertGreaterEqual(summary.invalid, 30)
        self.assertGreater(summary.schema_phase, 0)
        self.assertGreater(summary.core_semantic_phase, 0)

    def test_appendix_a_required_fixture_files_are_explicitly_present(self) -> None:
        paths = {item["path"] for item in subject.load_catalog(CONTRACTS)}
        self.assertTrue(self.REQUIRED_VALID <= paths)
        self.assertTrue(self.REQUIRED_INVALID <= paths)

    def test_attached_2x2_slot_contract_is_legally_satisfied(self) -> None:
        document = json.loads((CONTRACTS / "fixtures/valid/mesh-2x2-attached.json").read_text())
        self.assertEqual(subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", document).phase, None)
        interface = document["interfaces"][0]
        attachment = document["topologies"][0]["attachments"][0]
        slot = next(item for item in document["topologies"][0]["accessSlots"] if item["id"] == attachment["slotId"])
        allowance = next(item for item in slot["allowedContracts"] if item["contractLockId"] == interface["contract"]["lockId"])
        self.assertIn(interface["contract"]["role"], allowance["roles"])
        self.assertEqual(allowance["capabilityConstraints"], {"dataWidth": 128, "coherent": False})
        self.assertEqual(interface["capabilities"], {"dataWidth": 128, "coherent": False})

    def test_package_extension_fixture_contains_resolved_declared_objects(self) -> None:
        document = json.loads((CONTRACTS / "fixtures/valid/mesh-2x2-package-extension.json").read_text())
        topology = document["topologies"][0]
        self.assertEqual(len(topology["packageEntities"]), 1)
        self.assertEqual(len(topology["packageRelations"]), 1)
        self.assertEqual(len(document["extensions"]), 1)
        relation = topology["packageRelations"][0]
        package = json.loads((CONTRACTS / "fixtures/valid/noc-package.json").read_text())
        self.assertIn(topology["packageEntities"][0]["typeKey"], {item["typeKey"] for item in package["packageEntityTypes"]})
        self.assertIn(relation["typeKey"], {item["typeKey"] for item in package["packageRelationTypes"]})
        self.assertEqual(relation["sources"][0]["state"], "resolved")
        self.assertEqual(relation["targets"][0]["subject"]["id"], topology["packageEntities"][0]["id"])
        self.assertEqual(subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", document).phase, None)

    def test_protocol_contract_fixtures_are_representative(self) -> None:
        for protocol in ("axi5", "ace", "chi"):
            with self.subTest(protocol=protocol):
                document = json.loads((CONTRACTS / f"fixtures/valid/interface-contract-{protocol}.json").read_text())
                self.assertGreaterEqual(len(document["roles"]), 2)
                self.assertEqual({item["key"] for item in document["capabilities"]}, {"coherent", "dataWidth"})
                self.assertEqual({item["key"] for item in document["fields"]}, {"addressWidth", "idWidth"})
                self.assertEqual(subject.classify_document(CONTRACTS, "ipcraft.interface-contract.v1", document).phase, None)

    def test_project_adversarial_invariants_fail_closed(self) -> None:
        source = json.loads((CONTRACTS / "fixtures/valid/mesh-2x2-attached.json").read_text())
        mutations = {
            "duplicate-coordinate": lambda d: d["topologies"][0]["routers"][1].__setitem__("coordinate", copy.deepcopy(d["topologies"][0]["routers"][0]["coordinate"])),
            "duplicate-slot-key": lambda d: d["topologies"][0]["accessSlots"][1].update({"routerId": d["topologies"][0]["accessSlots"][0]["routerId"], "templateKey": d["topologies"][0]["accessSlots"][0]["templateKey"]}),
            "router-slot-mismatch": lambda d: d["topologies"][0]["attachments"][0].__setitem__("routerId", "router.0.1"),
            "occupied-slot": lambda d: (d["interfaces"].append({**copy.deepcopy(d["interfaces"][0]), "id":"interface.second"}), d["topologies"][0]["attachments"].append({**copy.deepcopy(d["topologies"][0]["attachments"][0]), "id":"attachment.second", "interfaceId":"interface.second"})),
            "two-defaults": lambda d: d["topologies"][0]["domains"].append({**copy.deepcopy(d["topologies"][0]["domains"][0]), "id":"domain.power.second"}),
            "missing-topology-owner": lambda d: d["topologies"][0].__setitem__("ownerComponentId", "component.missing"),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                document = copy.deepcopy(source)
                mutate(document)
                result = subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", document)
                self.assertIsNotNone(result.phase)
                self.assertIn(result.failure_boundary, {"project-reference", "project-invariant"})

    def test_schema_keyword_coverage_is_closed(self) -> None:
        self.assertEqual(subject.audit_schema_keywords(CONTRACTS), set())
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            schema = root / "schemas/ipcraft.command-result.v1.schema.json"
            document = json.loads(schema.read_text())
            document["unsupportedKeyword"] = True
            schema.write_text(json.dumps(document))
            self.assertTrue(any(path.endswith("/unsupportedKeyword") for path in subject.audit_schema_keywords(root)))
            with self.assertRaisesRegex(subject.FixtureVerificationError, "unsupported JSON Schema keywords"):
                subject.verify_all(root)

    def test_schema_validator_rejects_mutated_valid_fixture(self) -> None:
        catalog = subject.load_catalog(CONTRACTS)
        entry = next(item for item in catalog if item["expected"] == "accept")
        document = json.loads((CONTRACTS / entry["path"]).read_text())
        document.pop("schema", None)
        result = subject.classify_document(CONTRACTS, entry["schemaId"], document)
        self.assertEqual(result.phase, "schema")

    def test_schema_validator_uses_json_numeric_equality_and_rfc3339_time(self) -> None:
        contract = json.loads((CONTRACTS / "fixtures/valid/interface-contract-axi5.json").read_text())
        contract["capabilities"][0]["values"] = [128, 128.0]
        self.assertEqual(subject.classify_document(CONTRACTS, "ipcraft.interface-contract.v1", contract).phase, "schema")
        pipeline = json.loads((CONTRACTS / "fixtures/valid/pipeline-result.json").read_text())
        pipeline["startedAt"] = "2026-07-14T00:00:00"
        self.assertEqual(subject.classify_document(CONTRACTS, "ipcraft.pipeline-result.v1", pipeline).phase, "schema")

    def test_physical_json_duplicate_members_and_non_json_numbers_fail_closed(self) -> None:
        for raw in ('{"schema":"ipcraft.bundle-manifest.v1","schema":"duplicate"}', '{"schema":NaN}'):
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "fixture.json"
                path.write_text(raw)
                with self.assertRaises(subject.FixtureVerificationError):
                    subject.load_strict_json(path)

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
