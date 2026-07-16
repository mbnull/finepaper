#!/usr/bin/env python3
"""Focused conformance vectors for the shared RFC 8785 canonicalizer."""

from __future__ import annotations

import json
import shutil
import subprocess
import unittest
from pathlib import Path

from rfc8785 import CanonicalizationError, canonical_json, loads


class Rfc8785CanonicalizationTests(unittest.TestCase):
    def test_committed_conformance_vector_file(self) -> None:
        vector_path = Path(__file__).resolve().parents[1] / "vectors/rfc8785-conformance-v1.json"
        document = json.loads(vector_path.read_text(encoding="utf-8"))
        self.assertEqual(document["schema"], "ipcraft.rfc8785-conformance.v1")
        for case in document["cases"]:
            with self.subTest(case=case["id"]):
                if "expectedError" in case:
                    with self.assertRaises(CanonicalizationError):
                        loads(case["input"])
                else:
                    self.assertEqual(canonical_json(loads(case["input"])), case["expected"])

    def test_ecmascript_number_boundaries(self) -> None:
        vectors = {
            1.0: "1",
            -0.0: "0",
            1e-7: "1e-7",
            1e-6: "0.000001",
            1e20: "100000000000000000000",
            1e21: "1e+21",
            1.2345678901234567: "1.2345678901234567",
            1000000000000000100.0: "1000000000000000100",
        }
        for value, expected in vectors.items():
            self.assertEqual(canonical_json(value), expected, repr(value))

    def test_numeric_semantic_spellings_canonicalize_identically(self) -> None:
        self.assertEqual(canonical_json(loads("1")), "1")
        self.assertEqual(canonical_json(loads("1.0")), "1")
        self.assertEqual(canonical_json(loads("1e0")), "1")

    def test_oversized_integer_that_is_not_exact_binary64_is_rejected(self) -> None:
        with self.assertRaises(CanonicalizationError):
            loads("9007199254740993")

    def test_utf16_property_order_precedes_unicode_scalar_order(self) -> None:
        value = {"\ue000": 1, "\U0001f600": 2}
        self.assertEqual(canonical_json(value), '{"😀":2,"":1}')

    def test_duplicate_members_are_rejected_after_escape_decoding(self) -> None:
        with self.assertRaises(CanonicalizationError):
            loads('{"a":1,"a":2}')
        with self.assertRaises(CanonicalizationError):
            loads('{"a":1,"\\u0061":2}')

    def test_lone_surrogates_are_rejected(self) -> None:
        with self.assertRaises(CanonicalizationError):
            loads('"\\ud800"')
        with self.assertRaises(CanonicalizationError):
            canonical_json("\udfff")
        with self.assertRaises(CanonicalizationError):
            canonical_json({"\ud800": 1})

    @unittest.skipUnless(shutil.which("node"), "Node.js is not installed")
    def test_number_vectors_match_node_json_stringify(self) -> None:
        vectors = [1.0, -0.0, 1e-7, 1e-6, 1e20, 1e21,
                   1.2345678901234567, 5e-324, 1.7976931348623157e308]
        script = "const values = JSON.parse(process.argv[1]); for (const value of values) console.log(JSON.stringify(value));"
        result = subprocess.run(
            ["node", "-e", script, json.dumps(vectors)],
            check=True,
            capture_output=True,
            text=True,
        )
        expected = result.stdout.splitlines()
        self.assertEqual([canonical_json(value) for value in vectors], expected)


if __name__ == "__main__":
    unittest.main()
