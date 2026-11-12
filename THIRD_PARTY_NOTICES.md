# Third-party references and notices

This file records the origin and scope of third-party material. It does not
license the entire project or grant rights that the upstream authors have not
granted. See [the publication audit](Doc/license-audit.md) for unresolved items.

## JR E231 Hitachi preset parameters — permission not established

Source: [VvvfGeeks/YAML-Files](https://github.com/VvvfGeeks/YAML-Files),
`VVVF/JRE_E231_HITACHI.yaml`, commit
`2f83dc2cd0466b35ce9720d5101164656064f70c`.

Used for the `jr-fall` frequency/amplitude settings in
`src/profiles/presets.c`, parameter excerpts in `Doc/preset-sources.json`, and
related documentation/tests. No LICENSE/COPYING file or explicit redistribution
license was found in the referenced repository. Public visibility alone is not
a redistribution license. Permission for distributing these preset excerpts
and preset-containing releases has not been established.

The switching-angle interpretation was checked against
[VvvfGeeks/VVVF-Simulator](https://github.com/VvvfGeeks/VVVF-Simulator),
`VvvfSimulator/Vvvf/Calculation/L2.cs`, commit
`fb8d0c99e0ccc3708fa3429f71bc9270db460193`. Its README describes noncommercial
use and asks for a project link when publishing videos made with it. No standard
open-source license was found. The C implementation here was written separately;
this fact is not a claim that all possible upstream rights are cleared.

These projects and their authors do not endorse this project.

## Toshiba PMSM user preset — MIT

Parameter and angle-model reference:
[naoto64/VVVF-Simulator](https://github.com/naoto64/VVVF-Simulator), commit
`9335c10a135e383e60dc4a1d1ba22063eee40325`, including
`vvvf_pwm_pattern_presets.json`, `vvvf_current_settings.json`, and the Python
simulator's speed/frequency conversion and wide-three-pulse angle model.

Copyright (c) 2026 naoto64.

The MIT license text is reproduced in
[Doc/third-party/naoto64-MIT.txt](Doc/third-party/naoto64-MIT.txt).
It applies to the referenced/adapted portions, not automatically to the entire
project or to the JR preset material.

## Raspberry Pi Pico SDK and USB runtime

The build uses Pico SDK 2.3.1, commit
`079c6f39023649b154152db30f1d781e884879bc`.
`pico_sdk_import.cmake` is a copied SDK file and retains its original notice.

- SDK default: Copyright Raspberry Pi (Trading) Ltd., BSD-3-Clause.
  [License](Doc/third-party/pico-sdk-BSD-3-Clause.txt).
- pico_printf: Copyright (c) 2014 Marco Paland, MIT.
  [License](Doc/third-party/pico-printf-MIT.txt).
- Default USB descriptors include MicroPython-derived MIT material,
  Copyright (c) 2019 Damien P. George and (c) 2020 Raspberry Pi (Trading) Ltd.
- TinyUSB, commit `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`: MIT.
  [Root license](Doc/third-party/tinyusb-MIT.txt).

Additional per-file copyright/license headers, including the USB descriptors
and TinyUSB contributors, are preserved in
[compiled-sdk-source-notices.txt](Doc/third-party/compiled-sdk-source-notices.txt).
Do not remove these notices when distributing firmware with the accompanying
materials. Not all SDK libraries or conditional USB classes are linked/used.

## ARM GNU runtime

The checked build uses Arm GNU Toolchain 14_2_Rel1 / GCC 14.2.1 and links Newlib
C/math runtime objects and libgcc (see the linker map).
The supplied toolchain's complete license collection is preserved in
[arm-gnu-toolchain-14_2_Rel1-license.txt](Doc/third-party/arm-gnu-toolchain-14_2_Rel1-license.txt).
The [GCC Runtime Library Exception 3.1](Doc/third-party/gcc-runtime-exception-3.1.txt)
is included separately for ease of review.

The collection covers many tools/components that are not linked into this
firmware. Its inclusion is not a statement that all of those components' terms
apply to the firmware, or that the complete toolchain is being distributed.
Newlib has multiple component-specific notices; do not summarize it as a single
MIT or BSD grant for all files.

## Host measurement tools

`tools/capture_logic.c` uses the libsigrok/GLib APIs. libsigrok is GPL-3.0-or-later
([upstream](https://sigrok.org/wiki/Libsigrok)); its linked host executable must
be considered separately from the Pico firmware. The project currently includes
source only, not a bundled libsigrok binary or the `/tmp/vvvf_capture` executable.
Do not assume that a compiled host tool can be redistributed under MIT alone.

PulseView, the analyzer firmware, compiler binaries and other installed tools
are not bundled. Recorded `.sr` files are local measurements, not copies of the
PulseView or libsigrok program. This does not settle rights associated with any
preset material represented in those recordings.
