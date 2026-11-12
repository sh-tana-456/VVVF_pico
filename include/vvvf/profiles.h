#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Shipped IDs are stable. New entries can be appended to presets.c and looked
   up by name without adding an ID or editing the evaluator. */
typedef size_t vvvf_profile_t;
enum { VVVF_IGBT, VVVF_GTO, VVVF_JR_FALL_SOUND, VVVF_PMSM_SOUND };
typedef enum { VVVF_SPWM, VVVF_WIDE3, VVVF_ONE_PULSE } vvvf_waveform_t;
typedef enum { VVVF_CURVE_CONSTANT, VVVF_CURVE_LINEAR, VVVF_CURVE_POWER } vvvf_curve_kind_t;

typedef struct {
    vvvf_curve_kind_t kind;
    float x0, y0, x1, y1;
    float exponent; /* POWER: y0+(y1-y0)*pow((x-x0)/(x1-x0), exponent) */
    bool clamp_input; /* Clamp normalized x to [0,1]; false allows extrapolation. */
} vvvf_curve_t;

typedef struct {
    float maximum_degrees;
    float exponent; /* alpha=maximum_degrees * pow(1-clamp(m,0,1), exponent) */
} vvvf_angle_law_t;

typedef enum { VVVF_VOLTAGE_TARGET, VVVF_RAW_REFERENCE } vvvf_amplitude_mode_t;
typedef struct {
    float deviation_hz; /* Uniform carrier deviation +/- Hz; zero disables. */
    float update_hz; /* Sample-and-hold update rate, independent of foreground loop. */
} vvvf_random_carrier_t;

typedef struct {
    float from; /* Inclusive lower threshold in the profile's input units. */
    vvvf_waveform_t waveform;
    uint8_t pulses; /* SPWM: 0=async, otherwise fc/f1. Direct modes use 3 or 1. */
    vvvf_random_carrier_t random_carrier;
    vvvf_curve_t carrier; /* Used only for asynchronous SPWM. */
    vvvf_curve_t amplitude;
    vvvf_angle_law_t angle_law; /* Used only for WIDE3. */
} vvvf_profile_segment_t;

typedef struct {
    const vvvf_profile_segment_t *segments;
    size_t count;
} vvvf_profile_sequence_t;

typedef struct {
    const char *name;
    const char *const *aliases; /* Optional NULL-terminated list. */
    const char *input_unit;
    float hz_per_unit; /* 1 for Hz; a speed-based preset can supply Hz/(km/h). */
    vvvf_amplitude_mode_t amplitude_mode;
    float amplitude_limit;
    float amplitude_scale; /* Applied before limiting; 1=unchanged; 0 sets m=0, not output disable. */
    vvvf_profile_sequence_t power;
    vvvf_profile_sequence_t brake; /* Empty: use power sequence. */
    const char *source; /* Provenance only; never used to select behavior. */
} vvvf_profile_definition_t;

/* Registry/data live entirely in src/profiles/presets.c. */
extern const vvvf_profile_definition_t vvvf_profile_definitions[];
extern const size_t vvvf_profile_definition_count;

typedef struct {
    float carrier_hz;
    vvvf_random_carrier_t random_carrier;
    uint8_t pulses;
    vvvf_waveform_t waveform;
    float amplitude;
    vvvf_amplitude_mode_t amplitude_mode;
    float amplitude_limit;
    float amplitude_scale; /* Applied before limiting; 1=unchanged; 0 sets m=0, not output disable. */
    vvvf_angle_law_t angle_law;
    bool valid;
} vvvf_pattern_t;

size_t vvvf_profile_count(void);
const vvvf_profile_definition_t *vvvf_profile_definition(vvvf_profile_t id);
bool vvvf_profile_validate(const vvvf_profile_definition_t *definition);
float vvvf_curve_evaluate(const vvvf_curve_t *curve, float x);
vvvf_pattern_t vvvf_profile_evaluate(const vvvf_profile_definition_t *definition, float hz, bool braking);
vvvf_pattern_t vvvf_pattern(float hz, vvvf_profile_t profile);
vvvf_pattern_t vvvf_pattern_direction(float hz, vvvf_profile_t profile, bool braking);
const char *vvvf_profile_name(vvvf_profile_t profile);
bool vvvf_profile_from_name(const char *name, vvvf_profile_t *profile);
const char *vvvf_waveform_name(vvvf_waveform_t waveform);
