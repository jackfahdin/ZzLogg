#!/usr/bin/env python3
"""Offline independent fixtures; running C++ tests needs no Python/compressor."""
import bz2
import gzip
import hashlib
import json
import lzma
from pathlib import Path

root = Path(__file__).resolve().parent
line = '11-20 12:00:00.000  123  456 E Driver: i2c timeout 中文\r\n'.encode() + b'\x00tail\n'
size = 4 * 1024 * 1024 + 73
payloads = {'empty': b'', 'small': line,
            'large': (line * ((size + len(line) - 1) // len(line)))[:size]}
manifest = {}
for name, data in payloads.items():
    manifest[name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    for suffix, encoded in [('gz', gzip.compress(data, mtime=0)),
                            ('bz2', bz2.compress(data)), ('xz', lzma.compress(data))]:
        path = root / f'{name}.{suffix}'
        path.write_bytes(encoded)
        manifest[name][suffix] = hashlib.sha256(encoded).hexdigest()
(root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
