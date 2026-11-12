#!/usr/bin/env python3
"""Check C preset evaluation against independently extracted public records."""
import json
import math
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
data = json.loads((root / 'Doc/preset-sources.json').read_text())
cases = []
for braking, key in [(False, 'AcceleratePattern'), (True, 'BrakingPattern')]:
    rows = data['jr-fall'][key]
    frequencies = set(i / 10 for i in range(1101))
    for row in rows:
        frequencies.update([max(0, row['from_hz']-.001), row['from_hz'], row['from_hz']+.001])
    for hz in sorted(frequencies):
        row = max((r for r in rows if r['from_hz'] <= hz), key=lambda r: r['from_hz'])
        if row['pulse_type'] == 'ASYNC':
            if row['carrier_mode'] == 'Const':
                fc = row['carrier_const']
            else:
                r = row['carrier_linear']
                fc = r['StartValue'] + (r['EndValue']-r['StartValue'])*(hz-r['Start'])/(r['End']-r['Start'])
            pulses, wave = 0, 0
        else:
            fc, pulses = 0, row['pulses']
            wave = 1 if pulses == 3 else 2
        a = row['amplitude']
        amplitude = a['StartAmplitude'] + (a['EndAmplitude']-a['StartAmplitude'])*(hz-a['StartFrequency'])/(a['EndFrequency']-a['StartFrequency'])
        if wave == 2:
            amplitude = 1
        cases.append(('jr-fall', hz, braking, fc, pulses, wave, amplitude))
p = data['pmsm-toshiba']
ratio = p['frequency_hz'] / p['speed_kmh']
for hz in [i/10 for i in range(1101)]:
    speed = hz / ratio
    # The public table is indexed by speed, not basic-wave frequency.
    row = next(r for r in p['power'] if speed < r['vmax'])
    kind = row['kind']
    fc = row['value'] if kind == 'ASYNC' else hz*row['value'] if kind == 'SYNC' else 0
    wave = {'ASYNC': 0, 'SYNC': 0, 'WIDE3': 1, 'ONEPULSE': 2}[kind]
    pulses = 0 if kind == 'ASYNC' else row['value']
    amplitude = min(hz/p['frequency_hz'], 1)**.92 if wave != 2 else 1
    cases.append(('pmsm-toshiba', hz, False, fc, pulses, wave, amplitude))
with tempfile.TemporaryDirectory(prefix='vvvf-source-check-') as tmp:
    source = Path(tmp) / 'query.c'
    binary = Path(tmp) / 'query'
    source.write_text('''#include <stdio.h>
#include "vvvf/control.h"
int main(void) {
 char name[32]; float hz; unsigned braking;
 while(scanf("%31s %f %u",name,&hz,&braking)==3) {
  vvvf_profile_t id;
  if(!vvvf_profile_from_name(name,&id)) return 1;
  vvvf_pattern_t p=vvvf_pattern_direction(hz,id,braking!=0);
  vvvf_command_t c=vvvf_command_direction(hz,id,braking!=0);
  printf("%.9g %u %u %u\\n",p.carrier_hz,(unsigned)p.pulses,(unsigned)p.waveform,(unsigned)c.requested_modulation);
 }
 return 0;
}
''')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-Iinclude', str(source),
                    'src/profiles/presets.c', 'src/control/profiles.c', 'src/control/control.c', 'src/control/voltage.c', '-lm', '-o', str(binary)], cwd=root, check=True)
    query = ''.join(f'{name} {hz:.9f} {int(braking)}\n' for name, hz, braking, *_ in cases)
    output = subprocess.run([str(binary)], input=query, text=True, capture_output=True, check=True)
    lines = output.stdout.splitlines()
    assert len(lines) == len(cases)
    for case, line in zip(cases, lines):
        name, hz, braking, fc, pulses, wave, amplitude = case
        actual_fc, actual_n, actual_wave, actual_amplitude = map(float, line.split())
        assert abs(actual_fc-fc) < .01, (case, line)
        assert actual_n == pulses and actual_wave == wave, (case, line)
        assert abs(actual_amplitude-round(amplitude*32767)) <= 2, (case, line)
print(f'PASS: {len(cases)} public-source frequency, direction, waveform and amplitude cases')
