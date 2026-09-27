"""Reproduce software evidence; does not flash hardware or claim physical accuracy.

Requires Python 3.10+, pypdf, unicorn, pyelftools and ARM GCC.
Run with --toolchain PATH_TO_ARM_GCC_BIN. Reports go to the enhancement project evidence/ directory.
"""
import argparse
from datetime import datetime, timezone
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ENH = Path(__file__).resolve().parents[1]
ROOT = ENH.parent
sys.path.insert(0, str(ENH / 'tests'))
from verify_core import load, call
from pypdf import PdfReader


def published_cases(toolchain):
    paths = [ROOT / p / 'Core/Src/mov_avg.s' for p in
             ['CG2028_Assignment', 'CG2028_Assignment_Test', 'CG2028_Enhancements']]
    assert len({p.read_bytes() for p in paths}) == 1, 'Assembly copies differ'
    print('PASS: all three projects contain identical assembly')
    source = (ROOT / 'CG2028_Assignment_Test/Core/Src/main.c').read_text()
    pages = PdfReader(ROOT / 'CG2028_Assignment_TestCases.pdf').pages
    assert len(pages) == 5, 'Review changed published test document'
    with tempfile.TemporaryDirectory(prefix='cg2028-open-') as folder:
        elf = Path(folder) / 'filter.elf'
        subprocess.run([str(toolchain / 'arm-none-eabi-gcc'), '-mcpu=cortex-m4',
                        '-mthumb', '-nostdlib', '-Wl,-Ttext=0x10000',
                        '-Wl,-e,ewma_filter', str(paths[1]), '-o', str(elf)], check=True)
        uc, symbols = load(elf)
        for case, page in enumerate(pages, 1):
            text = page.extract_text()
            alpha = int(re.search(r'alpha_percent\s*=\s*(\d+)', text)[1])
            assert re.search(r'run_test_case\("Open test case ' + str(case) +
                             r'",\s*' + str(alpha) + r'\s*,', source)
            axes = []
            for axis in 'xyz':
                pdf_values = re.search(r'sensor_data_' + axis + r'\[16\]\s*=\s*\{([^}]+)\}', text)[1]
                c_values = re.search(r'test' + str(case) + '_' + axis +
                                     r'\[16\]\s*=\s*\{([^}]+)\}', source)[1]
                values = [int(x) for x in pdf_values.split(',')]
                assert values == [int(x) for x in c_values.split(',')], (case, axis)
                axes.append(values)
            table = text.split('Expected output after each sample', 1)[1].split('Tuple sequence:', 1)[0]
            rows = re.findall(r'^\s*(\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s*$', table, re.M)
            assert len(rows) == 16, (case, 'Expected 16 PDF table rows')
            old = [0, 0, 0]
            for sample, row in enumerate(rows):
                assert int(row[0]) == sample + 1
                actual = [call(uc, symbols['ewma_filter'], [axes[a][sample], old[a], alpha])
                          for a in range(3)]
                assert actual == list(map(int, row[1:])), (case, sample + 1, actual, row)
                old = actual
            print(f'PASS: published case {case}, alpha={alpha}, 16 samples x 3 axes, exact PDF results')
    print('PASS: 240 sequential axis outputs; source vectors match the board test harness')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--toolchain', type=Path, required=True)
    parser.add_argument('--published-only', action='store_true')
    args = parser.parse_args()
    if args.published_only:
        published_cases(args.toolchain)
        return
    evidence = ENH / 'evidence'
    evidence.mkdir(exist_ok=True)
    jobs = [
        ('published-vectors', [sys.executable, str(Path(__file__).resolve()), '--toolchain', str(args.toolchain), '--published-only'], ROOT),
        ('baseline-core', [sys.executable, 'tests/verify_core.py', '--toolchain', str(args.toolchain)], ROOT / 'CG2028_Assignment'),
        ('enhancement-core', [sys.executable, 'tests/verify_core.py', '--toolchain', str(args.toolchain)], ENH),
        ('firmware-build', [sys.executable, 'scripts/build_firmware.py', '--toolchain', str(args.toolchain)], ENH),
        ('oled', [sys.executable, 'tests/verify_oled.py'], ENH),
        ('sensor-and-buzzer', [sys.executable, 'tests/verify_sensor_firmware.py'], ENH),
        ('receiver', [sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_receiver.py'], ENH),
        ('dashboard-app-syntax', ['node', '--check', 'receiver/app.js'], ENH),
        ('dashboard-sensors-syntax', ['node', '--check', 'receiver/sensors.js'], ENH),
    ]
    results = []
    build_ok = False
    for name, command, cwd in jobs:
        if name in ('sensor-and-buzzer', 'oled') and not build_ok:
            results.append((name, 'BLOCKED: fresh build failed'))
            continue
        try:
            result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, errors='replace')
            output = result.stdout + result.stderr
            status = 'PASS' if result.returncode == 0 else 'FAIL'
        except OSError as error:
            status, output = 'FAIL', str(error)
        (evidence / (name + '.log')).write_text(output, encoding='utf-8')
        results.append((name, status))
        if name == 'firmware-build':
            build_ok = status == 'PASS'
        print(name + ': ' + status, flush=True)
    report = ['# Automated assignment evidence', '',
              'Generated: ' + datetime.now(timezone.utc).isoformat(), '',
              'These are host/emulator results, not on-board or physical-motion results.', '',
              '| Check | Result | Log |', '| --- | --- | --- |']
    report += [f'| {name} | {status} | [{name}.log]({name}.log) |' for name, status in results]
    report += ['', '## Tested file fingerprints', '',
               'Record these with physical results to identify the tested firmware. Credentials are excluded.', '',
               '```text']
    paths = [ROOT / 'CG2028_Assignment_Test/Core/Src/mov_avg.s',
             ROOT / 'CG2028_Assignment_TestCases.pdf', ROOT / 'Marking_Rubric.pdf']
    paths += sorted((ENH / 'Core/Src').glob('*.c'))
    paths += [ENH / 'Core/Inc/fall_detector.h', ENH / 'Core/Inc/alert_ui.h',
              ENH / 'receiver/server.py']
    if build_ok:
        paths.append(ENH / 'build/CG2028_Enhancements.elf')
    report += [hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.relative_to(ROOT).as_posix()
               for p in paths]
    report += ['```', '', '## Still pending', '',
               '- On-board published unit tests and live assembly/C comparison.',
               '- Repeated physical fall, normal-activity and near-fall trials.',
               '- Measured LED timing, button operation, audible alerts and sampling under network load.',
               '- Physical microphone response and end-to-end Wi-Fi outage/recovery.',
               '- OLED wiring, legibility, fall/SOS/ACK screens, disconnection and recovery under load.',
               '', 'See ../DEMONSTRATION.md for the acceptance procedure.', '']
    (evidence / 'AUTOMATED_RESULTS.md').write_text('\n'.join(report), encoding='utf-8')
    if any(status != 'PASS' for _, status in results):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
