#!/usr/bin/env python3
"""Generate reproducible SYNTHETIC Android logs and independent query oracles."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import re
from collections import defaultdict
from datetime import datetime, timedelta


class Oracle:
    def __init__(self, predicate):
        self.predicate = predicate
        self.rows = self.matched = self.width = 0
        self.digest = hashlib.sha256()

    def add(self, line):
        # Only the isolated FF fixture needs replacement decoding. Qt strips
        # a leading UTF-8 BOM for matching; width still counts original bytes.
        text = line.decode('utf-8', errors='replace').removeprefix('\ufeff')
        if self.predicate(text):
            self.digest.update(f'{self.rows},'.encode())
            self.matched += 1
            self.width = max(self.width, len(line.expandtabs(8)))
        self.rows += 1

    def finish(self):
        digest = self.digest.copy()
        digest.update(f'{self.width}:{self.rows}'.encode())
        return dict(rows=self.rows, matched=self.matched, max_length=self.width,
                    result_sha256=digest.hexdigest())


def oracle(lines, predicate):
    state = Oracle(predicate)
    for line in lines:
        state.add(line)
    return state.finish()


def query_specs():
    queries = []
    def add(name, file, pattern, predicate, kind='literal', case=True, inverse=False):
        queries.append(dict(id=name, file=file + '.log', pattern=pattern, predicate=predicate,
                            kind=kind, case_sensitive=case, inverse=inverse))
    add('dmesg-probe', 'dmesg', 'probe failed', lambda s: 'probe failed' in s)
    add('dmesg-i2c', 'dmesg', '"i2c" and ("timeout" or "-110")',
        lambda s: 'i2c' in s and ('timeout' in s or '-110' in s), 'boolean-literal')
    add('dmesg-irq', 'dmesg', 'irq', lambda s: 'irq' in s)
    add('dmesg-miss', 'dmesg', 'zzlogg-unmatched-8ad0b5', lambda s: 'zzlogg-unmatched-8ad0b5' in s)
    errno = re.compile(r'\berror -(?:5|19|110)\b')
    add('dmesg-errno', 'dmesg', errno.pattern, lambda s: bool(errno.search(s)), 'regex')
    add('dmesg-casefold', 'dmesg', 'TIMEOUT', lambda s: 'timeout' in s.lower(), case=False)
    add('logcat-fatal', 'logcat', 'FATAL EXCEPTION', lambda s: 'FATAL EXCEPTION' in s)
    add('logcat-binder', 'logcat', '"binder" and not("debug")',
        lambda s: 'binder' in s and 'debug' not in s, 'boolean-literal')
    levels = re.compile(r'\s[EF]\s+(?:AndroidRuntime|CameraService)\s*:')
    add('logcat-errors', 'logcat', levels.pattern, lambda s: bool(levels.search(s)), 'regex')
    add('logcat-package', 'logcat', 'com.example.driverlab', lambda s: 'com.example.driverlab' in s)
    add('app-temperature', 'app', '温度', lambda s: '温度' in s)
    add('app-failure', 'app', '"Exception" or "SIGSEGV" or "ANR"',
        lambda s: any(x in s for x in ('Exception', 'SIGSEGV', 'ANR')), 'boolean-literal')
    add('app-payload', 'app', 'payload_end', lambda s: 'payload_end' in s)
    add('app-non-heartbeat', 'app', 'heartbeat', lambda s: 'heartbeat' not in s, inverse=True)
    add('boundary-marker', 'boundaries', 'EDGE', lambda s: 'EDGE' in s)
    add('boundary-replacement', 'boundaries', '�', lambda s: '�' in s)
    add('boundary-empty', 'boundaries', '', lambda s: True)
    return queries


def messages(kind, event, rng):
    """Adjacent lines belong to the same synthetic event, pid and tid."""
    if kind == 'dmesg':
        if event % 251 == 0:
            bus = rng.randrange(1, 8)
            return [('E', '', f'i2c i2c-{bus}: transfer timeout, addr=0x38 error -110'),
                    ('E', '', f'my_sensor {bus}-0038: probe failed with error -110')]
        if event % 509 == 0:
            return [('W', '', s) for s in ['WARNING: CPU: 3 PID: 420 at drivers/usb/core/hub.c:1234',
                'Modules linked in: my_sensor wlan', 'Call trace:',
                ' usb_submit_urb+0x88/0x3e0', ' my_sensor_read+0x94/0x1b0', ' worker_thread+0x138/0x3d0']]
        choice = rng.randrange(8)
        bodies = [f'irq/{rng.randrange(32, 256)}-my_sensor: irq handled status=0x{rng.getrandbits(16):04x}',
            f'drm display0: atomic commit frame={event} fence={rng.randrange(10000)}',
            f'ufs 1d84000.ufshc: command completed tag={rng.randrange(32)} latency_us={rng.randrange(500)}',
            f'rpmsg remoteproc0: rx seq={event} len={rng.randrange(512)}',
            f'binder: {rng.randrange(1000, 5000)}: transaction failed error -5',
            f'wlan: tx completion ring={rng.randrange(4)} pending={rng.randrange(64)}',
            f'thermal thermal_zone0: temperature={rng.randrange(30000, 85000)}',
            f'spi spi0.0: irq completion transfer={event} bytes={rng.randrange(1, 1024)}']
        return [('I', '', bodies[choice])]
    if kind == 'logcat':
        if event % 379 == 0:
            return [('E', 'AndroidRuntime', s) for s in ['FATAL EXCEPTION: main',
                'Process: com.example.driverlab, PID: 2345',
                'java.lang.IllegalStateException: device /dev/my_sensor is not ready',
                '\tat com.example.driverlab.SensorActivity.openDevice(SensorActivity.java:148)',
                '\tat android.app.Activity.performResume(Activity.java:8564)',
                'Caused by: java.io.IOException: ioctl failed: ETIMEDOUT (Connection timed out)']]
        if event % 607 == 0:
            return [('F', 'libc', 'Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0'),
                    ('F', 'DEBUG', '#00 pc 000000000001a234 /vendor/lib64/libmysensor.so (sensor_read+52)'),
                    ('F', 'DEBUG', '#01 pc 000000000000b198 /apex/com.android.runtime/lib64/bionic/libc.so')]
        if event % 257 == 0:
            return [('E', 'CameraService', f'Camera {event % 3}: configureStreams failed error -19')]
        tag, body = rng.choice([
            ('Binder', f'binder transaction seq={event} completed'),
            ('Binder', f'binder debug txn={event} buffer freed'),
            ('ActivityManager', 'Start proc com.example.driverlab for activity .SensorActivity'),
            ('SensorService', f'poll events={rng.randrange(32)} latency_us={rng.randrange(900)}'),
            ('OpenGLRenderer', f'frame={event} duration_ms={rng.randrange(1, 25)}'),
            ('BufferQueueProducer', f'queueBuffer slot={rng.randrange(8)} frame={event}'),
            ('PackageManager', 'dexopt com.example.driverlab reason=install flags=public'),
            ('WifiHAL', f'RSSI={rng.randrange(-90, -30)} frequency=5180')])
        return [(rng.choice(['D', 'I', 'I', 'W']), tag, body)]
    if event % 257 == 0:
        return [('E', 'DriverClient', 'java.io.IOException: ioctl /dev/my_sensor failed error=-110'),
                ('E', 'DriverClient', '\tat com.example.driverlab.DriverClient.read(DriverClient.java:88)'),
                ('E', 'DriverClient', '\tat java.util.concurrent.ThreadPoolExecutor.runWorker(ThreadPoolExecutor.java:1137)')]
    if event % 503 == 0:
        return [('E', 'Watchdog', 'ANR in com.example.driverlab: Input dispatching timed out'),
                ('E', 'Watchdog', 'main thread blocked in nativePollOnce'),
                ('E', 'Watchdog', '\tat android.os.MessageQueue.nativePollOnce(Native Method)')]
    if event % 263 == 0:
        return [('E', 'NativeBridge', 'SIGSEGV sensor_read addr=0x0'),
                ('E', 'NativeBridge', '#00 pc 0000000000001240 /data/app/com.example.driverlab/lib/arm64/libdriverjni.so')]
    if event % 127 == 0:
        return [('D', 'Payload', 'register_dump=' + rng.randbytes(2048).hex() + ' payload_end')]
    return [rng.choice([
        ('I', 'Health', f'heartbeat sequence={event} uptime_ms={event * 17}'),
        ('D', 'Sensor', f'温度={rng.randrange(20, 80)}℃ 通道={rng.randrange(8)} 采集正常😀'),
        ('I', 'DriverClient', f'ioctl read completed bytes={rng.randrange(16, 4096)} seq={event}'),
        ('D', 'Buffer', f'frame={event} checksum={rng.getrandbits(32):08x}'),
        ('W', 'Retry', f'retry={rng.randrange(1, 4)} errno=EAGAIN'),
        ('I', 'Lifecycle', 'onResume com.example.driverlab.SensorActivity')])]


def physical_lines(kind, rows, seed):
    rng = random.Random(seed + {'dmesg': 11, 'logcat': 23, 'app': 37}[kind])
    base = datetime(2026, 11, 19, 10, 0, 0)
    row = event = 0
    while row < rows:
        pid, tid, cpu = rng.randrange(1000, 6000), rng.randrange(6000, 9000), rng.randrange(8)
        block = messages(kind, event, rng)
        # Keep IDs in crash/warning bodies consistent with their prefixes.
        if kind == 'logcat' and event % 379 == 0:
            pid = tid = 2345
        if kind == 'dmesg' and event % 509 == 0 and event % 251 != 0:
            cpu, pid = 3, 420
        for level, tag, body in block:
            if row >= rows:
                break
            stamp = base + timedelta(milliseconds=row * 17)
            if kind == 'dmesg':
                line = f'[{row * 137 // 1000000:5d}.{row * 137 % 1000000:06d}] [C{cpu} T{pid:5d}] {body}'
            elif kind == 'logcat':
                line = f'{stamp:%m-%d %H:%M:%S}.{stamp.microsecond // 1000:03d} {pid:5d} {tid:5d} {level} {tag:<18}: {body}'
            elif body.startswith(('\tat ', '#00')):
                line = body
            else:
                line = json.dumps(dict(ts=stamp.isoformat(timespec='milliseconds'), level=level,
                    pid=pid, tid=tid, package='com.example.driverlab', tag=tag, message=body),
                    ensure_ascii=False, separators=(',', ':'))
            yield line.encode('utf-8')
            row += 1
        event += 1


def boundary_lines():
    return [b'\xef\xbb\xbfEDGE boot', b'EDGE invalid \xff sensor', b'EDGE NUL \0END', b'',
            b'EDGE crlf\r', 'EDGE 温度😀\tready'.encode(), b'EDGE repeated EDGE', b'no marker',
            b'EDGE dump ' + b'ab' * 131072 + b' payload_end',
            b'EDGE json ' + b'0123456789abcdef' * 65536 + b' payload_end',
            b'EDGE error -110', b'EDGE unterminated tail']


def input_signature(case, file_hash):
    # Match the versioned identity in search_worker/corpus.cpp. QJsonObject
    # emits sorted keys and compact UTF-8 for these fixed strings/integers.
    identity = dict(file_sha256=file_hash, pattern=case['pattern'],
        case_sensitive=case['case_sensitive'], inverse=case['inverse'],
        boolean=case['kind'].startswith('boolean-'), plain_text=case['kind'].endswith('literal'),
        encoding=case['encoding'], prefilter='', threads=case['threads'],
        chunk_lines=case['chunk_lines'], timeout_ms=case['timeout_ms'], signature_version=1)
    encoded = json.dumps(identity, sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
    return hashlib.sha256(encoded).hexdigest()


def generate(output, rows=65536, seed=20261119):
    output = Path(output)
    if type(rows) is not int or rows < 1024 or type(seed) is not int or not 0 <= seed < 2**32:
        raise ValueError('Use rows >= 1024 and a 32-bit nonnegative seed')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Use an empty output directory; existing files are never overwritten')
    output.mkdir(parents=True, exist_ok=True)
    queries = query_specs()
    expected = dict(version=1, synthetic=True, seed=seed, requested_rows=rows,
        generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), files={}, cases={})
    manifest = dict(version=1, cases=[])
    for kind in ('dmesg', 'logcat', 'app', 'boundaries'):
        filename = kind + '.log'
        selected = [q for q in queries if q['file'] == filename]
        states = [Oracle(q['predicate']) for q in selected]
        digest = hashlib.sha256()
        size = count = longest = oversized = 0
        lines = boundary_lines() if kind == 'boundaries' else physical_lines(kind, rows, seed)
        with (output / filename).open('xb') as writer:
            for raw in lines:
                # Boundary file's last record deliberately has no LF.
                terminated = not (kind == 'boundaries' and count == 11)
                encoded = raw + (b'\n' if terminated else b'')
                writer.write(encoded)
                digest.update(encoded)
                size += len(encoded)
                count += 1
                longest = max(longest, len(raw))
                oversized += len(raw) > 262144
                for state in states:
                    state.add(raw)
        file_hash = digest.hexdigest()
        expected['files'][filename] = dict(sha256=file_hash, bytes=size, rows=count,
                                           max_line_bytes=longest, oversized_lines=oversized)
        for query, state in zip(selected, states):
            for threads in (1, 2):
                case = {k: v for k, v in query.items() if k != 'predicate'}
                case.update(id=f"{query['id']}-t{threads}", encoding='UTF-8', threads=threads,
                            chunk_lines=1024, timeout_ms=120000)
                manifest['cases'].append(case)
                eligible = query['kind'] in ('literal', 'boolean-literal') and query['case_sensitive'] and bool(query['pattern'])
                expected['cases'][case['id']] = dict(state.finish(), file_sha256=file_hash,
                    input_sha256=input_signature(case, file_hash), input_bytes=size,
                    eligible=eligible, oversized_lines=oversized, threads=threads)
    for name, document in [('queries.json', manifest), ('expected.json', expected)]:
        (output / name).write_text(json.dumps(document, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return expected


def verify_report(expected_path, report_path):
    expected = json.loads(Path(expected_path).read_text(encoding='utf-8'))
    report = json.loads(Path(report_path).read_text(encoding='utf-8'))
    wanted = expected['cases']
    groups = {}
    signatures = {}
    for field in ('samples', 'profiles'):
        records = report[field]
        if not records or {r['scenario'] for r in records} != set(wanted):
            raise ValueError('Incomplete Android scenario coverage')
        groups[field] = defaultdict(list)
        for record in records:
            repetition = record['round']
            if type(repetition) is not int or repetition < 0 or record['candidate'] not in ('qt', 'memchr'):
                raise ValueError('Invalid Android round or backend')
            groups[field][record['scenario'], record['candidate'], repetition].append(record)
            signature = record['input_sha256']
            if signatures.setdefault(record['scenario'], signature) != signature:
                raise ValueError('Android query configuration changed between records')
            case = wanted[record['scenario']]
            for key in ('result_sha256', 'file_sha256', 'input_sha256', 'input_bytes'):
                if record[key] != case[key]:
                    raise ValueError(f"Android oracle mismatch: {record['scenario']} {key}")
            if field == 'samples':
                if type(record['elapsed_ns']) is not int or record['elapsed_ns'] <= 0:
                    raise ValueError('Invalid Android timing')
                if record['rows'] != case['rows'] or record['matched'] != case['matched']:
                    raise ValueError('Android row or hit count differs')
            else:
                fast = record['candidate'] == 'memchr' and case['eligible']
                if (record['complete_lines'] != case['rows'] or record['live_blocks'] != 0
                        or record['matchers'] != (case['threads'] if fast else 0)
                        or record['oversized_qt_lines'] != (case['oversized_lines'] if fast else 0)
                        or record['batched_rows'] != (case['rows'] - case['oversized_lines'] if fast else 0)
                        or record['max_batch_rows'] > 128 or record['max_batch_bytes'] > 262144):
                    raise ValueError('Android routing or resource contract differs')
    rounds = {key[2] for key in groups['samples']}
    if rounds != set(range(max(rounds) + 1)):
        raise ValueError('Incomplete Android rounds')
    required = {(name, candidate, repetition) for name in wanted
                for candidate in ('qt', 'memchr') for repetition in rounds}
    if any(set(group) != required for group in groups.values()):
        raise ValueError('Incomplete Android backend or round coverage')
    for key in required:
        roles = (['reference', 'candidate', 'candidate', 'reference'] if key[2] % 2 == 0
                 else ['candidate', 'reference', 'reference', 'candidate'])
        if ([r['role'] for r in groups['samples'][key]] != roles
                or len(groups['profiles'][key]) != 1):
            raise ValueError('Incomplete or duplicate Android pairs/profiles')
    return len(report['samples']), len(report['profiles'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    create = commands.add_parser('generate')
    create.add_argument('--output', type=Path, required=True)
    create.add_argument('--rows', type=int, default=65536)
    create.add_argument('--seed', type=int, default=20261119)
    check = commands.add_parser('verify')
    check.add_argument('--expected', type=Path, required=True)
    check.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'generate':
        expected = generate(args.output, args.rows, args.seed)
        for filename, stats in expected['files'].items():
            print(filename, stats['rows'], 'lines,', stats['bytes'], 'bytes, SHA256', stats['sha256'])
        print('SYNTHETIC corpus; query cases:', len(expected['cases']))
    else:
        samples, profiles = verify_report(args.expected, args.report)
        print(f'Android synthetic oracles passed: {samples} samples, {profiles} profiles')


if __name__ == '__main__':
    main()
