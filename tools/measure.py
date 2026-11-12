#!/usr/bin/env python3
"""Bench-only SPWM preset measurements. CH0=U CH1=V CH2=W, no power stage.
Compile capture_logic.c first; close PulseView to release the analyzer.
Always commands stop before closing the Pico serial port.
"""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import termios
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def save_capture(destination, raw, fs):
    metadata = ('[global]\nsigrok version=0.5.2\n\n[device 1]\n'
                'capturefile=logic-1\ntotal probes=8\nunitsize=1\n'
                f'samplerate={fs} Hz\n')
    metadata += ''.join(f'probe{i+1}={name}\n' for i, name in
                        enumerate(['U', 'V', 'W', 'CH3', 'CH4', 'CH5', 'CH6', 'CH7']))
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('version', '2')
        archive.writestr('metadata', metadata)
        archive.writestr('logic-1-1', raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--helper', default='/tmp/vvvf_capture')
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--tag', default='measurement')
    parser.add_argument('--profile', choices=['igbt','gto','jr-fall','pmsm-toshiba','pmsm-demo'], default='jr-fall')
    parser.add_argument('--startup', action='store_true')
    parser.add_argument('--samplerate', type=int, default=12000000)
    args = parser.parse_args()
    if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.tag):
        parser.error('tag must contain only letters, numbers, hyphens and underscores')
    raw_dir = ROOT / 'build/captures'
    raw_dir.mkdir(parents=True, exist_ok=True)
    saved_dir = ROOT / 'Doc/captures'
    saved_dir.mkdir(parents=True, exist_ok=True)
    fd = os.open(args.port, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
    original = termios.tcgetattr(fd)
    config = termios.tcgetattr(fd)
    config[0] = config[1] = config[3] = 0
    termios.tcsetattr(fd, termios.TCSANOW, config)
    process = None
    results = []
    try:
        os.write(fd, b'stop\n')
        time.sleep(.1)
        os.write(fd, ('profile ' + args.profile + '\n').encode())
        time.sleep(.1)
        plan = [(0, 3)] if args.startup else [
            (0, 1), (.05, 2), (.1, 2), (.5, 4), (1, 2), (5, 1),
            (20, 1), (45, 1), (55, 1), (100, 1), (0, 1)]
        for hz, seconds in plan:
            os.write(fd, f'f {hz}\n'.encode())
            time.sleep(.12)
            label = 'startup' if args.startup else f'{hz:g}Hz'
            path = raw_dir / f'{args.tag}-{label}.bin'
            process = subprocess.Popen([args.helper, str(path),
                                        str(int(args.samplerate * seconds)), str(args.samplerate)],
                                       stderr=subprocess.PIPE, text=True)
            logs = []
            # The helper announces start after the analyzer has begun acquisition.
            deadline = time.monotonic() + 15
            while True:
                if time.monotonic() > deadline:
                    raise RuntimeError('Analyzer did not start within 15 seconds')
                if select.select([process.stderr], [], [], .1)[0]:
                    line = process.stderr.readline()
                    if line.startswith('CAPTURE_STARTED'):
                        break
                    if line:
                        print(line, end='')
                    elif process.poll() is not None:
                        raise RuntimeError('Analyzer failed to start')
            start = time.monotonic()
            actions = [(.2, 'r 5'), (2.0, 'stop')] if args.startup else []
            while process.poll() is None:
                elapsed = time.monotonic() - start
                while actions and elapsed >= actions[0][0]:
                    _, command = actions.pop(0)
                    os.write(fd, (command + '\n').encode())
                    logs.append(f'HOST t={elapsed:.3f} command={command}\n')
                if select.select([fd], [], [], .02)[0]:
                    logs.append(os.read(fd, 32768).decode(errors='replace'))
                if elapsed > seconds + 15:
                    raise RuntimeError('Analyzer acquisition timed out')
            if process.returncode:
                raise RuntimeError(f'Analyzer failed: {process.stderr.read()}')
            process.stderr.close()
            raw = path.read_bytes()
            if len(raw) != int(args.samplerate * seconds):
                raise RuntimeError(f'Short capture: {len(raw)} bytes')
            logic = raw.translate(bytes(i & 7 for i in range(256)))
            if args.startup or hz > .01:
                if logic.count(logic[:1]) == len(logic):
                    raise RuntimeError('No transitions on CH0..2: check probes and common GND')
            elif logic.count(b'\x00') != len(logic):
                raise RuntimeError('Stopped Pico is not Low on CH0..2: check probes and common GND')
            save_capture(saved_dir / f'{args.tag}-{label}.sr', raw, args.samplerate)
            result = {'profile': args.profile, 'hz': hz, 'seconds': seconds, 'samplerate': args.samplerate,
                      'capture': f'{args.tag}-{label}.sr', 'log': ''.join(logs).splitlines()}
            results.append(result)
            print(json.dumps(result), flush=True)
    finally:
        os.write(fd, b'stop\n')
        time.sleep(.1)
        os.write(fd, b'profile jr-fall\n')
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        termios.tcsetattr(fd, termios.TCSANOW, original)
        os.close(fd)
    suffix = 'startup' if args.startup else 'summary'
    (saved_dir / f'{args.tag}-{suffix}.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
