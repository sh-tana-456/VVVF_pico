#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "vvvf/profiles.h"
typedef struct {
    uint32_t fundamental_step, carrier_step;
    uint32_t random_step_deviation, random_hold_samples;
    uint16_t modulation; /* Comparator/source amplitude Q15: up to 1.4 for sourced overmodulation. */
    uint8_t pulses;      /* SPWM ratio, or named programmed pulse pattern. */
    bool enabled;
    vvvf_waveform_t waveform;
    uint32_t pulse_angle; /* 0..pi/3 in 32-bit-turn units for direct patterns. */
    vvvf_angle_law_t angle_law;
    float amplitude_limit, amplitude_scale;
    bool amplitude_is_reference; /* Public preset raw amplitude, not voltage target. */
    uint16_t requested_modulation; /* Display: desired voltage or source amplitude. */
} vvvf_command_t;
vvvf_command_t vvvf_command(float hz, vvvf_profile_t profile);
vvvf_command_t vvvf_command_direction(float hz, vvvf_profile_t profile, bool braking);
/* Evaluate a validated custom definition without adding a registry ID. */
vvvf_command_t vvvf_command_from_profile(float hz, const vvvf_profile_definition_t *definition, bool braking);
/* Update amplitude and cached switching angle together before publishing. */
void vvvf_command_set_amplitude(vvvf_command_t *command, float amplitude);
float vvvf_ramp(float hz, bool up, bool down, float seconds);
