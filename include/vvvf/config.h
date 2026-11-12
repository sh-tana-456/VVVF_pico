#pragma once
#define VVVF_SAMPLE_HZ 100000u
#define VVVF_BLOCK_SAMPLES 256u
#define VVVF_PIN_BASE 20u /* bit0=W, bit1=V, bit2=U */
#define VVVF_UP_PIN 16u
#define VVVF_DOWN_PIN 17u
#define VVVF_MAX_HZ 110.0f
#define VVVF_RAMP_HZ_S 5.0f
#define VVVF_WORDS_PER_FRAME 20u

/* Name from src/profiles/presets.c (including custom appended profiles). */
#define VVVF_DEFAULT_PROFILE_NAME "jr-fall" // jr-fall, igbt, gto, pmsm-toshiba
