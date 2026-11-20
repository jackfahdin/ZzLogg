#!/usr/bin/env python3
"""Offline fixtures: Python stdlib and 7-Zip; neither is needed by C++ tests."""
import bz2
import gzip
import hashlib
import io
import json
import lzma
from pathlib import Path
import subprocess
import tarfile
import tempfile
import zipfile

root = Path(__file__).resolve().parent
members = {'logs/dmesg.log': b'[1.000] i2c timeout\n\x00end\n',
           'logs/logcat.txt': b'11-20 E Driver: error\r\nlast',
           'empty.log': b'', '中文/应用.log': '驱动😀\n'.encode()}
manifest = {'files': {name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                               hex=data.hex()) for name, data in members.items()}, 'archives': {}}
for method, name in ((zipfile.ZIP_STORED, 'stored.zip'), (zipfile.ZIP_DEFLATED, 'deflated.zip')):
    with zipfile.ZipFile(root/name, 'w', compression=method) as archive:
        for path, data in members.items():
            info=zipfile.ZipInfo(path, (2000,1,1,0,0,0));info.compress_type=method
            archive.writestr(info, data)
tar = io.BytesIO()
with tarfile.open(fileobj=tar, mode='w', format=tarfile.GNU_FORMAT) as archive:
    for path, data in members.items():
        info=tarfile.TarInfo(path);info.size=len(data);info.mode=0o644
        archive.addfile(info, io.BytesIO(data))
data=tar.getvalue()
for name, encoded in [('logs.tar',data), ('logs.tar.gz',gzip.compress(data,mtime=0)),
                      ('logs.tar.bz2',bz2.compress(data)), ('logs.tar.xz',lzma.compress(data))]:
    (root/name).write_bytes(encoded)
with tempfile.TemporaryDirectory() as temporary:
    stage=Path(temporary)
    for name, data in members.items():
        path=stage/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    for solid in ('on','off'):
        path=root/f'solid-{solid}.7z';path.unlink(missing_ok=True)
        subprocess.run(['7z','a','-t7z','-m0=LZMA2',f'-ms={solid}','-mtc=off','-mta=off','-mtm=off',
                        str(path),*members],cwd=stage,stdout=subprocess.DEVNULL,check=True)
for name in ('stored.zip','deflated.zip','logs.tar','logs.tar.gz','logs.tar.bz2','logs.tar.xz','solid-on.7z','solid-off.7z'):
    manifest['archives'][name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
with zipfile.ZipFile(root/'portable.zip','w',compression=zipfile.ZIP_DEFLATED) as archive:
    files={'ZzLogg.exe':b'new executable','.zzlogg-portable':b'ZzLogg portable\n'}
    files['.zzlogg-portable-files.json']=json.dumps({'schema':1,'files':list(files)}).encode()
    for name,data in files.items():
        info=zipfile.ZipInfo('ZzLogg-1.2.3/'+name,(2000,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
        archive.writestr(info,data)
manifest['portable_sha256']=hashlib.sha256((root/'portable.zip').read_bytes()).hexdigest()
(root/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
