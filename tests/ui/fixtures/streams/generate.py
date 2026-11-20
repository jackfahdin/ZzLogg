#!/usr/bin/env python3
"""Offline independent stream fixtures; C++ tests consume committed files only."""
import bz2
import gzip
import hashlib
import json
import lzma
from pathlib import Path
import random

root = Path(__file__).resolve().parent
payloads = {'first': b'FIRST\n', 'second': b'SECOND\n',
            'large': random.Random(271828).randbytes(17000), 'boundary': b'A' * 16384}
for name, data in payloads.items():
    (root / (name + '.bin')).write_bytes(data)
rows = []
def add(name, suffix, data, parts, first_size=0):
    file = name + '.' + suffix
    (root / file).write_bytes(data)
    rows.append(dict(file=file, parts=parts, first_size=first_size,
                     sha256=hashlib.sha256(data).hexdigest()))

for suffix, encode in {'gz': lambda x: gzip.compress(x, mtime=0), 'bz2': bz2.compress,
                       'xz': lzma.compress}.items():
    first, second, empty = encode(payloads['first']), encode(payloads['second']), encode(b'')
    for name, data, parts in [
        ('two', first+second, ['first','second']),
        ('empty-first', empty+second, ['second']),
        ('empty-middle', first+empty+second, ['first','second']),
        ('empty-last', first+empty, ['first']),
        ('all-empty', empty+empty, []),
        ('large-first', encode(payloads['large'])+second, ['large','second']),
        ('output-boundary', encode(payloads['boundary'])+second, ['boundary','second']),
        ('truncated-second', first+second[:-5], None),
        ('corrupt-second', first+second[:-12]+bytes([second[-12]^0x40])+second[-11:], None),
        ('trailing-garbage', first+b'NOT-A-STREAM', None),
    ]:
        add(name, suffix, data, parts, len(first) if name == "two" else 0)
first, second = lzma.compress(payloads['first']), lzma.compress(payloads['second'])
for count in (1, 3, 4, 8, 8192, 8196):
    for middle in (False, True):
        valid = count % 4 == 0
        add(f'padding-{count}-'+('between' if middle else 'end'), 'xz',
            first+b'\0'*count+(second if middle else b''),
            (['first','second'] if middle else ['first']) if valid else None)
legacy = lzma.compress(payloads['first'], format=lzma.FORMAT_ALONE)
add('legacy', 'lzma', legacy, ['first'])
add('legacy-concatenated', 'lzma', legacy+legacy, None)
add('legacy-trailing', 'lzma', legacy+b'garbage', None)
add('legacy-truncated', 'lzma', legacy[:-5], None)
(root/'manifest.json').write_text(json.dumps(rows, indent=2)+'\n')
