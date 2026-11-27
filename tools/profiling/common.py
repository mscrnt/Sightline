"""Offline validation and immutable output helpers for the profiling tools."""
import hashlib
import json
import math
from pathlib import Path


class ProfileError(ValueError):
    pass


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def pin(path):
    path = Path(path).resolve(strict=True)
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha256(path)}


def hex_address(value):
    """Address fields are always hexadecimal, with optional 0x prefix."""
    if not isinstance(value, str) or not value or value.strip() != value:
        raise ProfileError('address must be a hexadecimal string')
    digits = value[2:] if value.startswith('0x') else value
    if not digits or any(c not in '0123456789abcdefABCDEF' for c in digits):
        raise ProfileError('invalid hexadecimal address: ' + value)
    result = int(digits, 16)
    if result >= 2**64:
        raise ProfileError('address exceeds 64 bits')
    return result


def number(value, label, minimum=0):
    if isinstance(value, bool):
        raise ProfileError(label + ' must be finite numeric data')
    try:
        result = float(value)
    except (TypeError, ValueError):
        raise ProfileError(label + ' must be finite numeric data') from None
    if not math.isfinite(result) or result < minimum:
        raise ProfileError(label + ' is out of range')
    return result


def integer(value, label, minimum=0):
    try:
        result = int(value)
    except (TypeError, ValueError, OverflowError):
        raise ProfileError(label + ' must be an integer') from None
    if isinstance(value, bool) or str(value) != str(result):
        raise ProfileError(label + ' must be an integer')
    if result < minimum:
        raise ProfileError(label + ' is out of range')
    return result


def digest(value):
    if not isinstance(value, str) or len(value) != 64 or any(
            c not in '0123456789abcdef' for c in value):
        raise ProfileError('expected lowercase SHA-256')
    return value


def load_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ProfileError('duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8-sig'),
                      object_pairs_hook=unique,
                      parse_constant=lambda x: (_ for _ in ()).throw(
                          ProfileError('non-finite JSON: ' + x)))


def write_json(path, data):
    """Create once; never replace existing evidence, including symlinks."""
    text = json.dumps(data, indent=2, allow_nan=False) + '\n'
    with Path(path).open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(text)


def same_pin(expected):
    actual = pin(expected['path'])
    if actual != expected:
        raise ProfileError('input changed: ' + expected['path'])
