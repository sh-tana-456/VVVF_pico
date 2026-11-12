#pragma once
#include "vvvf/control.h"
#include <stddef.h>
typedef struct {
    uint32_t fundamental, carrier;
    uint8_t previous_pulses;
    uint32_t carrier_offset;
    uint32_t random_state, random_remaining;
    int32_t random_offset;
    uint32_t previous_random_deviation, previous_random_hold;
} vvvf_modulator_t;
void vvvf_modulator_init(void);
/* Coarse control-point preview for numeric tests, not the PIO output path.
   Output bit0=W, bit1=V, bit2=U. No SDK dependencies. */
uint32_t vvvf_sample(vvvf_modulator_t *state, const vvvf_command_t *command);
void vvvf_render(vvvf_modulator_t *state, const vvvf_command_t *command,
                 uint32_t *samples, size_t count);
/* Variable-duration PIO words: low 3 bits=pins, upper 29 bits=ticks-3.
   Buffer capacity must be at least count*VVVF_WORDS_PER_FRAME words. */
size_t vvvf_render_timed(vvvf_modulator_t *state, const vvvf_command_t *command,
                         uint32_t *words, size_t count, uint32_t ticks_per_frame);
