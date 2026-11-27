#!/usr/bin/env python3
"""Offline hostile-path fixtures. Tests extract only inside their own temporary trees."""
import hashlib
import io
import json
from pathlib import Path
import tarfile
import zipfile
root = Path(__file__).resolve().parent
cases = {
    'parent': [('file','../outside/marker.txt',b'changed')],
    'prefix-sibling': [('file','../result-evil/marker.txt',b'changed')],
    'backslash': [('file','..\\outside\\marker.txt',b'changed')],
    'external-link': [('link','alias.log',b'../outside/marker.txt')],
    'indirect-escape': [('link','pivot',b'.'), ('link','escape',b'pivot/../outside/marker.txt')],
    'case-link-escape': [('link','pivot',b'.'), ('link','escape',b'PiVoT/../outside/marker.txt')],
    'unicode-link-escape': [('link','caf\u00e9',b'.'),
                            ('link','escape','cafe\u0301/../outside/marker.txt'.encode())],
    'case-collision': [('file','logs/value.log',b'first'), ('file','logs/VALUE.log',b'second')],
    'unicode-collision': [('file','caf\u00e9.log',b'first'), ('file','cafe\u0301.log',b'second')],
    'internal-links': [('file','logs/value.log',b'value\n'), ('link','alias.log',b'logs/value.log'),
                       ('link','nested/link.log',b'../alias.log'), ('link','dir',b'logs'),
                       ('link','via-dir.log',b'dir/value.log')],
}
for name, members in cases.items():
    with tarfile.open(root/(name+'.tar'),'w',format=tarfile.USTAR_FORMAT) as archive:
        for kind, path, data in members:
            info=tarfile.TarInfo(path);info.mode=0o644
            if kind=='link':
                info.type=tarfile.SYMTYPE;info.linkname=data.decode();archive.addfile(info)
            else:
                info.size=len(data);archive.addfile(info,io.BytesIO(data))
    with zipfile.ZipFile(root/(name+'.zip'),'w') as archive:
        for kind,path,data in members:
            info=zipfile.ZipInfo(path,(2000,1,1,0,0,0));info.create_system=3
            info.external_attr=((0o120777 if kind=='link' else 0o100644)<<16)
            archive.writestr(info,data)
for name, field in [('negative-size',b'-0000000001\0'),('invalid-size',b'99999999999\0')]:
    header=bytearray(tarfile.TarInfo('value.log').tobuf(format=tarfile.USTAR_FORMAT))
    header[124:136]=field;header[148:156]=b' '*8
    header[148:156]=('%06o\0 ' % sum(header)).encode()
    (root/(name+'.tar')).write_bytes(header+b'\0'*1024)
files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.suffix in ('.tar','.zip')}
(root/'manifest.json').write_text(json.dumps(files,indent=2,sort_keys=True)+'\n')
