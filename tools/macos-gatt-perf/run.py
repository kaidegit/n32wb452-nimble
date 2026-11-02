#!/usr/bin/env python3
"""Measure a previously flashed GATT server; never flashes or resets hardware."""
import argparse
import datetime
import json
import hashlib
import math
import shutil
from pathlib import Path
import subprocess


def assess_case(summary, events, returncode, duration, size, request_dle=False):
    """Keep payload integrity, applied load, and radio acceptance distinct."""
    stats = summary.get('receiver_stats', {})
    direction = summary.get('direction')
    verified = summary.get('verified_bytes', 0)
    integrity = (direction in ('up', 'down') and returncode == 0 and summary.get('exit_code') == 0
                 and summary.get('reason') == 'perf_complete'
                 and summary.get('failures') == 0 and stats.get('errors') == 0
                 and verified > 0 and stats.get('bytes') == verified
                 and stats.get('packets', 0) * size == verified
                 and summary.get('transfer_seconds', 0) >= duration - 0.25
                 and (direction != 'up' or summary.get('queued_bytes') == verified)
                 and (direction != 'down' or stats.get('producer_done') is True))
    checks = [event for event in events if event.get('event') == 'phy_status'
              and event.get('operation') in ('wait_phy', 'verify_phy')]
    phy_verified = ({event.get('operation') for event in checks}
                    == {'wait_phy', 'verify_phy'} and all(
                        event.get('status') == 0 and event.get('tx_phy') == 1
                        and event.get('rx_phy') == 1 and not event.get('pending')
                        for event in checks))
    dle_verified = stats.get('dle_tx_octets') == stats.get('dle_rx_octets') == 251
    backpressure = (summary.get('backpressure_events', 0) > 0 if direction == 'up'
                    else stats.get('producer_blocked_cycles', 0) > 0)
    dle_request_ok = (not request_dle or (dle_verified
                      and stats.get('dle_request_attempted') is True
                      and stats.get('dle_request_rc') == 0))
    passed = integrity and backpressure and dle_request_ok
    return {'data_integrity_passed': integrity, 'backpressure_observed': backpressure,
            'radio_parameters_verified': phy_verified and dle_verified,
            'passed': passed,
            'reference_standard_met': passed and phy_verified and dle_verified
                and size == 244 and duration >= 15}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('nimble', 'bridge'), required=True)
    parser.add_argument('--name', required=True, help='Exact advertised server name')
    parser.add_argument('--duration', type=float, default=30)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--size', type=int, default=244)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--request-dle', action='store_true', help='Explicitly request DLE251 before connection-parameter setup')
    parser.add_argument('--strict-radio', action='store_true', help='Require successful PHY commands and DLE251')
    args = parser.parse_args()
    if (not math.isfinite(args.duration) or not 0 < args.duration <= 3600
            or not 1 <= args.repeats <= 100 or not 4 <= args.size <= 244):
        parser.error('duration must be 0..3600s; repeats 1..100; size 4..244')
    tool = Path(__file__).resolve().parent
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output = args.output or tool / '.build' / f'{args.mode}-{timestamp}'
    output.mkdir(parents=True, exist_ok=False)
    root = tool.parent.parent
    elf = root / f'build-{args.mode}' / f'n32wb452_ble_{args.mode}'
    if not elf.is_file():
        parser.error(f'firmware ELF missing: {elf}')
    subprocess.run([str(tool / 'ble.sh'), '--self-test'], check=True)
    shutil.copy2(elf, output / 'firmware.elf')
    for source in ('main.swift', 'run.py', 'ble.sh'):
        shutil.copy2(tool / source, output / source)
    source_paths = [
        'components/gatt_perf', 'app/app_nimble.c',
        'components/ble/ble_spi_tx.c', 'components/ble/hci_spi.c',
        'components/ble/hci_fixup.c', 'tools/blectl/perf_server.c',
        'tools/blectl/hci_cdc.c', 'tools/blectl/inc/syscfg/syscfg.h',
    ]
    for relative in source_paths:
        source = root / relative
        destination = output / 'sources' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.is_dir():
            shutil.copytree(source, destination, dirs_exist_ok=True)
        else:
            shutil.copy2(source, destination)
    binary_hash = None
    if args.mode == 'bridge':
        binary = root / 'tools/blectl/build/blectl'
        shutil.copy2(binary, output / 'blectl')
        binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    reference_client = root / 'tools/macos-ble/main.swift'
    metadata = {
        'blectl_sha256': binary_hash,
        'mode': args.mode, 'name': args.name, 'duration': args.duration,
        'size': args.size, 'repeats': args.repeats,
        'firmware_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
        'client_sha256': hashlib.sha256((tool / 'main.swift').read_bytes()).hexdigest(),
        'macos': subprocess.check_output(['sw_vers'], text=True),
        'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'reference_client_sha256': hashlib.sha256(reference_client.read_bytes()).hexdigest()
            if reference_client.is_file() else None,
        'protocol': 'control v1 12 bytes; stats v5 100 bytes, elapsed_us at offset96; GATT sequence+pattern',
        'note': 'ELF is a local build snapshot; not on-target firmware attestation',
    }
    (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
    (output / 'changes.patch').write_text(subprocess.check_output(['git', 'diff', 'HEAD'], cwd=root, text=True))
    results = []
    run_status = 'running'
    active_case = None
    try:
        for repeat in range(1, args.repeats + 1):
            for direction in ('up', 'down'):
                stem = f'{args.mode}-{direction}-{repeat}'
                command = [str(tool / 'ble.sh'), 'probe', '--name', args.name,
                           '--perf', '--direction', direction,
                           '--duration', str(args.duration), '--timeout', str(args.duration + 45),
                           '--size', str(args.size)]
                if args.request_dle:
                    command += ['--request-dle']
                if not args.strict_radio:
                    command += ['--legacy-controller']
                active_case = {'mode': args.mode, 'direction': direction,
                               'repeat': repeat, 'command': command}
                print(f'{stem}: running {args.duration:g}s', flush=True)
                with (output / f'{stem}.jsonl').open('w') as stdout, (output / f'{stem}.stderr').open('w') as stderr:
                    result = subprocess.run(['caffeinate', '-i', *command], stdout=stdout, stderr=stderr,
                                            timeout=args.duration + 60)
                events = [json.loads(line) for line in (output / f'{stem}.jsonl').read_text().splitlines()]
                summaries = [event for event in events if event.get('event') == 'summary']
                summary = summaries[-1] if summaries else {}
                assessment = assess_case(summary, events, result.returncode,
                                         args.duration, args.size, args.request_dle)
                if args.strict_radio and not assessment['radio_parameters_verified']:
                    assessment['passed'] = False
                results.append({**active_case, **assessment,
                                'returncode': result.returncode, 'summary': summary})
                active_case = None
                print(json.dumps(results[-1], sort_keys=True), flush=True)
                if not assessment['passed']:
                    raise RuntimeError(f'{stem} failed; stopped without retry')
        run_status = 'passed'
    except BaseException as error:
        run_status = 'failed'
        if active_case is not None:
            results.append({**active_case, 'passed': False,
                            'reference_standard_met': False, 'error': str(error)})
        metadata['error'] = str(error)
        raise
    finally:
        metadata['status'] = run_status
        metadata['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        (output / 'report.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
