"""Prepare fixed local corpus snapshots for the optional search probe."""
from contextlib import contextmanager
import copy
import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile


def _identity(info):
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns


@contextmanager
def prepare_corpus(path):
    if path is None:
        yield None
        return
    path = path.resolve()
    if not path.is_file():
        raise ValueError('Manifest must be a regular file')
    raw = path.read_bytes()
    document = json.loads(raw)
    if (not isinstance(document, dict) or type(document.get('version')) is not int
            or document['version'] != 1 or set(document) != {'version', 'cases'}
            or not isinstance(document['cases'], list) or not document['cases']):
        raise ValueError('Expected corpus version 1 and nonempty cases')
    snapshot_document = copy.deepcopy(document)
    metadata = dict(manifest=str(path), manifest_sha256=hashlib.sha256(raw).hexdigest(),
                    queries=document, files=[], case_files={})
    with tempfile.TemporaryDirectory(prefix='zzlogg-corpus-') as temporary:
        root = Path(temporary)
        snapshots = {}
        for case in snapshot_document['cases']:
            if not isinstance(case, dict) or not isinstance(case.get('file'), str) or not case['file']:
                raise ValueError('Each corpus case needs a file path')
            source = (path.parent / case['file']).resolve()
            if source not in snapshots:
                if not source.is_file():
                    raise ValueError('Corpus input must be an existing regular file')
                before = source.stat()
                snapshot = root / f'input-{len(snapshots)}.log'
                digest = hashlib.sha256()
                with source.open('rb') as reader, snapshot.open('xb') as writer:
                    # Check the opened object too, rather than only the earlier path lookup.
                    opened = os.fstat(reader.fileno())
                    if not stat.S_ISREG(opened.st_mode) or _identity(opened) != _identity(before):
                        raise ValueError('Corpus input changed before copying')
                    while block := reader.read(1024 * 1024):
                        writer.write(block)
                        digest.update(block)
                    after = os.fstat(reader.fileno())
                if (_identity(before) != _identity(after)
                        or _identity(before) != _identity(source.stat())):
                    raise ValueError('Corpus input changed while copying; use a completed log')
                snapshots[source] = snapshot
                metadata['files'].append(dict(source=str(source), sha256=digest.hexdigest(), bytes=before.st_size))
            case['file'] = str(snapshots[source])
            info = next(item for item in metadata['files'] if item['source'] == str(source))
            if not isinstance(case.get('id'), str) or case['id'] in metadata['case_files']:
                raise ValueError('Invalid or duplicate corpus id')
            metadata['case_files'][case['id']] = dict(sha256=info['sha256'], bytes=info['bytes'])
        manifest = root / 'queries.json'
        manifest.write_text(json.dumps(snapshot_document, ensure_ascii=False), encoding='utf-8')
        yield dict(manifest=manifest, metadata=metadata)


def validate_output_path(manifest, output):
    """Refuse to overwrite an input through results/log files or symlink aliases."""
    if manifest is None:
        return
    manifest = manifest.resolve()
    output = output.resolve()
    if not manifest.is_file():
        raise ValueError('Manifest must be a regular file')
    document = json.loads(manifest.read_bytes())
    inputs = [manifest]
    if isinstance(document, dict) and isinstance(document.get('cases'), list):
        for case in document['cases']:
            if isinstance(case, dict) and isinstance(case.get('file'), str):
                inputs.append((manifest.parent / case['file']).resolve())
    if any(path.is_relative_to(output) for path in inputs):
        raise ValueError('Output directory must not contain the manifest or source logs')


def atomic_write_text(path, text):
    # Replacing the directory entry avoids following pre-existing symlinks or
    # truncating an inode shared with an input through a hard link.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix='.zzlogg-report-', delete=False) as writer:
            temporary = Path(writer.name)
            writer.write(text)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
