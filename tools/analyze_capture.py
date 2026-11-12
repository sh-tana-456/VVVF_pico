#!/usr/bin/env python3
"""Analyze CH0=U, CH1=V, CH2=W sigrok captures without third-party Python modules."""
import argparse
import cmath
import json
import math
import re
import zipfile


def analyze(path, hz):
    with zipfile.ZipFile(path) as archive:
        metadata = archive.read('metadata').decode()
        rate_text = next(line.split('=', 1)[1] for line in metadata.splitlines()
                         if line.startswith('samplerate='))
        value, unit = rate_text.split()
        fs = int(float(value) * {'Hz': 1, 'kHz': 1000, 'MHz': 1000000}[unit])
        source = archive.open('logic-1-1')
        states = [0] * 8
        fundamental = [0j] * 3
        samples = 0
        translate = bytes(i & 7 for i in range(256))
        # Integrate each 100 us bin before the fundamental projection.
        while raw := source.read(fs // 10000):
            chunk = raw.translate(translate)
            counts = [chunk.count(bytes([i])) for i in range(8)]
            t = (samples + len(chunk) / 2) / fs
            phasor = cmath.exp(-2j * math.pi * hz * t)
            for pair, (a, b) in enumerate(((0, 1), (1, 2), (2, 0))):
                signed = sum(counts[i] * (((i >> a) & 1) - ((i >> b) & 1)) for i in range(8))
                fundamental[pair] += signed * phasor
            for i in range(8):
                states[i] += counts[i]
            samples += len(chunk)
        duty = [sum(states[i] for i in range(8) if i & (1 << b)) / samples for b in range(3)]
        activity = [sum(states[i] for i in range(8) if ((i >> a) ^ (i >> b)) & 1) / samples
                    for a, b in ((0, 1), (1, 2), (2, 0))]
        source.close()
        uv = archive.read('logic-1-1').translate(bytes(((i ^ (i >> 1)) & 1) for i in range(256)))
        widths = sorted(match.end() - match.start() for match in re.finditer(b'\x01+', uv)
                        if match.start() > 0 and match.end() < len(uv))
        width_stats = None
        if widths:
            width_stats = {'count': len(widths), 'min_us': widths[0] * 1e6 / fs,
                           'median_us': widths[len(widths)//2] * 1e6 / fs,
                           'p95_us': widths[int((len(widths)-1)*.95)] * 1e6 / fs,
                           'max_us': widths[-1] * 1e6 / fs}
        return {'file': str(path), 'hz': hz, 'samplerate': fs, 'seconds': samples / fs,
                'duty_UVW': duty, 'UV_pulse_widths': width_stats, 'line_activity_UV_VW_WU': activity,
                'line_fundamental_peak_UV_VW_WU': [2 * abs(x) / samples for x in fundamental],
                'line_fundamental_phase_degrees_UV_VW_WU': [math.degrees(cmath.phase(x)) for x in fundamental]}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture')
    parser.add_argument('--hz', required=True, type=float)
    args = parser.parse_args()
    print(json.dumps(analyze(args.capture, args.hz), indent=2))
