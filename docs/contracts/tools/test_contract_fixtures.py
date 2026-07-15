#!/usr/bin/env python3
"""Executable regression tests for the populated Gate 0 fixture set."""

from __future__ import annotations

import copy
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
