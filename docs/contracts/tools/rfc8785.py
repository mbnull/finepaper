"""Small, strict RFC 8785 (JSON Canonicalization Scheme) implementation.

The contract tools use one JSON domain: null, booleans, strings, arrays,
objects, and finite IEEE-754 binary64 numbers.  JSON integer tokens are kept
as Python ``int`` values for schema/type checks, but admission requires the
integer to round-trip through binary64 exactly.  This prevents an exact large
integer from silently changing value during canonicalization; callers should
use a string when the value is not an exact binary64 number.

Only the stdlib is used.  ``canonical_json`` returns text for existing tool
callers, while ``canonicalize`` returns the exact UTF-8 bytes used for a
digest.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any


class CanonicalizationError(ValueError):
    """Raised when a value is outside the strict RFC 8785 JSON domain."""


def _error(message: str) -> CanonicalizationError:
    return CanonicalizationError(message)


def _parse_int(token: str) -> int:
    """Admit an integer token only when its binary64 value is exact."""

    try:
        value = int(token, 10)
        as_float = float(value)
    except (OverflowError, ValueError) as exc:
        raise _error(f"integer is outside finite binary64 range: {token!r}") from exc
    if not math.isfinite(as_float) or int(as_float) != value:
        raise _error(
            f"integer is not represented exactly by binary64; use a JSON string: {token!r}"
        )
    return value


def _parse_float(token: str) -> float:
    try:
        value = float(token)
    except (OverflowError, ValueError) as exc:
        raise _error(f"invalid binary64 number: {token!r}") from exc
    if not math.isfinite(value):
        raise _error(f"number is not finite binary64: {token!r}")
    return value


def _reject_constant(token: str) -> Any:
    raise _error(f"non-JSON numeric constant is not admitted: {token}")


def _object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _error(f"duplicate JSON object member after decoding: {key!r}")
        result[key] = value
    return result


def _decode_input(document: str | bytes | bytearray) -> str:
    if isinstance(document, (bytes, bytearray)):
        try:
            return bytes(document).decode("utf-8", errors="strict")
        except UnicodeDecodeError as exc:
            raise _error(f"input is not strict UTF-8: {exc}") from exc
    if isinstance(document, str):
        return document
    raise _error(f"JSON input must be str or bytes, got {type(document).__name__}")


def _validate_string(value: str, location: str) -> None:
    # Python normally combines escaped surrogate pairs while parsing JSON,
    # but preserves an unpaired surrogate.  Reject both keys and values.
    if any(0xD800 <= ord(character) <= 0xDFFF for character in value):
        raise _error(f"{location}: string contains an unpaired UTF-16 surrogate")


def _validate_domain(value: Any, location: str = "JSON value") -> None:
    if value is None or isinstance(value, (bool, str)):
        if isinstance(value, str):
            _validate_string(value, location)
        return
    if isinstance(value, int) and not isinstance(value, bool):
        # Programmatically supplied ints must obey the same admission rule as
        # parse_int.  This also protects callers that bypass ``loads``.
        try:
            as_float = float(value)
        except (OverflowError, ValueError) as exc:
            raise _error(f"{location}: integer is outside finite binary64 range") from exc
        if not math.isfinite(as_float) or int(as_float) != value:
            raise _error(f"{location}: integer is not represented exactly by binary64")
        return
    if isinstance(value, float):
        if not math.isfinite(value):
            raise _error(f"{location}: number must be finite binary64")
        return
    if isinstance(value, list):
        for index, child in enumerate(value):
            _validate_domain(child, f"{location}[{index}]")
        return
    if isinstance(value, dict):
        for key, child in value.items():
            if not isinstance(key, str):
                raise _error(f"{location}: object member names must be strings")
            _validate_string(key, f"{location} key")
            _validate_domain(child, f"{location}.{key}")
        return
    raise _error(f"{location}: unsupported JSON value type {type(value).__name__}")


def loads(document: str | bytes | bytearray) -> Any:
    """Parse strict UTF-8 JSON with duplicate and numeric-domain rejection."""

    text = _decode_input(document)
    try:
        value = json.loads(
            text,
            object_pairs_hook=_object_pairs,
            parse_int=_parse_int,
            parse_float=_parse_float,
            parse_constant=_reject_constant,
        )
    except CanonicalizationError:
        raise
    except (UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        raise _error(f"invalid JSON: {exc}") from exc
    _validate_domain(value)
    return value


def load(path: str | Path) -> Any:
    """Read and parse a UTF-8 JSON file through :func:`loads`."""

    source = Path(path)
    try:
        data = source.read_bytes()
    except OSError as exc:
        raise _error(f"cannot read JSON {source}: {exc}") from exc
    try:
        return loads(data)
    except CanonicalizationError as exc:
        raise _error(f"{source}: {exc}") from exc


def _utf16_sort_key(value: str) -> bytes:
    _validate_string(value, "object member name")
    return value.encode("utf-16-be")


def _number_text(value: int | float) -> str:
    if isinstance(value, bool):  # bool is an int subclass
        raise _error("boolean is not a number")
    if isinstance(value, int):
        try:
            as_float = float(value)
        except (OverflowError, ValueError) as exc:
            raise _error("integer is outside finite binary64 range") from exc
        if not math.isfinite(as_float) or int(as_float) != value:
            raise _error("integer is not represented exactly by binary64")
        value = as_float
    elif not isinstance(value, float):
        raise _error(f"unsupported number type {type(value).__name__}")
    if not math.isfinite(value):
        raise _error("number must be finite binary64")
    if value == 0:
        return "0"

    # Python's repr is the shortest round-tripping binary64 spelling.  RFC
    # 8785 follows ECMAScript's fixed/exponential cutovers, so normalize the
    # spelling without changing its significant digits.
    token = repr(value).lower()
    sign = ""
    if token.startswith("-"):
        sign, token = "-", token[1:]
    mantissa, exponent_separator, exponent_text = token.partition("e")
    exponent = int(exponent_text) if exponent_separator else 0
    integer_part, dot, fractional_part = mantissa.partition(".")
    digits = integer_part + (fractional_part if dot else "")
    # repr emits a single leading zero for values below one.  Remove it when
    # computing the decimal position; retaining it would shift the exponent.
    leading = len(digits) - len(digits.lstrip("0"))
    digits = digits.lstrip("0")
    decimal_position = len(integer_part) + exponent - leading
    if not digits:
        return "0"
    # A trailing zero in the shortest ``x.0`` spelling is not significant.
    digits = digits.rstrip("0") or "0"
    adjusted_exponent = decimal_position - 1

    if -6 <= adjusted_exponent < 21:
        if decimal_position <= 0:
            body = "0." + ("0" * (-decimal_position)) + digits
        elif decimal_position >= len(digits):
            body = digits + ("0" * (decimal_position - len(digits)))
        else:
            body = digits[:decimal_position] + "." + digits[decimal_position:]
        return sign + body

    mantissa_text = digits[0]
    if len(digits) > 1:
        mantissa_text += "." + digits[1:]
    exponent_text = f"+{adjusted_exponent}" if adjusted_exponent >= 0 else str(adjusted_exponent)
    return sign + mantissa_text + "e" + exponent_text


def _serialize(value: Any, output: bytearray, location: str) -> None:
    if value is None:
        output.extend(b"null")
        return
    if value is True:
        output.extend(b"true")
        return
    if value is False:
        output.extend(b"false")
        return
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        output.extend(_number_text(value).encode("ascii"))
        return
    if isinstance(value, str):
        _validate_string(value, location)
        try:
            escaped = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
            output.extend(escaped.encode("utf-8"))
        except (UnicodeError, ValueError) as exc:
            raise _error(f"{location}: cannot encode JSON string: {exc}") from exc
        return
    if isinstance(value, list):
        output.extend(b"[")
        for index, child in enumerate(value):
            if index:
                output.extend(b",")
            _serialize(child, output, f"{location}[{index}]")
        output.extend(b"]")
        return
    if isinstance(value, dict):
        output.extend(b"{")
        keys = list(value)
        for key in keys:
            if not isinstance(key, str):
                raise _error(f"{location}: object member names must be strings")
            _validate_string(key, f"{location} key")
        keys.sort(key=_utf16_sort_key)
        for index, key in enumerate(keys):
            if index:
                output.extend(b",")
            _serialize(key, output, f"{location} key")
            output.extend(b":")
            _serialize(value[key], output, f"{location}.{key}")
        output.extend(b"}")
        return
    raise _error(f"{location}: unsupported JSON value type {type(value).__name__}")


def canonicalize(value: Any) -> bytes:
    """Return RFC 8785 canonical UTF-8 bytes for ``value``."""

    _validate_domain(value)
    output = bytearray()
    _serialize(value, output, "JSON value")
    return bytes(output)


def canonical_json(value: Any) -> str:
    """Return RFC 8785 canonical JSON text."""

    return canonicalize(value).decode("utf-8")


def sha256_digest(value: Any) -> str:
    """Return the contract ``sha256:`` digest of canonical bytes."""

    return "sha256:" + hashlib.sha256(canonicalize(value)).hexdigest()


__all__ = [
    "CanonicalizationError",
    "canonicalize",
    "canonical_json",
    "load",
    "loads",
    "sha256_digest",
]
