/* Reference/preset provenance and licensing scope: THIRD_PARTY_NOTICES.md.
 * JR reference permissions remain unconfirmed; see Doc/license-audit.md.
 */
#include "vvvf/control.h"
#include "vvvf/config.h"
#include <math.h>
#include "vvvf/voltage.h"
static float clamp_hz(float hz) {
    if (!isfinite(hz) || hz < 0) return 0;
    return hz > VVVF_MAX_HZ ? VVVF_MAX_HZ : hz;
}
float vvvf_ramp(float hz, bool up, bool down, float seconds) {
    if (!isfinite(seconds) || seconds < 0) seconds = 0;
    /* Bound a delayed foreground iteration, so USB cannot cause a large jump. */
    if (seconds > 0.05f) seconds = 0.05f;
    return clamp_hz(clamp_hz(hz) + ((int)up - (int)down)*VVVF_RAMP_HZ_S*seconds);
}
vvvf_command_t vvvf_command(float hz, vvvf_profile_t profile) {
    return vvvf_command_direction(hz,profile,false);
}
vvvf_command_t vvvf_command_direction(float hz, vvvf_profile_t profile, bool braking) {
    return vvvf_command_from_profile(hz,vvvf_profile_definition(profile),braking);
}
vvvf_command_t vvvf_command_from_profile(float hz,const vvvf_profile_definition_t *definition,bool braking) {
    hz = clamp_hz(hz);
    vvvf_pattern_t pattern = vvvf_profile_evaluate(definition,hz,braking);
    if (!pattern.valid) pattern=(vvvf_pattern_t){0};
    vvvf_command_t command={
        .fundamental_step = (uint32_t)((double)hz * 4294967296.0 / VVVF_SAMPLE_HZ + 0.5),
        .carrier_step = (uint32_t)((double)pattern.carrier_hz * 4294967296.0 / VVVF_SAMPLE_HZ + 0.5),
        .amplitude_is_reference=(pattern.amplitude_mode==VVVF_RAW_REFERENCE),.waveform=pattern.waveform,
        .angle_law=pattern.angle_law,.amplitude_limit=pattern.amplitude_limit,
        .amplitude_scale=pattern.amplitude_scale,
        .random_step_deviation=(uint32_t)((double)pattern.random_carrier.deviation_hz*4294967296.0/VVVF_SAMPLE_HZ),
        .random_hold_samples=pattern.random_carrier.deviation_hz>0 ?
            (uint32_t)(VVVF_SAMPLE_HZ/pattern.random_carrier.update_hz+.5f) : 0,
        .pulses=pattern.pulses,.enabled=pattern.valid && hz>0.01f
    };
    vvvf_command_set_amplitude(&command,pattern.amplitude);
    return command;
}
void vvvf_command_set_amplitude(vvvf_command_t *command, float m) {
    if (!isfinite(m) || m<0) m=0;
    m*=command->amplitude_scale;
    float maximum=command->amplitude_limit;
    if (!isfinite(maximum) || maximum<0) maximum=0;
    if (m>maximum) m=maximum;
    uint16_t requested=(uint16_t)(m*32767.0f+.5f);
    command->requested_modulation=requested;
    command->modulation=command->amplitude_is_reference ? requested :
        vvvf_voltage_reference(requested,command->pulses);
    command->pulse_angle=0;
    if (command->waveform==VVVF_WIDE3) {
        const vvvf_angle_law_t *law=&command->angle_law;
        if (!isfinite(law->maximum_degrees) || law->maximum_degrees<0 ||
            law->maximum_degrees>60 || !isfinite(law->exponent) || law->exponent<=0) return;
        double remaining=1.0-fminf(m,1);
        double shape=law->exponent==1 ? remaining : powf((float)remaining,law->exponent);
        command->pulse_angle=(uint32_t)(shape*law->maximum_degrees/360*4294967296.0);
    }
}
