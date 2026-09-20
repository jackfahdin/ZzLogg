"""Read versioned release notes from the source commit's Markdown changelog."""
from pathlib import Path
import re


def release_changes(changelog, version, mode):
    if mode not in ('stable', 'nightly'):
        raise ValueError('Unsupported release mode')
    raw = Path(changelog).read_bytes()
    if len(raw) > 1024 * 1024:
        raise ValueError('Changelog exceeds 1 MiB')
    sections = {}
    current = None
    fence = None
    for line in raw.decode('utf-8-sig').splitlines():
        marker = re.match(r'^ {0,3}(`{3,}|~{3,})(.*)$', line)
        if fence:
            if current is not None:
                sections[current].append(line)
            if (marker and marker[1][0] == fence[0] and len(marker[1]) >= len(fence)
                    and not marker[2].strip()):
                fence = None
            continue
        if marker:
            fence = marker[1]
        elif line.startswith('## '):
            heading = re.fullmatch(r'## \[([^\]]+)\](?: - \d{4}-\d{2}-\d{2})?\s*', line)
            current = heading[1] if heading else None
            if current is not None:
                if current in sections:
                    raise ValueError('Duplicate changelog section')
                sections[current] = []
            continue
        if current is not None:
            sections[current].append(line)
    selected = []
    for key in (('Unreleased', version) if mode == 'nightly' else (version,)):
        body = '\n'.join(sections.get(key, [])).strip()
        if body:
            selected.append(f'## [{key}]\n\n{body}')
    if not selected:
        raise ValueError(f'Changelog has no release notes for {version} ({mode})')
    result = '\n\n'.join(selected)
    if len(result.encode('utf-8')) > 48 * 1024:
        raise ValueError('Selected release notes exceed 48 KiB')
    return result
