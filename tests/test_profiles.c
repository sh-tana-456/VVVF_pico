#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "vvvf/control.h"
#include "vvvf/config.h"

/* A new definition has no shipped ID and no evaluator branch. */
static const vvvf_profile_segment_t custom_power[]={
    {.from=0,.waveform=VVVF_SPWM,
     .carrier={.kind=VVVF_CURVE_LINEAR,.x0=0,.y0=800,.x1=10,.y1=1200},
     .amplitude={.kind=VVVF_CURVE_CONSTANT,.y0=.2f}},
    {.from=10,.waveform=VVVF_SPWM,.pulses=15,
     .amplitude={.kind=VVVF_CURVE_CONSTANT,.y0=.4f}},
    {.from=20,.waveform=VVVF_WIDE3,.pulses=3,
     .amplitude={.kind=VVVF_CURVE_CONSTANT,.y0=.64f},
     .angle_law={.maximum_degrees=55,.exponent=.5f}},
    {.from=30,.waveform=VVVF_ONE_PULSE,.pulses=1,
     .amplitude={.kind=VVVF_CURVE_CONSTANT,.y0=1}}
};
static const vvvf_profile_segment_t custom_brake[]={
    {.from=0,.waveform=VVVF_SPWM,.carrier={.kind=VVVF_CURVE_CONSTANT,.y0=600},
     .amplitude={.kind=VVVF_CURVE_CONSTANT,.y0=.3f}}
};
static const vvvf_profile_definition_t custom={
    .name="custom-without-id",.input_unit="test-units",.hz_per_unit=2,
    .amplitude_scale=1,.amplitude_mode=VVVF_RAW_REFERENCE,.amplitude_limit=1.2f,
    .power={custom_power,4},.brake={custom_brake,1}
};
int main(void) {
    assert(vvvf_profile_validate(&custom));
    vvvf_pattern_t p=vvvf_profile_evaluate(&custom,8,false);
    assert(p.valid && p.carrier_hz==960);
    assert(vvvf_profile_evaluate(&custom,19.999f,false).pulses==0);
    p=vvvf_profile_evaluate(&custom,20,false);
    assert(p.pulses==15 && p.carrier_hz==300); /* sync uses real Hz, not input units */
    assert(vvvf_profile_evaluate(&custom,40,true).carrier_hz==600);
    vvvf_command_t c=vvvf_command_from_profile(40,&custom,false);
    assert(c.enabled && c.waveform==VVVF_WIDE3);
    assert(fabs(c.pulse_angle*(360.0/4294967296.0)-33)<.0001);
    uint32_t step=c.fundamental_step;
    vvvf_command_set_amplitude(&c,.96f);
    assert(c.fundamental_step==step);
    assert(fabs(c.pulse_angle*(360.0/4294967296.0)-11)<.0001);
    vvvf_command_set_amplitude(&c,1.4f);
    assert(c.requested_modulation==(uint16_t)(1.2f*32767+.5f));
    assert(c.pulse_angle==0);
    assert(vvvf_profile_evaluate(&custom,60,false).waveform==VVVF_ONE_PULSE);
    vvvf_profile_definition_t fallback=custom;
    fallback.brake=(vvvf_profile_sequence_t){0};
    assert(vvvf_profile_validate(&fallback));
    assert(vvvf_profile_evaluate(&fallback,8,true).carrier_hz==960);

    /* Extrapolation is a curve property, not inferred from band boundaries. */
    vvvf_curve_t curve={.kind=VVVF_CURVE_LINEAR,.x0=23,.y0=1045,.x1=48.9f,.y1=710};
    assert(vvvf_curve_evaluate(&curve,49)<710);
    curve.clamp_input=true;assert(vvvf_curve_evaluate(&curve,49)==710);
    curve=(vvvf_curve_t){.kind=VVVF_CURVE_POWER,.x0=0,.y0=0,.x1=10,.y1=1,.exponent=2,.clamp_input=true};
    assert(vvvf_curve_evaluate(&curve,5)==.25f);
    assert(vvvf_curve_evaluate(&curve,20)==1);

    for(size_t i=0;i<vvvf_profile_count();++i) {
        const vvvf_profile_definition_t *definition=vvvf_profile_definition(i);
        assert(vvvf_profile_validate(definition));
        vvvf_profile_t id;
        assert(vvvf_profile_from_name(definition->name,&id) && id==i);
        if(definition->aliases) for(size_t j=0;definition->aliases[j];++j)
            assert(vvvf_profile_from_name(definition->aliases[j],&id) && id==i);
    }
    assert(!vvvf_profile_definition(vvvf_profile_count()));
    assert(!vvvf_command(20,vvvf_profile_count()).enabled);
    assert(!vvvf_command_from_profile(20,NULL,false).enabled);
    vvvf_profile_definition_t bad=custom;
    bad.hz_per_unit=0;assert(!vvvf_profile_validate(&bad));
    bad=custom;bad.amplitude_limit=3;assert(!vvvf_profile_validate(&bad));
    vvvf_profile_segment_t bad_band=custom_power[0];
    bad=custom;bad.power=(vvvf_profile_sequence_t){&bad_band,1};
    bad_band.from=1;assert(!vvvf_profile_validate(&bad));
    bad_band=custom_power[0];bad_band.carrier.x1=0;assert(!vvvf_profile_validate(&bad));
    assert(!vvvf_command_from_profile(20,&bad,false).enabled);
    bad_band=custom_power[0];bad_band.carrier=(vvvf_curve_t){.kind=VVVF_CURVE_CONSTANT,.y0=VVVF_SAMPLE_HZ};
    assert(!vvvf_profile_validate(&bad));assert(!vvvf_command_from_profile(20,&bad,false).enabled);
    bad_band=custom_power[2];bad_band.from=0;bad_band.angle_law.maximum_degrees=61;
    assert(!vvvf_profile_validate(&bad));assert(!vvvf_command_from_profile(20,&bad,false).enabled);
    puts("PASS: custom profile without ID/branches, independent curves, units, brake fallback, angle coefficients, registry and invalid definitions");
}
