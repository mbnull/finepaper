#!/usr/bin/env python3
"""Executable regression tests for the populated Gate 0 fixture set."""

from __future__ import annotations

import copy
import hashlib
import json
import math
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
        "fixtures/invalid/project-duplicate-root-extension-key.json",
        "fixtures/invalid/project-duplicate-relation-source-key.json",
        "fixtures/invalid/project-duplicate-slot-allowed-contract-key.json",
        "fixtures/invalid/project-duplicate-slot-role-key.json",
        "fixtures/invalid/project-domain-membership-wrong-kind.json",
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
            "membership-domain-wrong-kind": lambda d: d["topologies"][0]["domainMemberships"][0].__setitem__("domainId", "interface.axi"),
            "membership-router-wrong-kind": lambda d: d["topologies"][0]["domainMemberships"][0].__setitem__("routerId", "interface.axi"),
            "slot-router-wrong-kind": lambda d: d["topologies"][0]["accessSlots"][0].__setitem__("routerId", "interface.axi"),
            "link-endpoint-wrong-kind": lambda d: d["topologies"][0]["structuralLinks"][0].__setitem__("endpointA", "interface.axi"),
            "attachment-interface-wrong-kind": lambda d: d["topologies"][0]["attachments"][0].__setitem__("interfaceId", "router.0.0"),
            "attachment-router-wrong-kind": lambda d: d["topologies"][0]["attachments"][0].__setitem__("routerId", "interface.axi"),
            "attachment-slot-wrong-kind": lambda d: d["topologies"][0]["attachments"][0].__setitem__("slotId", "router.0.0"),
            "topology-owner-wrong-kind": lambda d: d["topologies"][0].__setitem__("ownerComponentId", "interface.axi"),
            "component-package-wrong-kind": lambda d: d["components"][0].__setitem__("packageLockId", "dep.contract.axi5"),
            "interface-contract-wrong-kind": lambda d: d["interfaces"][0]["contract"].__setitem__("lockId", "dep.noc.simple"),
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

    def test_reviewer_items_false_rejects_nonempty_pipeline_steps(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            schema_path = root / "schemas/ipcraft.pipeline-result.v1.schema.json"
            schema = json.loads(schema_path.read_text())
            schema["properties"]["steps"]["items"] = False
            schema_path.write_text(json.dumps(schema))
            self.assertEqual(subject.audit_schema_keywords(root), set())
            with self.assertRaises(subject.FixtureVerificationError):
                subject.verify_all(root)

    def test_boolean_schemas_work_in_nested_schema_positions(self) -> None:
        pipeline = json.loads((CONTRACTS / "fixtures/valid/pipeline-result.json").read_text())

        def validate_with(mutate, should_pass: bool) -> None:
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "contracts"
                subject.copy_contract_tree(CONTRACTS, root)
                schema_path = root / "schemas/ipcraft.pipeline-result.v1.schema.json"
                schema = json.loads(schema_path.read_text())
                instance = copy.deepcopy(pipeline)
                mutate(schema, instance)
                schema_path.write_text(json.dumps(schema))
                self.assertEqual(subject.audit_schema_keywords(root), set())
                validator = subject.Draft202012Subset(root)
                if should_pass:
                    validator.validate("ipcraft.pipeline-result.v1", instance)
                else:
                    with self.assertRaises(subject.SchemaFailure):
                        validator.validate("ipcraft.pipeline-result.v1", instance)

        cases = (
            (lambda s, _i: s["properties"].__setitem__("pipelineRunId", True), True),
            (lambda s, _i: s["properties"].__setitem__("pipelineRunId", False), False),
            (lambda s, i: (s.__setitem__("additionalProperties", True), i.update({"extra": True})), True),
            (lambda s, i: (s.__setitem__("additionalProperties", False), i.update({"extra": True})), False),
            (lambda s, _i: s["properties"]["steps"].__setitem__("items", False), False),
            (lambda s, i: (s["properties"]["steps"].__setitem__("items", True), i["steps"][0].__setitem__("extra", True)), True),
            (lambda s, _i: s["allOf"].append(True), True),
            (lambda s, _i: s["allOf"].append(False), False),
            (lambda s, _i: s.__setitem__("if", True) or s.__setitem__("then", False), False),
            (lambda s, _i: s.__setitem__("not", False), True),
            (lambda s, _i: s["$defs"].__setitem__("id", False), False),
        )
        for index, (mutate, should_pass) in enumerate(cases):
            with self.subTest(index=index):
                validate_with(mutate, should_pass)

    def test_schema_form_audit_rejects_recognized_keyword_with_unsupported_shape(self) -> None:
        mutations = (
            lambda schema: schema.__setitem__("$schema", 7),
            lambda schema: schema.__setitem__("$id", False),
            lambda schema: schema.__setitem__("$id", "not a valid id"),
            lambda schema: schema.__setitem__("$id", "bad%ZZ"),
            lambda schema: schema.__setitem__("$id", "bad\nvalue"),
            lambda schema: schema.__setitem__("$ref", []),
            lambda schema: schema.__setitem__("$ref", "not a valid ref with spaces"),
            lambda schema: schema.__setitem__("$ref", "#/$defs/%ZZ"),
            lambda schema: schema.__setitem__("$ref", "#/$defs/id\n"),
            lambda schema: schema.__setitem__("$ref", "ipcraft.project-design.v1.schema.json#/$defs/dependencyLock/oneOf/-1"),
            lambda schema: schema.__setitem__("$ref", "ipcraft.project-design.v1.schema.json#/$defs/dependencyLock/oneOf/+1"),
            lambda schema: schema.__setitem__("$ref", "ipcraft.project-design.v1.schema.json#/$defs/dependencyLock/oneOf/01"),
            lambda schema: schema.__setitem__("$ref", "ipcraft.project-design.v1.schema.json#/$defs/dependencyLock/oneOf/-"),
            lambda schema: schema.__setitem__("$ref", "ipcraft.project-design.v1.schema.json#/$defs/dependencyLock/oneOf/999999999999999999999999"),
            lambda schema: schema.__setitem__("$ref", "#/$defs/~2bad"),
            lambda schema: schema.__setitem__("$ref", "#/$defs/bad~"),
            lambda schema: schema.__setitem__("$comment", {}),
            lambda schema: schema.__setitem__("title", []),
            lambda schema: schema.__setitem__("description", 1),
            lambda schema: schema.__setitem__("type", "bytes"),
            lambda schema: schema.__setitem__("type", []),
            lambda schema: schema.__setitem__("type", ["object", "object"]),
            lambda schema: schema.__setitem__("type", ["object", 1]),
            lambda schema: schema.__setitem__("enum", "value"),
            lambda schema: schema.__setitem__("enum", []),
            lambda schema: schema.__setitem__("enum", [1, 1.0]),
            lambda schema: schema.__setitem__("properties", []),
            lambda schema: schema.__setitem__("properties", {"bad": 1}),
            lambda schema: schema.__setitem__("$defs", {"bad": None}),
            lambda schema: schema.__setitem__("required", "schema"),
            lambda schema: schema.__setitem__("required", ["schema", 1]),
            lambda schema: schema.__setitem__("required", ["schema", "schema"]),
            lambda schema: schema.__setitem__("allOf", {}),
            lambda schema: schema.__setitem__("allOf", []),
            lambda schema: schema.__setitem__("anyOf", [1]),
            lambda schema: schema.__setitem__("oneOf", [None]),
            lambda schema: schema["properties"]["steps"].__setitem__("items", 7),
            lambda schema: schema.__setitem__("additionalProperties", "false"),
            lambda schema: schema.__setitem__("contains", 1),
            lambda schema: schema.__setitem__("not", None),
            lambda schema: schema.__setitem__("if", "true"),
            lambda schema: schema.__setitem__("then", 0),
            lambda schema: schema.__setitem__("else", []),
            lambda schema: schema.__setitem__("minItems", -1),
            lambda schema: schema.__setitem__("maxItems", 1.5),
            lambda schema: schema.__setitem__("minContains", False),
            lambda schema: schema.__setitem__("maxContains", -1),
            lambda schema: schema.__setitem__("minLength", -1),
            lambda schema: schema.__setitem__("uniqueItems", "true"),
            lambda schema: schema.__setitem__("minimum", False),
            lambda schema: schema.__setitem__("maximum", "1"),
            lambda schema: schema.__setitem__("exclusiveMinimum", None),
            lambda schema: schema.__setitem__("multipleOf", 0),
            lambda schema: schema.__setitem__("multipleOf", False),
            lambda schema: schema.__setitem__("pattern", 1),
            lambda schema: schema.__setitem__("pattern", "["),
            lambda schema: schema.__setitem__("pattern", "(?P<n>a)"),
            lambda schema: schema.__setitem__("pattern", r"(a)\1"),
            lambda schema: schema.__setitem__("pattern", "(?i)a"),
            lambda schema: schema.__setitem__("pattern", "[a&&b]"),
            lambda schema: schema.__setitem__("pattern", r"^\d+$"),
            lambda schema: schema.__setitem__("pattern", r"^\w+$"),
            lambda schema: schema.__setitem__("pattern", r"^\s+$"),
            lambda schema: schema.__setitem__("pattern", r"^\b.$"),
            lambda schema: schema.__setitem__("format", 1),
            lambda schema: schema.__setitem__("format", "uuid"),
            lambda schema: schema.__setitem__("x-ipcraft-canonical", []),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "contracts"
                subject.copy_contract_tree(CONTRACTS, root)
                schema_path = root / "schemas/ipcraft.pipeline-result.v1.schema.json"
                schema = json.loads(schema_path.read_text())
                mutate(schema)
                schema_path.write_text(json.dumps(schema))
                self.assertTrue(subject.audit_schema_keywords(root))
                with self.assertRaisesRegex(subject.FixtureVerificationError, "unsupported JSON Schema keywords"):
                    subject.verify_all(root)

    def test_schema_form_audit_accepts_empty_id_uri_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            schema_path = root / "schemas/ipcraft.pipeline-result.v1.schema.json"
            schema = json.loads(schema_path.read_text())
            schema["$id"] = ""
            schema_path.write_text(json.dumps(schema))
            self.assertEqual(subject.audit_schema_keywords(root), set())
            validator = subject.Draft202012Subset(root)
            schema_root, root_path = validator.schemas["ipcraft.pipeline-result.v1"]
            validator._validate({"type": "string", "pattern": "^.$"}, "é", schema_root, root_path, "$")
            with self.assertRaises(subject.SchemaFailure):
                validator._validate({"type": "string", "pattern": "^.$"}, "\r", schema_root, root_path, "$")
            with self.assertRaises(subject.SchemaFailure):
                validator._validate({"type": "string", "pattern": "^a$"}, "a\n", schema_root, root_path, "$")

    def test_schema_form_audit_accepts_supported_local_references_and_regex(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            schema_path = root / "schemas/ipcraft.pipeline-result.v1.schema.json"
            schema = json.loads(schema_path.read_text())
            schema["$id"] = "local.schema-v1"
            schema["properties"]["pipelineRunId"] = {
                "$ref": "ipcraft.core-canonical-models.v1.schema.json#/$defs/id"
            }
            schema["$defs"]["validArrayReference"] = {
                "$ref": "ipcraft.project-design.v1.schema.json#/$defs/dependencyLock/oneOf/0"
            }
            schema["properties"]["kind"]["pattern"] = r"^(?:validate|generate)$"
            schema_path.write_text(json.dumps(schema))
            self.assertEqual(subject.audit_schema_keywords(root), set())

    def test_schema_form_audit_resolves_every_unused_reference(self) -> None:
        for reference in (
            "missing.schema.json#/$defs/id",
            "ipcraft.core-canonical-models.v1.schema.json#/$defs/missing",
        ):
            with self.subTest(reference=reference), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "contracts"
                subject.copy_contract_tree(CONTRACTS, root)
                schema_path = root / "schemas/ipcraft.pipeline-result.v1.schema.json"
                schema = json.loads(schema_path.read_text())
                schema["$defs"]["unusedBroken"] = {"$ref": reference}
                schema_path.write_text(json.dumps(schema))
                self.assertTrue(subject.audit_schema_keywords(root))
                with self.assertRaisesRegex(subject.FixtureVerificationError, "unsupported JSON Schema keywords"):
                    subject.verify_all(root)

    def test_sample_builder_handles_boolean_schemas_without_type_errors(self) -> None:
        builder = generate_contract_fixtures.SampleBuilder(CONTRACTS)
        root_path = CONTRACTS / "schemas/authoring-test.schema.json"
        true_property = {
            "type": "object", "required": ["free"], "properties": {"free": True}
        }
        self.assertEqual(builder.build(true_property, true_property, root_path), {"free": None})
        true_items = {"type": "array", "minItems": 1, "items": True}
        self.assertEqual(builder.build(true_items, true_items, root_path), [None])
        true_ref = {"$defs": {"free": True}, "$ref": "#/$defs/free"}
        self.assertIsNone(builder.build(true_ref, true_ref, root_path))
        self.assertIsNone(builder.build({"anyOf": [False, True]}, {}, root_path))
        self.assertIsNone(builder.build({"oneOf": [False, True]}, {}, root_path))
        for schema in ({"oneOf": [True, True]}, {"oneOf": [True, {}]}):
            with self.subTest(schema=schema), self.assertRaisesRegex(
                generate_contract_fixtures.UnsatisfiableSampleError, "oneOf has no satisfiable branch"
            ):
                builder.build(schema, schema, root_path)

        false_positions = (
            ({"type": "object", "required": ["blocked"], "properties": {"blocked": False}}, "required property blocked"),
            ({"type": "array", "minItems": 1, "items": False}, "array item"),
            ({"$defs": {"blocked": False}, "$ref": "#/$defs/blocked"}, "false schema"),
            ({"allOf": [False]}, "false schema"),
        )
        for schema, message in false_positions:
            with self.subTest(message=message), self.assertRaisesRegex(
                generate_contract_fixtures.UnsatisfiableSampleError, message
            ):
                builder.build(schema, schema, root_path)

        self.assertEqual(
            builder.build({"type": "number", "minimum": 0.1, "multipleOf": 0.2}, {}, root_path),
            0.2,
        )
        self.assertEqual(builder.build({"type": "integer", "minimum": 1.5}, {}, root_path), 2)

    def test_project_canonical_set_duplicates_are_project_invariants(self) -> None:
        source = json.loads((CONTRACTS / "fixtures/valid/mesh-2x2-package-extension.json").read_text())
        attached = json.loads((CONTRACTS / "fixtures/valid/mesh-2x2-attached.json").read_text())
        mutations = {
            "root-extension": lambda d: d["extensions"].append(copy.deepcopy(d["extensions"][0])),
            "component-extension": lambda d: d["components"][0]["extensions"].extend(
                [copy.deepcopy(d["extensions"][0]), copy.deepcopy(d["extensions"][0])]
            ),
            "topology-extension": lambda d: d["topologies"][0]["extensions"].extend(
                [copy.deepcopy(d["extensions"][0]), copy.deepcopy(d["extensions"][0])]
            ),
            "package-entity-extension": lambda d: d["topologies"][0]["packageEntities"][0]["extensions"].extend(
                [copy.deepcopy(d["extensions"][0]), copy.deepcopy(d["extensions"][0])]
            ),
            "package-relation-extension": lambda d: d["topologies"][0]["packageRelations"][0]["extensions"].extend(
                [copy.deepcopy(d["extensions"][0]), copy.deepcopy(d["extensions"][0])]
            ),
            "relation-source": lambda d: d["topologies"][0]["packageRelations"][0]["sources"].append(
                copy.deepcopy(d["topologies"][0]["packageRelations"][0]["sources"][0])
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                document = copy.deepcopy(source)
                mutate(document)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", document),
                    subject.Classification("core-semantic", "project-invariant", "project.invariant_violation"),
                )
        for name, mutate in {
            "interface-extension": lambda d: d["interfaces"][0]["extensions"].extend(
                [copy.deepcopy(source["extensions"][0]), copy.deepcopy(source["extensions"][0])]
            ),
            "allowed-contract": lambda d: d["topologies"][0]["accessSlots"][0]["allowedContracts"].append(
                copy.deepcopy(d["topologies"][0]["accessSlots"][0]["allowedContracts"][0])
            ),
            "allowed-role": lambda d: d["topologies"][0]["accessSlots"][0]["allowedContracts"][0]["roles"].append("initiator"),
        }.items():
            with self.subTest(name=name):
                document = copy.deepcopy(attached)
                mutate(document)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", document),
                    subject.Classification("core-semantic", "project-invariant", "project.invariant_violation"),
                )

    def test_project_canonical_set_rule_coverage_fails_closed_on_index_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            index_path = root / "vectors/core-canonical-projection-v1.json"
            index = json.loads(index_path.read_text())
            index["canonicalCollections"] = [
                rule for rule in index["canonicalCollections"]
                if not (
                    rule["schemaId"] == "ipcraft.core-canonical-models.v1"
                    and rule["schemaPointer"] == "/$defs/accessSlot/properties/allowedContracts"
                )
            ]
            index_path.write_text(json.dumps(index))
            with self.assertRaisesRegex(subject.FixtureVerificationError, "canonical set rule missing"):
                subject.audit_project_canonical_set_coverage(root)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            schema_path = root / "schemas/ipcraft.core-canonical-models.v1.schema.json"
            schema = json.loads(schema_path.read_text())
            allowed = schema["$defs"]["accessSlot"]["properties"]["allowedContracts"]["items"]
            allowed["required"].remove("contractLockId")
            schema_path.write_text(json.dumps(schema))
            with self.assertRaisesRegex(subject.FixtureVerificationError, "no executable item property"):
                subject.audit_project_canonical_set_coverage(root)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            schema_path = root / "schemas/ipcraft.project-design.v1.schema.json"
            schema = json.loads(schema_path.read_text())
            schema["$defs"]["unresolvedRelationEndpoint"]["required"].remove("reasonCode")
            schema_path.write_text(json.dumps(schema))
            with self.assertRaisesRegex(subject.FixtureVerificationError, "persisted endpoint canonical key"):
                subject.audit_project_canonical_set_coverage(root)

    def test_json_schema_integer_accepts_integral_numbers_only(self) -> None:
        matches = subject.Draft202012Subset._matches_type
        for value in (1, 1.0, -0.0):
            with self.subTest(value=value):
                self.assertTrue(matches(value, "integer"))
        for value in (True, 1.5, math.inf, -math.inf, math.nan):
            with self.subTest(value=value):
                self.assertFalse(matches(value, "integer"))
        self.assertFalse(matches(math.inf, "number"))

        project = json.loads((CONTRACTS / "fixtures/valid/minimal-1x1.json").read_text())
        project["topologies"][0]["derivation"]["topologyInputRevision"] = 1.0
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", project).phase)
        project["topologies"][0]["derivation"]["topologyInputRevision"] = 1.5
        self.assertEqual(subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", project).phase, "schema")

        validator = subject.Draft202012Subset(CONTRACTS)
        root, root_path = validator.schemas["ipcraft.pipeline-result.v1"]
        numeric_schema = {"type": "number", "multipleOf": 0.1, "minimum": 0, "maximum": 1}
        validator._validate(numeric_schema, 0.3, root, root_path, "$")
        with self.assertRaises(subject.SchemaFailure):
            validator._validate(numeric_schema, 0.31, root, root_path, "$")

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

    def test_strict_json_preserves_exact_numeric_tokens(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "exact.json"
            path.write_text('{"x":1e-400,"y":0.3000000000000000000000001}')
            document = subject.load_strict_json(path)
            self.assertFalse(subject.Draft202012Subset._matches_type(document["x"], "integer"))
            validator = subject.Draft202012Subset(CONTRACTS)
            root, root_path = validator.schemas["ipcraft.pipeline-result.v1"]
            with self.assertRaises(subject.SchemaFailure):
                validator._validate({"type": "number", "multipleOf": 0.1}, document["y"], root, root_path, "$")

    def test_manifest_paths_use_pinned_portable_identity(self) -> None:
        bundle_source = json.loads((CONTRACTS / "fixtures/valid/bundle-manifest.json").read_text())
        artifact_source = json.loads((CONTRACTS / "fixtures/valid/artifact-manifest.json").read_text())
        digest = "sha256:" + "a" * 64

        def bundle_entry(path: str) -> dict:
            return {"path": path, "size": 1, "digest": digest, "executable": False}

        def artifact_entry(path: str) -> dict:
            return {"path": path, "kind": "rtl", "mediaType": "text/plain", "size": 1, "digest": digest}

        cases = (
            ["A.txt", "a.txt"],
            ["Σ.txt", "ς.txt"],
            ["e\u0301.txt"],
            ["CON.txt"],
            ["trailing."],
            ["../escape.txt"],
        )
        for paths in cases:
            for schema_id, source, make_entry in (
                ("ipcraft.bundle-manifest.v1", bundle_source, bundle_entry),
                ("ipcraft.artifact-manifest.v1", artifact_source, artifact_entry),
            ):
                with self.subTest(schema_id=schema_id, paths=paths):
                    document = copy.deepcopy(source)
                    member = "files" if schema_id == "ipcraft.bundle-manifest.v1" else "artifacts"
                    document[member] = [make_entry(path) for path in paths]
                    self.assertIsNotNone(subject.classify_document(CONTRACTS, schema_id, document).phase)

    def test_manifest_relative_pointer_paths_are_portable(self) -> None:
        cases = (
            ("ipcraft.tool-input.v1", "tool-input.json", "projectDesignFile"),
            ("ipcraft.pipeline-result.v1", "pipeline-result.json", "step-result"),
            ("ipcraft.step-result.v1", "step-result.json", "toolResult"),
            ("ipcraft.tool-result.v1", "tool-result.json", "diagnosticReport"),
        )
        for schema_id, filename, member in cases:
            with self.subTest(schema_id=schema_id):
                document = json.loads((CONTRACTS / "fixtures/valid" / filename).read_text())
                if member == "step-result":
                    document["steps"][0]["result"] = "CON.txt"
                else:
                    document[member] = "CON.txt"
                self.assertIsNotNone(subject.classify_document(CONTRACTS, schema_id, document).phase)

        engine = json.loads((CONTRACTS / "fixtures/valid/engine-bundle.json").read_text())
        engine["entrypoint"] = "CON.exe"
        self.assertEqual(
            subject.classify_document(CONTRACTS, "ipcraft.engine-bundle.v1", engine),
            subject.Classification("core-semantic", "engine-bundle-binding", "engine.bundle_mismatch"),
        )

    def test_tool_input_path_roles_are_closed(self) -> None:
        source = json.loads((CONTRACTS / "fixtures/valid/tool-input.json").read_text())
        cases = {
            "input-result-alias": lambda d: d.__setitem__("resultFile", d["projectDesignFile"]),
            "result-outside-report": lambda d: d.__setitem__("resultFile", "other/tool-result.json"),
            "report-is-input": lambda d: d.__setitem__("reportDirectory", "inputs"),
        }
        for name, mutate in cases.items():
            with self.subTest(name=name):
                document = copy.deepcopy(source)
                mutate(document)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.tool-input.v1", document),
                    subject.Classification("core-semantic", "tool-input", "tool.input_invalid"),
                )

    def test_noc_package_declaration_invariants_are_table_driven(self) -> None:
        source = json.loads((CONTRACTS / "fixtures/valid/noc-package.json").read_text())
        package_contained = json.loads((CONTRACTS / "fixtures/valid/noc-package-maximum.json").read_text())
        package_contained["tools"]["drc"]["command"][0] = "--package-contained-mode"
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.noc-package.v1", package_contained).phase)

        def duplicate_global(d):
            d["configuration"]["global"]["fields"].append(copy.deepcopy(d["configuration"]["global"]["fields"][0]))

        def bad_default(d):
            d["configuration"]["global"]["fields"][0]["default"] = "not-an-int"

        def bad_condition(d):
            d["configuration"]["global"]["fields"][0]["visibleWhen"] = {"field": "missing", "equals": 1}

        def bad_cardinality(d):
            d["packageRelationTypes"][0]["sources"].update({"minimum": 2, "maximum": 1})

        def duplicate_slot(d):
            template = {
                "stableKey": "local", "identityCompatibilityVersion": 1, "displayOrder": 0,
                "label": "Local", "allowedContracts": [], "properties": {},
            }
            d["topology"]["slotTemplates"] = [template, copy.deepcopy(template)]

        for name, mutate in {
            "duplicate-global-field": duplicate_global,
            "field-default-type": bad_default,
            "condition-reference": bad_condition,
            "relation-cardinality": bad_cardinality,
            "duplicate-slot": duplicate_slot,
        }.items():
            with self.subTest(name=name):
                document = copy.deepcopy(source)
                mutate(document)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.noc-package.v1", document),
                    subject.Classification("core-semantic", "package-declaration", "package.invariant_violation"),
                )

    def test_patch_operation_and_source_matrix_is_executable(self) -> None:
        matrix = json.loads((CONTRACTS / "patch-validation-context-v1.json").read_text())
        self.assertEqual(matrix["schema"], "ipcraft.patch-validation-context.v1")
        operation_kinds = {
            "createEntity", "updateEntity", "deleteEntity",
            "createRelation", "updateRelation", "deleteRelation",
        }
        source_kinds = {
            "user-command", "application-reconcile", "application-migration",
            "default-engine", "extension-provider", "recovery", "undo-redo",
        }
        self.assertEqual({item["selectedAuthority"]["kind"] for item in matrix["authorityContexts"]}, {"default-engine", "extension-provider"})
        catalog = subject.load_catalog(CONTRACTS)
        accepted = {
            Path(item["path"]).stem for item in catalog
            if item["schemaId"] == "ipcraft.patch.v1" and item["expected"] == "accept"
        }
        for operation in operation_kinds:
            self.assertIn(f"patch-operation-{operation}", accepted)
        for source in source_kinds:
            self.assertIn(f"patch-source-{source}", accepted)

    def test_valid_fixture_coverage_matrix_closes_all_roots_and_tiers(self) -> None:
        matrix = json.loads((CONTRACTS / "fixture-coverage-v1.json").read_text())
        self.assertEqual(matrix["schema"], "ipcraft.fixture-coverage.v1")
        roots = {
            item["id"] for item in json.loads((CONTRACTS / "schema-catalog.json").read_text())["items"]
            if item["id"] != "ipcraft.fixture-catalog.v1"
        }
        self.assertEqual(set(matrix["roots"]), roots)
        catalog = {item["path"]: item for item in subject.load_catalog(CONTRACTS)}
        for schema_id, tiers in matrix["roots"].items():
            self.assertEqual(set(tiers), {"minimal", "representative", "maximumShape"})
            for tier, paths in tiers.items():
                self.assertTrue(paths, f"{schema_id} {tier} must not be empty")
                for path in paths:
                    with self.subTest(schema_id=schema_id, tier=tier, path=path):
                        self.assertEqual(catalog[path]["schemaId"], schema_id)
                        self.assertEqual(catalog[path]["expected"], "accept")
                        document = json.loads((CONTRACTS / path).read_text())
                        self.assertIsNone(subject.classify_document(CONTRACTS, schema_id, document).phase)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            mutated = json.loads((root / "fixture-coverage-v1.json").read_text())
            mutated["requirements"]["ipcraft.artifact-manifest.v1"]["maximumShape"]["minimumArrayLengths"]["/artifacts"] = 3
            (root / "fixture-coverage-v1.json").write_text(json.dumps(mutated))
            with self.assertRaises(subject.FixtureVerificationError):
                subject.verify_all(root)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            mutated = json.loads((root / "fixture-coverage-v1.json").read_text())
            mutated["requirements"]["ipcraft.bundle-manifest.v1"]["maximumShape"]["minimumArrayLenghts"] = {"/files": 999}
            (root / "fixture-coverage-v1.json").write_text(json.dumps(mutated))
            with self.assertRaises(subject.FixtureVerificationError):
                subject.verify_all(root)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            mutated = json.loads((root / "fixture-coverage-v1.json").read_text())
            mutated["roots"]["ipcraft.noc-package.v1"]["maximumShape"] = ["fixtures/valid/noc-package-maximum.json"]
            (root / "fixture-coverage-v1.json").write_text(json.dumps(mutated))
            with self.assertRaisesRegex(subject.FixtureVerificationError, "coverage (root/tier mapping differs|required pointer missing|discriminator predicate failed)"):
                subject.verify_all(root)

    def test_patch_authority_and_relation_lifecycle_fail_closed(self) -> None:
        provider = json.loads((CONTRACTS / "fixtures/valid/patch-source-extension-provider.json").read_text())
        authority = provider["applicability"]["structureAuthority"]
        self.assertEqual(authority["kind"], "extension-provider")
        self.assertEqual(authority["bundleDigest"], provider["source"]["bundleDigest"])
        mismatch = copy.deepcopy(provider)
        mismatch["applicability"]["structureAuthority"]["bundleDigest"] = "sha256:" + "a" * 64
        self.assertEqual(
            subject.classify_document(CONTRACTS, "ipcraft.patch.v1", mismatch),
            subject.Classification("core-semantic", "structure-authority", "patch.authority_conflict"),
        )
        relation = json.loads((CONTRACTS / "fixtures/valid/patch-operation-updateRelation.json").read_text())
        relation["operations"][0].update({
            "relationKind":"domain-membership", "id":"membership.0", "set":{}, "unset":["domainRef"],
        })
        self.assertEqual(
            subject.classify_document(CONTRACTS, "ipcraft.patch.v1", relation),
            subject.Classification("core-semantic", "patched-subject-schema", "patch.schema_violation"),
        )
        for filename in (
            "patch-user-mutates-engine-package-entity.json",
            "patch-authority-mutates-user-package-entity.json",
            "patch-user-mutates-engine-package-relation.json",
            "patch-authority-mutates-user-package-relation.json",
        ):
            with self.subTest(filename=filename):
                document = json.loads((CONTRACTS / "fixtures/invalid" / filename).read_text())
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                    subject.Classification("core-semantic", "ownership", "patch.ownership_violation"),
                )

    def test_project_structure_authority_resolves_selected_lock_kind(self) -> None:
        provider = json.loads((CONTRACTS / "fixtures/valid/project-provider-authority.json").read_text())
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", provider).phase)
        authority = provider["topologies"][0]["derivation"]["structureAuthority"]
        dependency = next(item for item in provider["dependencies"] if item["lockId"] == authority["lockId"])
        self.assertEqual(dependency["kind"], "extension-provider")
        for name, (mutate, boundary, code) in {
            "missing": (lambda d: d["topologies"][0]["derivation"]["structureAuthority"].__setitem__("lockId", "dep.missing"), "project-reference", "project.unknown_reference"),
            "wrong-kind": (lambda d: d["topologies"][0]["derivation"]["structureAuthority"].__setitem__("lockId", "dep.engine.default"), "project-reference", "project.unknown_reference"),
            "identity": (lambda d: d["topologies"][0]["derivation"]["structureAuthority"].__setitem__("identity", "wrong.provider"), "project-invariant", "project.invariant_violation"),
            "version": (lambda d: d["topologies"][0]["derivation"]["structureAuthority"].__setitem__("version", "wrong"), "project-invariant", "project.invariant_violation"),
            "digest": (lambda d: d["topologies"][0]["derivation"]["structureAuthority"].__setitem__("bundleDigest", "sha256:" + "f" * 64), "project-invariant", "project.invariant_violation"),
        }.items():
            with self.subTest(name=name):
                document = copy.deepcopy(provider)
                mutate(document)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.project-design.v1", document),
                    subject.Classification("core-semantic", boundary, code),
                )

    def test_patch_current_state_reviewer_reproductions_fail_closed(self) -> None:
        cases = {
            "attachment-transition": ("patch-operation-updateRelation.json", lambda d: d["operations"][0].update({
                "relationKind":"attachment", "id":"attachment.boundary",
                "set":{"state":"unresolved"}, "unset":["routerRef","slotRef"],
            }), "patched-subject-schema", "patch.schema_violation"),
            "interface-owner": ("patch-operation-updateEntity.json", lambda d: d["operations"][0].update({
                "entityKind":"interface", "id":"interface.boundary", "set":{}, "unset":["ownerComponentRef"],
            }), "patched-subject-schema", "patch.schema_violation"),
            "slot-missing-router": ("patch-source-default-engine.json", lambda d: d["operations"].__setitem__(0, {
                "op":"createEntity","entityKind":"access-slot","localRef":"authority:missing-slot",
                "value":{"routerRef":{"id":"router.missing"},"templateKey":"local-x","identityCompatibilityVersion":1,"displayOrder":2,"label":"Missing","allowedContracts":[],"properties":{}},
            }), "reference", "patch.unknown_reference"),
            "duplicate-slot-role": ("patch-source-default-engine.json", lambda d: d["operations"].__setitem__(0, {
                "op":"createEntity","entityKind":"access-slot","localRef":"authority:duplicate-role-slot",
                "value":{"routerRef":{"id":"router.0.0"},"templateKey":"local-x","identityCompatibilityVersion":1,"displayOrder":2,"label":"Duplicate","allowedContracts":[{"contractLockId":"dep.contract.axi5","roles":["initiator","initiator"],"capabilityConstraints":{}}],"properties":{}},
            }), "patch-invariant", "patch.invariant_violation"),
            "reconcile-rename": ("patch-source-application-reconcile.json", lambda d: d["operations"].append({
                "op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"name":"Forbidden"},"unset":[],
            }), "ownership", "patch.ownership_violation"),
        }
        for name, (filename, mutate, boundary, code) in cases.items():
            with self.subTest(name=name):
                document = json.loads((CONTRACTS / "fixtures/valid" / filename).read_text())
                mutate(document)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                    subject.Classification("core-semantic", boundary, code),
                )

        preconditions = json.loads((CONTRACTS / "fixtures/valid/patch-preconditions-all.json").read_text())
        preconditions["preconditions"].append(copy.deepcopy(preconditions["preconditions"][0]))
        self.assertEqual(
            subject.classify_document(CONTRACTS, "ipcraft.patch.v1", preconditions),
            subject.Classification("core-semantic", "patch-invariant", "patch.invariant_violation"),
        )

    def test_patch_broader_state_machine_attacks_fail_closed(self) -> None:
        expected = {
            "patch-local-slot-double-occupancy.json": ("patch-invariant", "patch.invariant_violation"),
            "patch-local-slot-router-mismatch.json": ("patch-invariant", "patch.invariant_violation"),
            "patch-structural-link-self.json": ("patch-invariant", "patch.invariant_violation"),
            "patch-reconcile-package-relation-engine-update.json": ("ownership", "patch.ownership_violation"),
            "patch-reconcile-package-relation-engine-delete.json": ("ownership", "patch.ownership_violation"),
            "patch-reconcile-package-relation-user-data.json": ("ownership", "patch.ownership_violation"),
        }
        for filename, (boundary, code) in expected.items():
            with self.subTest(filename=filename):
                document = json.loads((CONTRACTS / "fixtures/invalid" / filename).read_text())
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                    subject.Classification("core-semantic", boundary, code),
                )
        allowed = json.loads((CONTRACTS / "fixtures/valid/patch-application-package-relation-unresolved.json").read_text())
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.patch.v1", allowed).phase)
        allowed_attachment = json.loads((CONTRACTS / "fixtures/valid/patch-application-attachment-unresolved.json").read_text())
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.patch.v1", allowed_attachment).phase)
        for document, path in (
            (copy.deepcopy(allowed), ("sources", 0)),
            (copy.deepcopy(allowed_attachment), ("attachment", 0)),
        ):
            if path[0] == "sources":
                document["operations"][-1]["set"]["sources"][path[1]]["reasonCode"] = "arbitrary"
            else:
                document["operations"][-1]["set"]["reasonCode"] = "arbitrary"
            self.assertEqual(
                subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                subject.Classification("core-semantic", "ownership", "patch.ownership_violation"),
            )
        replay = json.loads((CONTRACTS / "fixtures/valid/patch-source-recovery.json").read_text())
        for name, mutate in {
            "operation": lambda d: d["operations"][0]["set"].__setitem__("name", "Tampered replay"),
            "transaction": lambda d: d.__setitem__("transactionId", "forged.transaction"),
            "causality": lambda d: d["causality"].__setitem__("sessionRevision", d["causality"]["sessionRevision"] + 1),
        }.items():
            with self.subTest(replay_mutation=name):
                forged = copy.deepcopy(replay)
                mutate(forged)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", forged),
                    subject.Classification("core-semantic", "source-authority", "patch.source_not_allowed"),
                )

    def test_topology_sources_bind_the_complete_frozen_applicability_tuple(self) -> None:
        context = json.loads((CONTRACTS / "patch-validation-context-v1.json").read_text())
        authority_contexts = {item["selectedAuthority"]["kind"]: item for item in context["authorityContexts"]}
        self.assertTrue(all("expectedApplicability" in item for item in authority_contexts.values()))
        source_files = {
            "default-engine": "patch-source-default-engine.json",
            "extension-provider": "patch-source-extension-provider.json",
            "application-reconcile": "patch-source-application-reconcile.json",
            "application-migration": "patch-source-application-migration.json",
        }
        scalar_members = (
            "groupId", "requestGeneration", "topologyInputRevision", "topologyInputDigest",
            "baseDerivedStateRevision", "baseDerivedStateDigest", "baseAuthoritativeDesignDigest",
            "packageBundleDigest", "reconcileDependencySetDigest", "defaultEngineLockId",
            "defaultEngineBundleDigest", "engineHostContractVersion", "hostSideEffectContractVersion",
        )
        for source_kind, filename in source_files.items():
            with self.subTest(source=source_kind, member="baseline"):
                document = json.loads((CONTRACTS / "fixtures/valid" / filename).read_text())
                expected_kind = "extension-provider" if source_kind == "extension-provider" else "default-engine"
                self.assertEqual(document["applicability"], authority_contexts[expected_kind]["expectedApplicability"])
            for member in scalar_members:
                with self.subTest(source=source_kind, member=member):
                    mutated = copy.deepcopy(document)
                    value = mutated["applicability"][member]
                    mutated["applicability"][member] = value + 1 if isinstance(value, int) else (
                        "sha256:" + "f" * 64 if str(value).startswith("sha256:") else "wrong"
                    )
                    observed = subject.classify_document(CONTRACTS, "ipcraft.patch.v1", mutated)
                    self.assertIsNotNone(observed.phase)
                    if observed.phase == "core-semantic":
                        self.assertEqual(observed, subject.Classification("core-semantic", "structure-authority", "patch.authority_conflict"))

            for member in ("kind", "lockId", "identity", "version", "bundleDigest"):
                with self.subTest(source=source_kind, authority_member=member):
                    mutated = copy.deepcopy(document)
                    value = mutated["applicability"]["structureAuthority"][member]
                    mutated["applicability"]["structureAuthority"][member] = (
                        "sha256:" + "f" * 64 if str(value).startswith("sha256:") else "wrong"
                    )
                    observed = subject.classify_document(CONTRACTS, "ipcraft.patch.v1", mutated)
                    self.assertIsNotNone(observed.phase)
                    if observed.phase == "core-semantic":
                        self.assertEqual(observed, subject.Classification("core-semantic", "structure-authority", "patch.authority_conflict"))

        provider_application = json.loads((CONTRACTS / "fixtures/valid/patch-source-application-reconcile-provider-authority.json").read_text())
        self.assertEqual(provider_application["applicability"], authority_contexts["extension-provider"]["expectedApplicability"])
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.patch.v1", provider_application).phase)

    def test_patch_typed_references_are_dependency_kind_checked(self) -> None:
        context = json.loads((CONTRACTS / "patch-validation-context-v1.json").read_text())
        dependency_kinds = {item["kind"] for item in context["dependencyLocks"]}
        self.assertEqual(dependency_kinds, {
            "noc-package", "interface-contract", "default-engine", "extension-provider",
            "runtime", "drc-tool", "generator-tool",
        })
        interface = json.loads((CONTRACTS / "fixtures/valid/patch-source-user-command.json").read_text())
        slot = json.loads((CONTRACTS / "fixtures/valid/patch-source-default-engine.json").read_text())
        cases = {
            "interface-missing-contract": (interface, lambda d: d["operations"][0]["value"]["contract"].__setitem__("lockId", "dep.missing")),
            "interface-wrong-kind-contract": (interface, lambda d: d["operations"][0]["value"]["contract"].__setitem__("lockId", "dep.noc.simple")),
            "slot-missing-contract": (slot, lambda d: d["operations"].__setitem__(0, {
                "op":"createEntity", "entityKind":"access-slot", "localRef":"authority:slot.contract",
                "value":{"routerRef":{"id":"router.0.0"}, "templateKey":"typed-ref", "identityCompatibilityVersion":1,
                         "displayOrder":3, "label":"Typed", "allowedContracts":[{"contractLockId":"dep.missing", "roles":["initiator"], "capabilityConstraints":{}}], "properties":{}},
            })),
            "extension-missing-owner": (interface, lambda d: d["operations"][0]["value"]["extensions"].append({"ownerLockId":"dep.missing", "schema":"vendor.x.v1", "version":"1", "data":{}})),
            "extension-wrong-kind-owner": (interface, lambda d: d["operations"][0]["value"]["extensions"].append({"ownerLockId":"dep.runtime.provider", "schema":"vendor.x.v1", "version":"1", "data":{}})),
            "component-missing-package": (interface, lambda d: d["operations"].__setitem__(0, {
                "op":"createEntity", "entityKind":"component", "localRef":"application:000030",
                "value":{"kind":"noc", "name":"Other", "packageLockId":"dep.missing", "typeKey":"mesh-noc", "config":{}, "extensions":[]},
            })),
            "component-wrong-kind-package": (interface, lambda d: d["operations"].__setitem__(0, {
                "op":"createEntity", "entityKind":"component", "localRef":"application:000031",
                "value":{"kind":"noc", "name":"Other", "packageLockId":"dep.contract.axi5", "typeKey":"mesh-noc", "config":{}, "extensions":[]},
            })),
        }
        for name, (base, mutate) in cases.items():
            with self.subTest(name=name):
                document = copy.deepcopy(base)
                mutate(document)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                    subject.Classification("core-semantic", "reference", "patch.unknown_reference"),
                )
        migration = json.loads((CONTRACTS / "fixtures/valid/patch-source-application-migration.json").read_text())
        migration_cases = {
            "remove-live-contract": lambda deps: deps.__setitem__(slice(None), [item for item in deps if item["kind"] != "interface-contract"]),
            "provider-runtime-wrong-kind": lambda deps: next(item for item in deps if item["kind"] == "extension-provider").__setitem__("runtimeLockId", "dep.noc.simple"),
            "tool-runtime-missing": lambda deps: next(item for item in deps if item["kind"] == "drc-tool").__setitem__("runtimeLockId", "dep.missing"),
        }
        for name, mutate in migration_cases.items():
            with self.subTest(migration=name):
                document = copy.deepcopy(migration)
                mutate(document["operations"][0]["set"]["dependencies"])
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                    subject.Classification("core-semantic", "engine-migration-binding", "engine.migration_invalid"),
                )

    def test_package_type_key_is_immutable_and_cannot_launder_ownership(self) -> None:
        bases = {
            "user-entity-to-engine": ("patch-operation-updateEntity.json", "user-command", "package-entity", "package-entity.user", "vendor.engine-entity"),
            "engine-entity-to-user": ("patch-source-default-engine.json", "default-engine", "package-entity", "package-entity.engine", "vendor.user-entity"),
            "user-relation-to-engine": ("patch-operation-updateRelation.json", "user-command", "package-relation", "package-relation.user", "vendor.engine-relation"),
            "engine-relation-to-user": ("patch-source-default-engine.json", "default-engine", "package-relation", "package-relation.engine", "vendor.user-relation"),
        }
        for name, (filename, source_kind, kind, subject_id, target_type) in bases.items():
            with self.subTest(name=name):
                document = json.loads((CONTRACTS / "fixtures/valid" / filename).read_text())
                document["source"] = {
                    "kind": source_kind,
                    "identity": "ipcraft.default-noc-engine" if source_kind == "default-engine" else "ipcraft.host",
                    "version": "1.0.0" if source_kind == "default-engine" else "1",
                    **({"bundleDigest": "sha256:" + "a" * 64} if source_kind == "default-engine" else {}),
                }
                if source_kind == "default-engine":
                    context = json.loads((CONTRACTS / "patch-validation-context-v1.json").read_text())
                    document["applicability"] = copy.deepcopy(context["authorityContexts"][0]["expectedApplicability"])
                else:
                    document.pop("applicability", None)
                relation = kind == "package-relation"
                document["operations"] = [{
                    "op": "updateRelation" if relation else "updateEntity",
                    "relationKind" if relation else "entityKind": kind,
                    "id": subject_id, "set": {"typeKey": target_type}, "unset": [],
                }]
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                    subject.Classification("core-semantic", "ownership", "patch.ownership_violation"),
                )
        create_fixtures = (
            "patch-package-ownership-user-entity.json", "patch-package-ownership-engine-entity.json",
            "patch-package-ownership-user-relation.json", "patch-package-ownership-engine-relation.json",
        )
        for filename in create_fixtures:
            with self.subTest(undeclared=filename):
                document = json.loads((CONTRACTS / "fixtures/valid" / filename).read_text())
                document["operations"][0]["value"]["typeKey"] = "vendor.undeclared"
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                    subject.Classification("core-semantic", "reference", "patch.unknown_reference"),
                )

    def test_reconcile_derivation_and_relation_declarations_are_executable(self) -> None:
        context = json.loads((CONTRACTS / "patch-validation-context-v1.json").read_text())
        self.assertTrue(context["packageRelationTypes"]["vendor.user-relation"]["unresolvedAllowed"])
        self.assertFalse(context["packageRelationTypes"]["vendor.user-resolved-only"]["unresolvedAllowed"])
        for filename in (
            "patch-source-application-reconcile.json",
            "patch-application-router-create-default-membership.json",
            "patch-application-router-delete-membership.json",
            "patch-application-attachment-unresolved.json",
            "patch-application-package-relation-unresolved.json",
        ):
            with self.subTest(filename=filename):
                document = json.loads((CONTRACTS / "fixtures/valid" / filename).read_text())
                derivation = document["operations"][0]["set"]["derivation"]
                self.assertGreater(derivation["derivedStateRevision"], document["applicability"]["baseDerivedStateRevision"])
                self.assertNotEqual(derivation["derivedStateDigest"], document["applicability"]["baseDerivedStateDigest"])
        forbidden = json.loads((CONTRACTS / "fixtures/valid/patch-operation-updateRelation.json").read_text())
        forbidden["operations"] = [{
            "op":"updateRelation", "relationKind":"package-relation", "id":"package-relation.resolved-only",
            "set":{"sources":[{"state":"unresolved","intendedSubject":{"kind":"router","ref":{"id":"router.0.0"}},"reasonCode":"relation.target_removed"}]}, "unset":[],
        }]
        self.assertEqual(
            subject.classify_document(CONTRACTS, "ipcraft.patch.v1", forbidden),
            subject.Classification("core-semantic", "patch-invariant", "patch.invariant_violation"),
        )

    def test_engine_migration_patch_is_exact_not_arbitrary_dependency_rewrite(self) -> None:
        migration = json.loads((CONTRACTS / "fixtures/valid/patch-source-application-migration.json").read_text())
        dependencies = migration["operations"][0]["set"]["dependencies"]
        current = next(item for item in json.loads((CONTRACTS / "patch-validation-context-v1.json").read_text())["dependencyLocks"] if item["kind"] == "default-engine")
        target = next(item for item in dependencies if item["kind"] == "default-engine")
        self.assertNotEqual(target["bundleManifestDigest"], current["bundleManifestDigest"])
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.patch.v1", migration).phase)
        mutated = copy.deepcopy(migration)
        next(item for item in mutated["operations"][0]["set"]["dependencies"] if item["kind"] == "generator-tool")["bundleManifestDigest"] = "sha256:" + "f" * 64
        self.assertEqual(
            subject.classify_document(CONTRACTS, "ipcraft.patch.v1", mutated),
            subject.Classification("core-semantic", "engine-migration-binding", "engine.migration_invalid"),
        )

    def test_application_domain_side_effects_are_exact_and_final_domain_invariants_hold(self) -> None:
        for filename in (
            "patch-application-router-create-default-membership.json",
            "patch-application-router-delete-membership.json",
            "patch-application-delete-emptied-nondefault-domain.json",
        ):
            with self.subTest(valid=filename):
                document = json.loads((CONTRACTS / "fixtures/valid" / filename).read_text())
                self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document).phase)
        base = json.loads((CONTRACTS / "fixtures/valid/patch-source-application-reconcile.json").read_text())
        attacks = {
            "rename-domain": {"op":"updateEntity", "entityKind":"domain", "id":"domain.power.default", "set":{"name":"Changed"}, "unset":[]},
            "delete-default": {"op":"deleteEntity", "entityKind":"domain", "id":"domain.power.default"},
            "create-second-default": {"op":"createEntity", "entityKind":"domain", "localRef":"application:000099", "value":{"typeKey":"power", "name":"Second", "isDefault":True, "config":{}}},
            "arbitrary-membership-move": {"op":"updateRelation", "relationKind":"domain-membership", "id":"membership.power.0.0", "set":{"routerRef":{"id":"router.0.1"}}, "unset":[]},
        }
        for name, operation in attacks.items():
            with self.subTest(name=name):
                document = copy.deepcopy(base)
                document["operations"].append(operation)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.patch.v1", document),
                    subject.Classification("core-semantic", "ownership", "patch.ownership_violation"),
                )

    def test_formal_history_replay_is_separate_exact_and_unforgeable(self) -> None:
        context = json.loads((CONTRACTS / "patch-validation-context-v1.json").read_text())
        records = context["formalHistoryRecords"]
        self.assertEqual({item["kind"] for item in records}, {"topology", "default-engine-migration"})
        for record in records:
            increment = record["revisions"]["replayIncrement"]
            undo_current = copy.deepcopy(record["revisions"]["committed"])
            undo_next = {member:undo_current[member] + increment[member] for member in increment}
            redo_current = copy.deepcopy(undo_next)
            redo_next = {member:redo_current[member] + increment[member] for member in increment}
            for direction in ("undo", "redo"):
                current_revisions = undo_current if direction == "undo" else redo_current
                next_revisions = undo_next if direction == "undo" else redo_next
                request = {
                    "historyTransactionId": record["historyTransactionId"],
                    "recordDigest": record["recordDigest"],
                    "direction": direction,
                    "transactionBodyDigest": record["inverseTransactionDigest" if direction == "undo" else "forwardTransactionDigest"],
                    "currentAuthoritativeDesignDigest": record["afterAuthoritativeDesignDigest" if direction == "undo" else "beforeAuthoritativeDesignDigest"],
                    "currentTopologyInputDigest": record["afterTopologyInputDigest" if direction == "undo" else "beforeTopologyInputDigest"],
                    "currentDerivedStateDigest": record["afterDerivedStateDigest" if direction == "undo" else "beforeDerivedStateDigest"],
                    "currentRevisions": copy.deepcopy(current_revisions),
                    "nextRevisions": copy.deepcopy(next_revisions),
                }
                with self.subTest(kind=record["kind"], direction=direction):
                    self.assertIsNone(subject.validate_history_replay_request(CONTRACTS, request))
                    plan = subject.history_replay_plan(CONTRACTS, request)
                    self.assertEqual(plan["transactionBody"], record["inverseTransactionBody" if direction == "undo" else "forwardTransactionBody"])
                    self.assertEqual(plan["nextRevisions"], next_revisions)
                mutations = {
                    "id": lambda d: d.__setitem__("historyTransactionId", "missing.history"),
                    "record": lambda d: d.__setitem__("recordDigest", "sha256:" + "f" * 64),
                    "body": lambda d: d.__setitem__("transactionBodyDigest", "sha256:" + "f" * 64),
                    "direction": lambda d: d.__setitem__("direction", "redo" if direction == "undo" else "undo"),
                    "before-state": lambda d: d.__setitem__("currentDerivedStateDigest", "sha256:" + "e" * 64),
                    "revisions": lambda d: d["nextRevisions"].__setitem__("derivedStateRevision", d["nextRevisions"]["derivedStateRevision"] + 1),
                    "current-revisions": lambda d: d["currentRevisions"].__setitem__("sessionRevision", d["currentRevisions"]["sessionRevision"] + 1),
                }
                for name, mutate in mutations.items():
                    with self.subTest(kind=record["kind"], direction=direction, mutation=name):
                        forged = copy.deepcopy(request)
                        mutate(forged)
                        self.assertEqual(subject.validate_history_replay_request(CONTRACTS, forged), ("history-replay", "patch.source_not_allowed"))
            repeated_undo_current = copy.deepcopy(redo_next)
            repeated_undo_next = {member:repeated_undo_current[member] + increment[member] for member in increment}
            repeated_undo = {
                "historyTransactionId":record["historyTransactionId"], "recordDigest":record["recordDigest"], "direction":"undo",
                "transactionBodyDigest":record["inverseTransactionDigest"],
                "currentAuthoritativeDesignDigest":record["afterAuthoritativeDesignDigest"],
                "currentTopologyInputDigest":record["afterTopologyInputDigest"],
                "currentDerivedStateDigest":record["afterDerivedStateDigest"],
                "currentRevisions":repeated_undo_current, "nextRevisions":repeated_undo_next,
            }
            self.assertIsNone(subject.validate_history_replay_request(CONTRACTS, repeated_undo))
        ordinary = json.loads((CONTRACTS / "fixtures/valid/patch-source-undo-redo.json").read_text())
        ordinary["operations"] = [{
            "op":"updateEntity", "entityKind":"topology", "id":"topology.mesh",
            "set":{"derivation":copy.deepcopy(context["entities"][-1]["value"]["derivation"])}, "unset":[],
        }]
        self.assertNotEqual(subject.classify_document(CONTRACTS, "ipcraft.patch.v1", ordinary).phase, None)

    def test_formal_history_record_tombstones_ids_and_symmetry_fail_closed(self) -> None:
        mutations = {
            "tombstone-value": lambda r: r["tombstones"][0]["value"].__setitem__("corrupt", True),
            "duplicate-host-id": lambda r: r["localRefToHostId"].__setitem__("authority:router.0.1", r["localRefToHostId"]["authority:slot.free"]),
            "inverse-body": lambda r: r["inverseTransactionBody"]["authorityOperations"][0]["value"].__setitem__("corrupt", True),
            "mapping-correspondence": lambda r: r["inverseTransactionBody"]["authorityOperations"][0].__setitem__("localRef", "authority:wrong"),
            "affected-snapshot": lambda r: r["affectedBeforeSubjects"][0]["value"].__setitem__("corrupt", True),
            "forward-subject": lambda r: r["forwardTransactionBody"]["authorityOperations"][0].__setitem__("id", "slot.missing"),
            "topology-intent": lambda r: r["forwardTransactionBody"]["topologyIntent"].__setitem__("rows", 99),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "contracts"
                subject.copy_contract_tree(CONTRACTS, root)
                context_path = root / "patch-validation-context-v1.json"
                context = json.loads(context_path.read_text())
                record = next(item for item in context["formalHistoryRecords"] if item["kind"] == "topology")
                mutate(record)
                record["forwardTransactionDigest"] = subject._digest_json(record["forwardTransactionBody"])
                record["inverseTransactionDigest"] = subject._digest_json(record["inverseTransactionBody"])
                record["recordDigest"] = subject._digest_json({key:value for key,value in record.items() if key != "recordDigest"})
                context["contextDigest"] = subject._patch_context_digest(context)
                context_path.write_text(json.dumps(context))
                previous = subject.FROZEN_PATCH_CONTEXT_DIGEST
                subject.FROZEN_PATCH_CONTEXT_DIGEST = context["contextDigest"]
                try:
                    with self.assertRaises(subject.FixtureVerificationError):
                        subject._validate_patch_context(root, subject.Draft202012Subset(root))
                finally:
                    subject.FROZEN_PATCH_CONTEXT_DIGEST = previous
        migration_mutations = {
            "authority-operation": lambda r: r["forwardTransactionBody"]["authorityOperations"].append({"op":"deleteEntity","entityKind":"router","id":"router.0.0"}),
            "application-operation": lambda r: r["forwardTransactionBody"]["applicationOperations"].append({"op":"updateEntity","entityKind":"project","id":"project.mesh","set":{"name":"Wrong"},"unset":[]}),
            "affected-id": lambda r: r["affectedAfterSubjects"][0].__setitem__("id", "project.wrong"),
        }
        for name, mutate in migration_mutations.items():
            with self.subTest(migration=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "contracts"
                subject.copy_contract_tree(CONTRACTS, root)
                context_path = root / "patch-validation-context-v1.json"
                context = json.loads(context_path.read_text())
                record = next(item for item in context["formalHistoryRecords"] if item["kind"] == "default-engine-migration")
                mutate(record)
                record["forwardTransactionDigest"] = subject._digest_json(record["forwardTransactionBody"])
                record["inverseTransactionDigest"] = subject._digest_json(record["inverseTransactionBody"])
                record["recordDigest"] = subject._digest_json({key:value for key,value in record.items() if key != "recordDigest"})
                context["contextDigest"] = subject._patch_context_digest(context)
                context_path.write_text(json.dumps(context))
                previous = subject.FROZEN_PATCH_CONTEXT_DIGEST
                subject.FROZEN_PATCH_CONTEXT_DIGEST = context["contextDigest"]
                try:
                    with self.assertRaises(subject.FixtureVerificationError):
                        subject._validate_patch_context(root, subject.Draft202012Subset(root))
                finally:
                    subject.FROZEN_PATCH_CONTEXT_DIGEST = previous

    def test_bundle_manifest_digest_is_set_order_independent(self) -> None:
        document = json.loads((CONTRACTS / "fixtures/valid/bundle-manifest-maximum.json").read_text())
        reversed_document = copy.deepcopy(document)
        reversed_document["files"].reverse()
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.bundle-manifest.v1", document).phase)
        self.assertIsNone(subject.classify_document(CONTRACTS, "ipcraft.bundle-manifest.v1", reversed_document).phase)
        mutations = {
            "bundle-id": lambda d: d.__setitem__("bundleId", "changed"),
            "bundle-version": lambda d: d.__setitem__("bundleVersion", "changed"),
            "path": lambda d: d["files"][0].__setitem__("path", "bin/changed"),
            "size": lambda d: d["files"][0].__setitem__("size", d["files"][0]["size"] + 1),
            "digest": lambda d: d["files"][0].__setitem__("digest", "sha256:" + "f" * 64),
            "executable": lambda d: d["files"][0].__setitem__("executable", not d["files"][0]["executable"]),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                changed = copy.deepcopy(document)
                mutate(changed)
                self.assertEqual(
                    subject.classify_document(CONTRACTS, "ipcraft.bundle-manifest.v1", changed),
                    subject.Classification("core-semantic", "bundle-manifest", "dependency.manifest_invalid"),
                )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            schema_path = root / "schemas/ipcraft.bundle-manifest.v1.schema.json"
            schema = json.loads(schema_path.read_text())
            schema["properties"]["files"]["x-ipcraft-canonical"]["sortKey"] = ["size"]
            schema_path.write_text(json.dumps(schema))
            with self.assertRaisesRegex(subject.FixtureVerificationError, "Bundle files canonical set rule drifted"):
                subject.verify_all(root)

    def test_coverage_requirements_are_frozen_exactly(self) -> None:
        mutations = {
            "delete-root": lambda d: d["requirements"].pop("ipcraft.project-design.v1"),
            "delete-tier": lambda d: d["requirements"]["ipcraft.project-design.v1"].pop("maximumShape"),
            "delete-predicate": lambda d: d["requirements"]["ipcraft.project-design.v1"]["maximumShape"].pop("discriminatorCoverage"),
            "swap-root": lambda d: d["requirements"].__setitem__("ipcraft.project-design.v1", copy.deepcopy(d["requirements"]["ipcraft.noc-package.v1"])),
            "swap-tier-fixtures": lambda d: (
                d["roots"]["ipcraft.command-result.v1"].__setitem__("minimal", copy.deepcopy(d["roots"]["ipcraft.command-result.v1"]["representative"])),
                d["roots"]["ipcraft.command-result.v1"].__setitem__("representative", copy.deepcopy(d["roots"]["ipcraft.command-result.v1"]["minimal"])),
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "contracts"
                subject.copy_contract_tree(CONTRACTS, root)
                coverage = json.loads((root / "fixture-coverage-v1.json").read_text())
                mutate(coverage)
                coverage["requirementsDigest"] = "sha256:" + hashlib.sha256(
                    json.dumps(coverage["requirements"], ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
                ).hexdigest()
                (root / "fixture-coverage-v1.json").write_text(json.dumps(coverage))
                with self.assertRaises(subject.FixtureVerificationError):
                    subject.verify_all(root)

    def test_patch_context_integrity_is_frozen(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "contracts"
            subject.copy_contract_tree(CONTRACTS, root)
            path = root / "patch-validation-context-v1.json"
            context = json.loads(path.read_text())
            context["entities"][0]["value"]["label"] = "Changed"
            projected = copy.deepcopy(context)
            projected.pop("contextDigest")
            context["contextDigest"] = "sha256:" + hashlib.sha256(
                json.dumps(projected, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
            ).hexdigest()
            path.write_text(json.dumps(context))
            with self.assertRaisesRegex(subject.FixtureVerificationError, "context digest differs from frozen"):
                subject.verify_all(root)

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
                for path in [CONTRACTS / "fixture-catalog.json", CONTRACTS / "fixture-coverage-v1.json", CONTRACTS / "patch-validation-context-v1.json", *sorted((CONTRACTS / "fixtures").rglob("*.json"))]
            }
            actual = {
                path.relative_to(root).as_posix(): path.read_bytes()
                for path in [root / "fixture-catalog.json", root / "fixture-coverage-v1.json", root / "patch-validation-context-v1.json", *sorted((root / "fixtures").rglob("*.json"))]
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
