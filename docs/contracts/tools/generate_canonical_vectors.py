#!/usr/bin/env python3
"""NON-NORMATIVE deterministic authoring generator for canonical vectors.

The schemas, Appendix F, and committed vector artifacts are normative. This
stdlib-only script is authoring support and does not define canonical behavior.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import random
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
CONTRACTS = ROOT / 'docs/contracts'
SCHEMAS = CONTRACTS / 'schemas'
VECTORS = CONTRACTS / 'vectors'
INDEX_PATH = VECTORS / 'core-canonical-projection-v1.json'
DIGEST_A = 'sha256:' + 'a' * 64
DIGEST_B = 'sha256:' + 'b' * 64
DIGEST_C = 'sha256:' + 'c' * 64
ORDER_VALUES = ['alpha', 'mu', 'zeta']


def canonical_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))


def digest(value: Any) -> str:
    return 'sha256:' + hashlib.sha256(canonical_json(value).encode('utf-8')).hexdigest()


def pointer(document: Any, value: str) -> Any:
    current = document
    if value:
        for token in value[1:].split('/'):
            token = token.replace('~1', '/').replace('~0', '~')
            current = current[int(token)] if isinstance(current, list) else current[token]
    return current


class SchemaWorld:
    def __init__(self) -> None:
        catalog = json.loads((CONTRACTS / 'schema-catalog.json').read_text())
        self.docs = {}
        self.files = {}
        for entry in catalog['items']:
            doc = json.loads((CONTRACTS / entry['path']).read_text())
            self.docs[entry['id']] = doc
            self.files[Path(entry['path']).name] = entry['id']

    def resolve(self, schema_id: str, node: Any) -> tuple[str, Any]:
        seen = set()
        while isinstance(node, dict) and '$ref' in node:
            ref = node['$ref']
            key = (schema_id, ref)
            if key in seen:
                raise RuntimeError(f'cyclic direct ref {key}')
            seen.add(key)
            file_part, _, fragment = ref.partition('#')
            if file_part:
                schema_id = self.files[Path(file_part).name]
            node = pointer(self.docs[schema_id], fragment)
        return schema_id, node

    def matches(self, value: Any, schema_id: str, schema: Any) -> bool:
        schema_id, schema = self.resolve(schema_id, schema)
        if not isinstance(schema, dict):
            return True
        if 'const' in schema and value != schema['const']:
            return False
        if 'enum' in schema and value not in schema['enum']:
            return False
        if isinstance(value, dict):
            for key in schema.get('required', []):
                if key not in value:
                    return False
            for key, subschema in schema.get('properties', {}).items():
                if key in value and not self.matches(value[key], schema_id, subschema):
                    return False
        return True

    def sample(self, schema_id: str, schema: Any, seed: int = 0, depth: int = 0) -> Any:
        if depth > 24:
            return {}
        schema_id, schema = self.resolve(schema_id, schema)
        if not isinstance(schema, dict):
            return None
        if 'const' in schema:
            return copy.deepcopy(schema['const'])
        if 'enum' in schema:
            values = schema['enum']
            return copy.deepcopy(values[seed % len(values)])
        if 'oneOf' in schema:
            return self.sample(schema_id, schema['oneOf'][seed % len(schema['oneOf'])], seed, depth + 1)
        if 'anyOf' in schema:
            return self.sample(schema_id, schema['anyOf'][seed % len(schema['anyOf'])], seed, depth + 1)
        typ = schema.get('type')
        if isinstance(typ, list):
            typ = next((x for x in typ if x != 'null' and x != 'number'), typ[0])
        if typ == 'object' or 'properties' in schema:
            out = {}
            for key in schema.get('required', []):
                out[key] = self.sample(schema_id, schema.get('properties', {}).get(key, {}), seed, depth + 1)
            for branch in schema.get('allOf', []):
                if not isinstance(branch, dict):
                    continue
                if 'if' in branch and self.matches(out, schema_id, branch['if']):
                    then = branch.get('then', {})
                    if isinstance(then, dict):
                        for key in then.get('required', []):
                            out[key] = self.sample(schema_id, then.get('properties', {}).get(key, schema.get('properties', {}).get(key, {})), seed, depth + 1)
                        for key, subschema in then.get('properties', {}).items():
                            if key in out:
                                out[key] = self.sample(schema_id, subschema, seed, depth + 1)
            return out
        if typ == 'array':
            count = max(1, schema.get('minItems', 0))
            if 'maxItems' in schema:
                count = min(count, schema['maxItems'])
            values = [self.sample(schema_id, schema.get('items', {}), seed + i, depth + 1) for i in range(count)]
            return self.normalize(schema_id, schema, values)
        if typ == 'boolean':
            return bool(seed % 2)
        if typ == 'integer':
            return max(int(schema.get('minimum', 0)), seed + 1)
        if typ == 'number':
            return max(int(schema.get('minimum', 0)), seed + 1)
        if typ == 'null':
            return None
        pattern = schema.get('pattern', '')
        if 'sha256:' in pattern:
            return [DIGEST_A, DIGEST_B, DIGEST_C][seed % 3]
        if 'authority:' in pattern or 'application:' in pattern:
            return f'authority:item-{seed + 1}'
        if 'path' in schema.get('$comment', '').lower() or 'portable' in schema.get('$comment', '').lower():
            return f'path/item-{seed + 1}.json'
        return f'{ORDER_VALUES[seed % 3]}-{seed + 1}'

    def normalize(self, schema_id: str, schema: Any, value: Any) -> Any:
        schema_id, schema = self.resolve(schema_id, schema)
        if not isinstance(schema, dict):
            return copy.deepcopy(value)
        if 'oneOf' in schema:
            for branch in schema['oneOf']:
                if self.matches(value, schema_id, branch):
                    return self.normalize(schema_id, branch, value)
            return copy.deepcopy(value)
        if 'anyOf' in schema:
            for branch in schema['anyOf']:
                if self.matches(value, schema_id, branch):
                    return self.normalize(schema_id, branch, value)
            return copy.deepcopy(value)
        if isinstance(value, dict):
            properties = schema.get('properties', {})
            out = {key: self.normalize(schema_id, properties.get(key, {}), child) for key, child in value.items()}
            if schema_id == 'ipcraft.core-canonical-models.v1' and schema is self.docs[schema_id]['$defs'].get('patchStructuralLinkValue'):
                a = object_ref_token(out['endpointA'])
                b = object_ref_token(out['endpointB'])
                if b < a:
                    out['endpointA'], out['endpointB'] = out['endpointB'], out['endpointA']
            return out
        if isinstance(value, list):
            item_schema = schema.get('items', {})
            normalized = [self.normalize(schema_id, item_schema, item) for item in value]
            metadata = schema.get('x-ipcraft-canonical')
            if metadata and metadata['kind'] == 'set':
                normalized.sort(key=lambda item: sort_tuple(metadata['sortKey'], item))
            return normalized
        return copy.deepcopy(value)


def object_ref_token(ref: dict[str, Any]) -> str:
    return 'id:' + ref['id'] if 'id' in ref else 'localRef:' + ref['localRef']


def endpoint_key(item: dict[str, Any], patch: bool) -> tuple[Any, ...]:
    if item['state'] == 'resolved':
        subject = item['subject']
        token = object_ref_token(subject['ref']) if patch else 'id:' + subject['id']
        return (0, subject['kind'], token)
    subject = item['intendedSubject']
    token = object_ref_token(subject['ref']) if patch else 'id:' + subject['id']
    return (1, subject['kind'], token, item['reasonCode'])


def sort_tuple(keys: list[str], item: Any) -> tuple[Any, ...]:
    result = []
    for key in keys:
        if key == 'unicodeScalarValue':
            result.append(item)
        elif key == 'canonicalJson':
            result.append(canonical_json(item).encode('utf-8'))
        elif key == 'subjectsCanonicalJson':
            result.append(canonical_json(item['subjects']).encode('utf-8'))
        elif key == 'detailsCanonicalJson':
            result.append(canonical_json(item['details']).encode('utf-8'))
        elif key == 'persistedEndpointCanonicalKey':
            result.extend(endpoint_key(item, False))
        elif key == 'patchEndpointCanonicalKey':
            result.extend(endpoint_key(item, True))
        else:
            result.append(item[key])
    return tuple(result)


def set_keyed_sample(world: SchemaWorld, schema_id: str, node: dict[str, Any], index: int) -> Any:
    item_schema = node.get('items', {})
    value = world.sample(schema_id, item_schema, index)
    keys = node['x-ipcraft-canonical']['sortKey']
    label = ORDER_VALUES[index]
    if keys == ['unicodeScalarValue']:
        try:
            _, resolved_item = world.resolve(schema_id, item_schema)
            if isinstance(resolved_item, dict) and len(resolved_item.get('enum', [])) >= 3:
                return sorted(resolved_item['enum'])[index]
        except Exception:
            pass
        return label
    if keys == ['canonicalJson']:
        _, resolved_item = world.resolve(schema_id, item_schema)
        if isinstance(resolved_item, dict) and isinstance(resolved_item.get('type'), list):
            return [False, 2, 'zeta'][index]
        return value
    if keys == ['persistedEndpointCanonicalKey']:
        return {
            'state': 'resolved',
            'subject': {'kind': ['component', 'router', 'interface'][index], 'id': label},
        }
    if keys == ['patchEndpointCanonicalKey']:
        return {
            'state': 'resolved',
            'subject': {'kind': ['component', 'router', 'interface'][index], 'ref': {'id': label}},
        }
    if not isinstance(value, dict):
        value = {}
    for key in keys:
        if key in {'subjectsCanonicalJson', 'detailsCanonicalJson'}:
            continue
        prop = item_schema
        try:
            _, resolved = world.resolve(schema_id, item_schema)
            prop = resolved.get('properties', {}).get(key, {}) if isinstance(resolved, dict) else {}
            _, resolved_prop = world.resolve(schema_id, prop)
        except Exception:
            resolved_prop = {}
        if key == 'dataLoss':
            value[key] = bool(index % 2)
        elif isinstance(resolved_prop, dict) and 'enum' in resolved_prop:
            value[key] = resolved_prop['enum'][index % len(resolved_prop['enum'])]
        elif isinstance(resolved_prop, dict) and resolved_prop.get('type') == 'boolean':
            value[key] = bool(index % 2)
        elif 'digest' in key.lower():
            value[key] = [DIGEST_A, DIGEST_B, DIGEST_C][index]
        elif schema_id == 'ipcraft.fixture-catalog.v1' and key == 'path':
            value[key] = f'fixtures/valid/{label}.json'
        else:
            value[key] = label
    if keys == ['code', 'severity', 'dataLoss', 'subjectsCanonicalJson', 'detailsCanonicalJson', 'resolution']:
        code, severity, data_loss, resolution = [
            ('attachment.target_removed', 'warning', False, 'reattach-or-detach'),
            ('domain.non_default_deleted', 'warning', True, 'confirm-or-discard'),
            ('package_relation.endpoint_blocks_candidate', 'error', False, 'discard-and-repair'),
        ][index]
        value.update({'code': code, 'severity': severity, 'dataLoss': data_loss,
                      'subjects': [{'kind': 'router', 'id': label}],
                      'details': {'rank': index + 1}, 'resolution': resolution})
    if schema_id == 'ipcraft.fixture-catalog.v1':
        value.update({
            'validationPhase': 'schema',
            'failureBoundary': None,
            'expected': 'accept',
            'errorCode': None,
            'behaviorEvidence': None,
        })
    return value


def comparison_value(world: SchemaWorld, schema_id: str, item_schema: Any, key: str, high: bool) -> Any:
    _, resolved = world.resolve(schema_id, item_schema)
    prop = resolved.get('properties', {}).get(key, {}) if isinstance(resolved, dict) else {}
    _, prop = world.resolve(schema_id, prop)
    if key == 'dataLoss' or (isinstance(prop, dict) and prop.get('type') == 'boolean'):
        return high
    if isinstance(prop, dict) and 'enum' in prop:
        values = sorted(prop['enum'])
        return values[-1] if high else values[0]
    if 'digest' in key.lower():
        return DIGEST_B if high else DIGEST_A
    return 'zeta' if high else 'alpha'


def context_value(world: SchemaWorld, schema_id: str, item_schema: Any, key: str, component_index: int) -> Any:
    _, resolved = world.resolve(schema_id, item_schema)
    prop = resolved.get('properties', {}).get(key, {}) if isinstance(resolved, dict) else {}
    _, prop = world.resolve(schema_id, prop)
    if isinstance(prop, dict) and 'enum' in prop:
        return sorted(prop['enum'])[0]
    if isinstance(prop, dict) and prop.get('type') == 'boolean':
        return False
    if 'digest' in key.lower():
        return DIGEST_C
    return f'context-{component_index}'


def impact_component_samples() -> list[dict[str, Any]]:
    dispositions = [
        ('attachment.target_removed', 'warning', False, 'reattach-or-detach'),
        ('domain.non_default_deleted', 'warning', True, 'confirm-or-discard'),
        ('domain.disconnected', 'error', False, 'repair-domain'),
        ('package_relation.endpoint_unresolved', 'warning', False, 'reattach-or-delete-relation'),
        ('package_relation.endpoint_blocks_candidate', 'error', False, 'discard-and-repair'),
        ('engine_migration.dependency_replaced', 'warning', False, 'confirm-or-discard'),
    ]
    values = [
        {'code': code, 'severity': severity, 'dataLoss': data_loss,
         'subjects': [{'kind': 'router', 'id': f'router.{index:02d}'}],
         'details': {'rank': index + 1}, 'resolution': resolution}
        for index, (code, severity, data_loss, resolution) in enumerate(dispositions)
    ]
    values.extend([
        {'code': 'attachment.target_removed', 'severity': 'warning', 'dataLoss': False,
         'subjects': [{'kind': 'router', 'id': 'router.zeta'}],
         'details': {'rank': 1}, 'resolution': 'reattach-or-detach'},
        {'code': 'attachment.target_removed', 'severity': 'warning', 'dataLoss': False,
         'subjects': [{'kind': 'router', 'id': 'router.zeta'}],
         'details': {'rank': 2}, 'resolution': 'reattach-or-detach'},
    ])
    return values


def persisted_endpoint_samples() -> list[dict[str, Any]]:
    return [
        {'state':'resolved','subject':{'kind':'component','id':'alpha'}},
        {'state':'resolved','subject':{'kind':'component','id':'zeta'}},
        {'state':'resolved','subject':{'kind':'interface','id':'alpha'}},
        {'state':'unresolved','intendedSubject':{'kind':'component','id':'alpha'},'reasonCode':'reason.alpha'},
        {'state':'unresolved','intendedSubject':{'kind':'component','id':'alpha'},'reasonCode':'reason.zeta'},
        {'state':'unresolved','intendedSubject':{'kind':'component','id':'zeta'},'reasonCode':'reason.alpha'},
        {'state':'unresolved','intendedSubject':{'kind':'interface','id':'alpha'},'reasonCode':'reason.alpha'},
    ]


def patch_endpoint_samples() -> list[dict[str, Any]]:
    return [
        {'state':'resolved','subject':{'kind':'component','ref':{'id':'alpha'}}},
        {'state':'resolved','subject':{'kind':'component','ref':{'id':'zeta'}}},
        {'state':'resolved','subject':{'kind':'component','ref':{'localRef':'authority:alpha'}}},
        {'state':'resolved','subject':{'kind':'interface','ref':{'id':'alpha'}}},
        {'state':'unresolved','intendedSubject':{'kind':'component','ref':{'id':'alpha'}},'reasonCode':'reason.alpha'},
        {'state':'unresolved','intendedSubject':{'kind':'component','ref':{'id':'alpha'}},'reasonCode':'reason.zeta'},
        {'state':'unresolved','intendedSubject':{'kind':'component','ref':{'localRef':'authority:alpha'}},'reasonCode':'reason.alpha'},
        {'state':'unresolved','intendedSubject':{'kind':'interface','ref':{'id':'alpha'}},'reasonCode':'reason.alpha'},
    ]


def set_samples(world: SchemaWorld, schema_id: str, node: dict[str, Any]) -> list[Any]:
    keys = node['x-ipcraft-canonical']['sortKey']
    if keys == ['persistedEndpointCanonicalKey']:
        return persisted_endpoint_samples()
    if keys == ['patchEndpointCanonicalKey']:
        return patch_endpoint_samples()
    if keys == ['code', 'severity', 'dataLoss', 'subjectsCanonicalJson', 'detailsCanonicalJson', 'resolution']:
        return impact_component_samples()
    if len(keys) == 1:
        return [set_keyed_sample(world, schema_id, node, i) for i in range(3)]
    item_schema = node.get('items', {})
    values = []
    for component_index, component in enumerate(keys):
        for high in (False, True):
            value = set_keyed_sample(world, schema_id, node, component_index % 3)
            for previous_index, previous in enumerate(keys):
                if previous_index < component_index:
                    value[previous] = context_value(world, schema_id, item_schema, previous, component_index)
                elif previous_index == component_index:
                    value[previous] = comparison_value(world, schema_id, item_schema, previous, high)
                else:
                    value[previous] = context_value(world, schema_id, item_schema, previous, component_index)
            values.append(value)
    return values


def ordered_items(world: SchemaWorld, schema_id: str, node: dict[str, Any]) -> list[Any]:
    items = []
    for seed in range(2):
        item = world.sample(schema_id, node.get('items', {}), seed)
        if isinstance(item, dict):
            for key in ('id', 'stepId', 'draftId', 'code', 'ruleId'):
                if key in item:
                    item[key] = f'{key}-{seed + 1}'
                    break
        elif isinstance(item, str):
            item = f'ordered-{seed + 1}'
        items.append(item)
    if items[0] == items[1]:
        items = ['ordered-1', 'ordered-2']
    return items


def make_collection_catalog(world: SchemaWorld, index: dict[str, Any]) -> dict[str, Any]:
    cases = []
    for rule in index['canonicalCollections']:
        schema_id = rule['schemaId']
        node = pointer(world.docs[schema_id], rule['schemaPointer'])
        kind = rule['kind']
        case_id = (schema_id + rule['schemaPointer']).replace('ipcraft.', '').replace('.v1', '').replace('$defs/', '').replace('/properties/', '-').replace('/', '-').replace('$', '').strip('-').replace('.', '-')
        base = {
            'id': case_id,
            'schemaId': schema_id,
            'schemaPointer': rule['schemaPointer'],
            'collectionKind': kind,
        }
        if kind != 'ordered':
            base['sortKey'] = rule['sortKey']
        if kind == 'set':
            values = set_samples(world, schema_id, node)
            normalized = world.normalize(schema_id, node, values)
            reverse = list(reversed(normalized))
            shuffled = copy.deepcopy(normalized)
            random.Random(0x1C0A7).shuffle(shuffled)
            if shuffled in (normalized, reverse):
                shuffled = [normalized[1], normalized[2], normalized[0]]
            base.update({
                'inputVariants': [normalized, reverse, shuffled],
                'expectedRelation': 'equal',
                'expectedNormalized': normalized,
                'expectedCanonicalJson': canonical_json(normalized),
                'expectedDigest': digest(normalized),
            })
        elif kind == 'ordered':
            values = ordered_items(world, schema_id, node)
            variants = [values, list(reversed(values))]
            normalized = [world.normalize(schema_id, node, variant) for variant in variants]
            base.update({
                'inputVariants': variants,
                'expectedRelation': 'different',
                'expectedNormalized': normalized,
                'expectedCanonicalJson': [canonical_json(value) for value in normalized],
                'expectedDigest': [digest(value) for value in normalized],
            })
        else:
            valid = ['application:000001', 'authority:router-0', 'authority:router-1']
            invalid = [valid[1], valid[0], valid[2]]
            base.update({
                'inputVariants': [valid, invalid],
                'expectedRelation': 'invalid',
                'expectedNormalized': [valid, None],
                'expectedCanonicalJson': [canonical_json(valid), None],
                'expectedDigest': [digest(valid), None],
                'expectedErrorCode': [None, 'patch.invariant_violation'],
            })
        cases.append(base)
    return {
        'schema': 'ipcraft.canonical-vector-catalog.v1',
        'kind': 'collection-permutation',
        'canonicalization': 'RFC8785-after-Appendix-F-set-projection',
        'cases': cases,
    }


def applicability() -> dict[str, Any]:
    return {
        'schema': 'ipcraft.reconcile-applicability.v1', 'groupId': 'group-1', 'requestGeneration': 1,
        'topologyInputRevision': 2, 'topologyInputDigest': DIGEST_A, 'baseDerivedStateRevision': 3,
        'baseDerivedStateDigest': DIGEST_B, 'baseAuthoritativeDesignDigest': DIGEST_C,
        'structureAuthority': {'kind': 'default-engine', 'lockId': 'dep.engine', 'identity': 'ipcraft.default-noc-engine', 'version': '1', 'bundleDigest': DIGEST_A},
        'packageBundleDigest': DIGEST_B, 'reconcileDependencySetDigest': DIGEST_C,
        'defaultEngineLockId': 'dep.engine', 'defaultEngineBundleDigest': DIGEST_A,
        'engineHostContractVersion': 'ipcraft.engine-host.v1', 'hostSideEffectContractVersion': 'ipcraft.noc-side-effects.v1',
    }


def router_create(local_ref: str = 'authority:router-0') -> dict[str, Any]:
    return {'op': 'createEntity', 'entityKind': 'router', 'localRef': local_ref, 'value': {'templateKey': 'mesh-router', 'identityCompatibilityVersion': 1, 'coordinate': {'row': 0, 'column': 0}, 'properties': {}}}


def membership_create(local_ref: str = 'application:000001', router_ref: str = 'authority:router-0') -> dict[str, Any]:
    return {'op': 'createRelation', 'relationKind': 'domain-membership', 'localRef': local_ref, 'value': {'domainRef': {'id': 'domain.default'}, 'routerRef': {'localRef': router_ref}}}


def base_candidate() -> dict[str, Any]:
    return {
        'schema': 'ipcraft.candidate-transaction.v1', 'transactionId': 'tx-1', 'kind': 'topology-materialization',
        'applicability': applicability(),
        'topologyIntent': {'schema': 'ipcraft.topology-intent.v1', 'componentId': 'component.noc', 'topologyId': 'topology.main', 'topologyKind': 'mesh', 'globalConfig': {'columns': 2, 'rows': 2}, 'packageEntities': [], 'packageRelations': []},
        'authorityPatch': {'patchId': 'patch.authority', 'source': {'kind': 'default-engine', 'identity': 'ipcraft.default-noc-engine', 'version': '1', 'bundleDigest': DIGEST_A}, 'operations': [router_create()]},
        'applicationPatch': {'patchId': 'patch.application', 'source': {'kind': 'application-reconcile', 'identity': 'host', 'version': '1'}, 'operations': [membership_create()]},
        'tombstones': [], 'allocationOrder': ['application:000001', 'authority:router-0'],
        'impactReport': {'schema': 'ipcraft.topology-impact-report.v1', 'impacts': []},
        'candidateDigest': DIGEST_A,
    }


def normalize_candidate(world: SchemaWorld, value: dict[str, Any]) -> dict[str, Any]:
    value = copy.deepcopy(value)
    if 'candidate' in value:
        value = value['candidate']
    for excluded in ('candidateDigest', 'finalHostIdMapping', 'publishedHostIds', 'impactPresentation', 'localizedMessages'):
        value.pop(excluded, None)
    schema = world.docs['ipcraft.core-canonical-models.v1']['$defs']['candidateTransaction']
    normalized = world.normalize('ipcraft.core-canonical-models.v1', schema, value)
    for patch_name in ('authorityPatch', 'applicationPatch'):
        for operation in normalized[patch_name]['operations']:
            if operation.get('op') == 'createEntity' and operation.get('entityKind') == 'structural-link':
                link = operation['value']
                if object_ref_token(link['endpointB']) < object_ref_token(link['endpointA']):
                    link['endpointA'], link['endpointB'] = link['endpointB'], link['endpointA']
            if operation.get('op') == 'updateEntity':
                values = operation.get('set', {})
                if operation.get('entityKind') == 'access-slot' and 'allowedContracts' in values:
                    schema = world.docs['ipcraft.core-canonical-models.v1']['$defs']['accessSlot']['properties']['allowedContracts']
                    values['allowedContracts'] = world.normalize('ipcraft.core-canonical-models.v1', schema, values['allowedContracts'])
                extension_defs = {'component':'patchComponentValue', 'interface':'patchInterfaceValue', 'package-entity':'patchPackageEntityValue'}
                if operation.get('entityKind') in extension_defs and 'extensions' in values:
                    schema = world.docs['ipcraft.core-canonical-models.v1']['$defs'][extension_defs[operation['entityKind']]]['properties']['extensions']
                    values['extensions'] = world.normalize('ipcraft.core-canonical-models.v1', schema, values['extensions'])
            if operation.get('op') == 'updateRelation' and operation.get('relationKind') == 'package-relation':
                values = operation.get('set', {})
                properties = world.docs['ipcraft.core-canonical-models.v1']['$defs']['patchPackageRelationValue']['properties']
                for key in ('sources', 'targets', 'extensions'):
                    if key in values:
                        values[key] = world.normalize('ipcraft.core-canonical-models.v1', properties[key], values[key])
    return normalized


def candidate_case(world: SchemaWorld, case_id: str, variants: list[dict[str, Any]], relation: str,
                   included: list[str], excluded: list[str] | None = None, error: str | None = None) -> dict[str, Any]:
    out = {'id': case_id, 'inputVariants': variants, 'expectedRelation': relation,
           'includedProjection': included, 'excludedProjection': excluded or []}
    if relation == 'invalid':
        out['expectedErrorCode'] = error
        return out
    normalized = [normalize_candidate(world, value) for value in variants]
    canonical = [canonical_json(value) for value in normalized]
    digests = [digest(value) for value in normalized]
    if relation == 'equal':
        assert len(set(canonical)) == 1, case_id
        out.update({'expectedNormalized': normalized[0], 'expectedCanonicalJson': canonical[0], 'expectedDigest': digests[0]})
    else:
        assert len(set(canonical)) == len(canonical), case_id
        out.update({'expectedNormalized': normalized, 'expectedCanonicalJson': canonical, 'expectedDigest': digests})
    return out


def mutation_parent(document: Any, path: str) -> tuple[Any, str]:
    tokens = path[1:].split('/')
    current = document
    for raw in tokens[:-1]:
        token = raw.replace('~1', '/').replace('~0', '~')
        current = current[int(token)] if isinstance(current, list) else current[token]
    return current, tokens[-1].replace('~1', '/').replace('~0', '~')


def apply_mutation(baseline: dict[str, Any], mutation: dict[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(baseline)
    operation = mutation['operation']
    parent, token = mutation_parent(result, mutation['path'])
    if operation == 'remove':
        if isinstance(parent, list): del parent[int(token)]
        else: del parent[token]
    elif operation in {'replace', 'add'}:
        if isinstance(parent, list): parent[int(token)] = copy.deepcopy(mutation['value'])
        else: parent[token] = copy.deepcopy(mutation['value'])
    elif operation == 'append':
        target = parent[int(token)] if isinstance(parent, list) else parent[token]
        target.append(copy.deepcopy(mutation['value']))
    elif operation == 'rename-local-ref':
        old = parent[int(token)] if isinstance(parent, list) else parent[token]
        new = mutation['value']
        if isinstance(parent, list): parent[int(token)] = new
        else: parent[token] = new
        def rewrite(value: Any) -> None:
            if isinstance(value, dict):
                for key, child in value.items():
                    if key == 'localRef' and child == old: value[key] = new
                    else: rewrite(child)
            elif isinstance(value, list):
                for index, child in enumerate(value):
                    if child == old: value[index] = new
                    else: rewrite(child)
        rewrite(result)
        result['allocationOrder'] = sorted(set(result['allocationOrder']))
    else:
        raise ValueError(f'unsupported mutation operation {operation}')
    return result


def make_candidate_catalog(world: SchemaWorld) -> dict[str, Any]:
    base = base_candidate()
    cases = []
    cases.append(candidate_case(world, 'shared-authority-application-localref-namespace', [base], 'equal', ['authorityPatch.operations', 'applicationPatch.operations', 'allocationOrder']))

    a = {'candidate':copy.deepcopy(base),'hypotheticalFinalHostIdMapping':{'authority:router-0':'host.random-a','application:000001':'host.random-b'}}
    b = {'candidate':copy.deepcopy(base),'hypotheticalFinalHostIdMapping':{'authority:router-0':'host.other-a','application:000001':'host.other-b'}}
    cases.append(candidate_case(world, 'hypothetical-final-host-id-mappings-excluded', [a, b], 'equal', ['candidate.allocationOrder'], ['hypotheticalFinalHostIdMapping']))

    a = {'candidate':copy.deepcopy(base),'impactPresentation':{'locale':'en','message':'Router created'}}
    b = {'candidate':copy.deepcopy(base),'impactPresentation':{'locale':'zh-CN','message':'已创建路由器'}}
    cases.append(candidate_case(world, 'localized-impact-presentation-excluded', [a, b], 'equal', ['candidate.impactReport.impacts'], ['impactPresentation']))

    a = copy.deepcopy(base)
    b = json.loads(json.dumps(base, ensure_ascii=False, sort_keys=True))
    cases.append(candidate_case(world, 'object-key-order-permutation', [a, b], 'equal', ['entire candidate projection']))

    a, b = copy.deepcopy(base), copy.deepcopy(base)
    link = {'op': 'createEntity', 'entityKind': 'structural-link', 'localRef': 'authority:link-0', 'value': {'templateKey': 'mesh-link', 'identityCompatibilityVersion': 1, 'endpointA': {'localRef': 'authority:router-0'}, 'endpointB': {'id': 'router.existing'}, 'axis': 'horizontal', 'properties': {}}}
    a['authorityPatch']['operations'].append(link)
    b['authorityPatch']['operations'].append(copy.deepcopy(link))
    b['authorityPatch']['operations'][1]['value']['endpointA'], b['authorityPatch']['operations'][1]['value']['endpointB'] = b['authorityPatch']['operations'][1]['value']['endpointB'], b['authorityPatch']['operations'][1]['value']['endpointA']
    for value in (a, b): value['allocationOrder'] = ['application:000001', 'authority:link-0', 'authority:router-0']
    cases.append(candidate_case(world, 'undirected-link-endpoint-swap-normalizes-equally', [a, b], 'equal', ['authorityPatch.operations[].value.endpointA', 'authorityPatch.operations[].value.endpointB']))

    a, b = copy.deepcopy(base), copy.deepcopy(base)
    b['authorityPatch']['operations'][0]['localRef'] = 'authority:router-renamed'
    b['applicationPatch']['operations'][0]['value']['routerRef'] = {'localRef': 'authority:router-renamed'}
    b['allocationOrder'] = ['application:000001', 'authority:router-renamed']
    cases.append(candidate_case(world, 'localref-rename-with-edges-updated-changes-digest', [a, b], 'different', ['localRef graph', 'allocationOrder']))

    a, b = copy.deepcopy(base), copy.deepcopy(base)
    b['applicationPatch']['operations'][0]['value']['routerRef'] = {'id': 'router.other'}
    cases.append(candidate_case(world, 'localref-edge-change-changes-digest', [a, b], 'different', ['applicationPatch.operations[].value.routerRef']))

    a, b = copy.deepcopy(base), copy.deepcopy(base)
    second = {'op': 'deleteEntity', 'entityKind': 'router', 'id': 'router.old'}
    a['authorityPatch']['operations'].append(second)
    b['authorityPatch']['operations'] = list(reversed(a['authorityPatch']['operations']))
    cases.append(candidate_case(world, 'patch-operation-order-swap-changes-digest', [a, b], 'different', ['authorityPatch.operations']))

    pipeline_a = {'schema':'ipcraft.pipeline-plan.v1','pipelineRunId':'run-1','kind':'generate','snapshotSessionRevision':1,'snapshotDigest':DIGEST_A,'formallySavedProjectDigest':DIGEST_A,'dependencySetDigest':DIGEST_B,'defaultEngineBundleDigest':DIGEST_C,'engineHostContractVersion':'ipcraft.engine-host.v1','hostSideEffectContractVersion':'ipcraft.noc-side-effects.v1','steps':[{'stepId':'structural-drc','kind':'host'},{'stepId':'generator','kind':'external-tool','toolLockId':'dep.generator'}]}
    pipeline_b = copy.deepcopy(pipeline_a); pipeline_b['steps'] = list(reversed(pipeline_b['steps']))
    pipeline_schema = world.docs['ipcraft.core-canonical-models.v1']['$defs']['pipelinePlan']
    pipeline_normalized = [world.normalize('ipcraft.core-canonical-models.v1', pipeline_schema, x) for x in (pipeline_a,pipeline_b)]
    cases.append({'id':'pipeline-step-order-swap-changes-digest','modelSchema':'ipcraft.pipeline-plan.v1','inputVariants':[pipeline_a,pipeline_b],'expectedRelation':'different','includedProjection':['steps'],'excludedProjection':[],'expectedNormalized':pipeline_normalized,'expectedCanonicalJson':[canonical_json(x) for x in pipeline_normalized],'expectedDigest':[digest(x) for x in pipeline_normalized]})

    a, b = copy.deepcopy(base), copy.deepcopy(base)
    contracts = [
        {'contractLockId':'contract.z','roles':['target','requester'],'capabilityConstraints':{}},
        {'contractLockId':'contract.a','roles':['slave','master'],'capabilityConstraints':{}},
        {'contractLockId':'contract.m','roles':['observer','initiator'],'capabilityConstraints':{}},
    ]
    endpoints = [
        {'state':'resolved','subject':{'kind':'router','ref':{'id':'router.z'}}},
        {'state':'resolved','subject':{'kind':'interface','ref':{'id':'interface.a'}}},
        {'state':'resolved','subject':{'kind':'component','ref':{'id':'component.m'}}},
    ]
    extensions = [
        {'ownerLockId':'owner.z','schema':'ext.z','version':'1','data':{}},
        {'ownerLockId':'owner.a','schema':'ext.a','version':'1','data':{}},
        {'ownerLockId':'owner.m','schema':'ext.m','version':'1','data':{}},
    ]
    updates = [
        {'op':'updateEntity','entityKind':'access-slot','id':'slot.0','set':{'allowedContracts':contracts},'unset':[]},
        {'op':'updateEntity','entityKind':'component','id':'component.noc','set':{'extensions':extensions},'unset':[]},
        {'op':'updateRelation','relationKind':'package-relation','id':'relation.0','set':{'sources':endpoints,'targets':list(reversed(endpoints)),'extensions':list(reversed(extensions))},'unset':[]},
    ]
    a['authorityPatch']['operations'].extend(copy.deepcopy(updates)); b['authorityPatch']['operations'].extend(copy.deepcopy(updates))
    for operation in b['authorityPatch']['operations'][1:]:
        values = operation.get('set', {})
        for key in ('allowedContracts','extensions','sources','targets'):
            if key in values: values[key] = list(reversed(values[key]))
        for contract in values.get('allowedContracts', []): contract['roles'] = list(reversed(contract['roles']))
    cases.append(candidate_case(world, 'update-set-reused-collection-permutations', [a,b], 'equal', ['updateEntity.set.allowedContracts','updateEntity.set.allowedContracts[].roles','updateRelation.set.sources','updateRelation.set.targets','updateEntity/updateRelation.set.extensions']))

    for case_id, mutate, included in [
        ('applicability-change-changes-digest', lambda x: x['applicability'].__setitem__('requestGeneration', 2), ['applicability']),
        ('transaction-id-change-changes-digest', lambda x: x.__setitem__('transactionId', 'tx-2'), ['transactionId']),
        ('structured-impact-change-changes-digest', lambda x: x['impactReport']['impacts'].append({'code':'domain.non_default_deleted','severity':'warning','dataLoss':True,'subjects':[{'kind':'domain','id':'domain.old'}],'details':{'discardedConfig':True},'resolution':'confirm-or-discard'}), ['impactReport']),
    ]:
        a, b = copy.deepcopy(base), copy.deepcopy(base); mutate(b)
        cases.append(candidate_case(world, case_id, [a, b], 'different', included))

    invalids = [
        ('candidate-localref-collision', {'operation':'append','path':'/authorityPatch/operations','value':router_create('authority:router-0')}, 'patch.local_ref_invalid', 'candidate.localRef.unique'),
        ('authority-uses-application-prefix', {'operation':'rename-local-ref','path':'/authorityPatch/operations/0/localRef','value':'application:000009'}, 'patch.local_ref_invalid', 'candidate.localRef.authority-prefix'),
        ('application-uses-authority-prefix', {'operation':'rename-local-ref','path':'/applicationPatch/operations/0/localRef','value':'authority:membership-0'}, 'patch.local_ref_invalid', 'candidate.localRef.application-prefix'),
        ('allocation-order-missing', {'operation':'remove','path':'/allocationOrder'}, 'patch.schema_violation', 'candidate.required.allocationOrder'),
        ('allocation-order-duplicate', {'operation':'replace','path':'/allocationOrder','value':['application:000001','authority:router-0','authority:router-0']}, 'patch.schema_violation', 'candidate.allocationOrder.uniqueItems'),
        ('allocation-order-noncanonical', {'operation':'replace','path':'/allocationOrder','value':['authority:router-0','application:000001']}, 'patch.invariant_violation', 'candidate.allocationOrder.canonical'),
        ('final-host-id-mapping-injected', {'operation':'add','path':'/finalHostIdMapping','value':{'authority:router-0':'router.host'}}, 'patch.schema_violation', 'candidate.additionalProperties.finalHostIdMapping'),
        ('published-host-id-injected', {'operation':'add','path':'/publishedHostIds','value':['router.host']}, 'patch.schema_violation', 'candidate.additionalProperties.publishedHostIds'),
    ]
    for case_id, mutation, error, violated_rule in invalids:
        value = apply_mutation(base, mutation)
        cases.append({
            'id':case_id, 'baselineId':'shared-authority-application-localref-namespace',
            'mutation':mutation, 'inputVariants':[value], 'expectedRelation':'invalid',
            'includedProjection':['semantic validation'], 'excludedProjection':[],
            'expectedErrorCode':error, 'violatedRule':violated_rule,
        })

    return {'schema':'ipcraft.canonical-vector-catalog.v1','kind':'candidate-causality','canonicalization':'RFC8785-after-Appendix-F-set-projection','cases':cases}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir', type=Path, default=VECTORS)
    args = parser.parse_args()
    world = SchemaWorld()
    index = json.loads(INDEX_PATH.read_text())
    collections = make_collection_catalog(world, index)
    candidates = make_candidate_catalog(world)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / 'core-set-permutation-v1.json').write_text(json.dumps(collections, ensure_ascii=False, indent=2) + '\n')
    (args.output_dir / 'candidate-local-ref-v1.json').write_text(json.dumps(candidates, ensure_ascii=False, indent=2) + '\n')
    print(f"generated {len(collections['cases'])} collection cases and {len(candidates['cases'])} candidate cases")


if __name__ == '__main__':
    main()
