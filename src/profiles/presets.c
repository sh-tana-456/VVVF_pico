/* Editable profile data. No vehicle-specific branches belong in the evaluator.
 * Reference/preset provenance: THIRD_PARTY_NOTICES.md and Doc/preset-sources.md.
 * JR reference permissions remain unconfirmed; see Doc/license-audit.md.
 */
#include "vvvf/profiles.h"
#define COUNT(a) (sizeof(a)/sizeof(*(a)))
#define SEQUENCE(a) {.segments=(a),.count=COUNT(a)}
#define CONSTANT(value) {.kind=VVVF_CURVE_CONSTANT,.y0=(value)}
#define LINEAR(a,b,c,d) {.kind=VVVF_CURVE_LINEAR,.x0=(a),.y0=(b),.x1=(c),.y1=(d)}
#define CLAMPED_LINEAR(a,b,c,d) {.kind=VVVF_CURVE_LINEAR,.x0=(a),.y0=(b),.x1=(c),.y1=(d),.clamp_input=true}
#define CLAMPED_POWER(a,b,c,d,p) {.kind=VVVF_CURVE_POWER,.x0=(a),.y0=(b),.x1=(c),.y1=(d),.exponent=(p),.clamp_input=true}
#define VF CLAMPED_LINEAR(0,0,100,1)
#define ASYNC(start,fc) {.from=(start),.waveform=VVVF_SPWM,.carrier=CONSTANT(fc),.amplitude=VF}
#define SYNC(start,n) {.from=(start),.waveform=VVVF_SPWM,.pulses=(n),.amplitude=VF}

static const vvvf_profile_segment_t igbt_power[]={
    {.from=0,.waveform=VVVF_SPWM,.carrier=CONSTANT(1000),.amplitude=VF,
     .random_carrier={.deviation_hz=100,.update_hz=1000}},
    SYNC(40,9), SYNC(50,3)
};
static const vvvf_profile_segment_t gto_power[]={
    ASYNC(0,175), ASYNC(2,197), ASYNC(4,223), ASYNC(6,234),
    ASYNC(8,262), ASYNC(10,294), ASYNC(12,312), ASYNC(14,350),
    ASYNC(16,400), SYNC(46,15), SYNC(60,9), SYNC(70,3)
};

/* VvvfGeeks/YAML-Files @2f83dc2, JRE_E231_HITACHI.yaml.
   Curve endpoints 48.9/55.9 deliberately differ from switch thresholds 49/56. */
#define JR_POWER_AMPLITUDE LINEAR(0,0,65,1.37471264f)
#define JR_BRAKE_AMPLITUDE LINEAR(0,0,73,1.4f)
static const vvvf_profile_segment_t jr_power[]={
    {.from=0,.waveform=VVVF_SPWM,.carrier=CONSTANT(1045),.amplitude=JR_POWER_AMPLITUDE},
    {.from=23,.waveform=VVVF_SPWM,.carrier=LINEAR(23,1045,48.9f,710),.amplitude=JR_POWER_AMPLITUDE},
    {.from=49,.waveform=VVVF_SPWM,.carrier=LINEAR(49,710,60,1750),.amplitude=JR_POWER_AMPLITUDE},
    {.from=60,.waveform=VVVF_WIDE3,.pulses=3,.amplitude=LINEAR(60,.7f,68,1),
     .angle_law={.maximum_degrees=60,.exponent=1}},
    {.from=67,.waveform=VVVF_ONE_PULSE,.pulses=1,.amplitude=CONSTANT(1)}
};
static const vvvf_profile_segment_t jr_brake[]={
    {.from=0,.waveform=VVVF_SPWM,.carrier=CONSTANT(1045),.amplitude=JR_BRAKE_AMPLITUDE},
    {.from=29,.waveform=VVVF_SPWM,.carrier=LINEAR(29,1045,55.9f,700),.amplitude=JR_BRAKE_AMPLITUDE},
    {.from=56,.waveform=VVVF_SPWM,.carrier=LINEAR(56,700,67,1600),.amplitude=JR_BRAKE_AMPLITUDE},
    {.from=67,.waveform=VVVF_WIDE3,.pulses=3,.amplitude=LINEAR(67,.7f,75,.85f),
     .angle_law={.maximum_degrees=60,.exponent=1}},
    {.from=73,.waveform=VVVF_ONE_PULSE,.pulses=1,.amplitude=CONSTANT(1)}
};

/* naoto64/VVVF-Simulator @9335c10, Toshiba PMSM user preset (MIT).
   Its x-axis is km/h, converted to Hz by hz_per_unit in the registry below.
   HFI and the original special SYNC9 waveform remain outside this port. */
#define PMSM_AMPLITUDE CLAMPED_POWER(0,0,80.12956810631229f,1,.92f)
static const vvvf_profile_segment_t pmsm_power[]={
    {.from=0,.waveform=VVVF_SPWM,.carrier=CONSTANT(750),.amplitude=PMSM_AMPLITUDE},
    {.from=35,.waveform=VVVF_SPWM,.pulses=9,.amplitude=PMSM_AMPLITUDE},
    {.from=60,.waveform=VVVF_WIDE3,.pulses=3,.amplitude=PMSM_AMPLITUDE,
     .angle_law={.maximum_degrees=38,.exponent=.72f}},
    {.from=80,.waveform=VVVF_ONE_PULSE,.pulses=1,.amplitude=CONSTANT(1)}
};
static const char *const pmsm_aliases[]={"pmsm-demo",NULL};

/* Append custom profiles here. Name lookup/counting need no evaluator edits.
   Omitted brake sequence means use the power sequence for braking too. */
const vvvf_profile_definition_t vvvf_profile_definitions[]={
    {.name="igbt",.input_unit="Hz",.hz_per_unit=1,
     .amplitude_scale=1,.amplitude_mode=VVVF_VOLTAGE_TARGET,.amplitude_limit=1,
     .power=SEQUENCE(igbt_power),.source="Original project IGBT table"},
    {.name="gto",.input_unit="Hz",.hz_per_unit=1,
     .amplitude_scale=1,.amplitude_mode=VVVF_VOLTAGE_TARGET,.amplitude_limit=1,
     .power=SEQUENCE(gto_power),.source="Original project GTO table"},
    {.name="jr-fall",.input_unit="Hz",.hz_per_unit=1,
     .amplitude_scale=1,.amplitude_mode=VVVF_RAW_REFERENCE,.amplitude_limit=1.4f,
     .power=SEQUENCE(jr_power),.brake=SEQUENCE(jr_brake),
     .source="VvvfGeeks/YAML-Files @2f83dc2 / JRE_E231_HITACHI.yaml"},
    {.name="pmsm-toshiba",.aliases=pmsm_aliases,.input_unit="km/h",
     .hz_per_unit=79.79401993355482f/80.12956810631229f,
     .amplitude_scale=1,.amplitude_mode=VVVF_RAW_REFERENCE,.amplitude_limit=1.4f,
     .power=SEQUENCE(pmsm_power),
     .source="naoto64/VVVF-Simulator @9335c10 / Toshiba PMSM user preset (MIT)"}
};
const size_t vvvf_profile_definition_count=COUNT(vvvf_profile_definitions);
