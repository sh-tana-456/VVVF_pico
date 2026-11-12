#include "vvvf/profiles.h"
#include "vvvf/config.h"
#include <math.h>
#include <string.h>

size_t vvvf_profile_count(void) { return vvvf_profile_definition_count; }
const vvvf_profile_definition_t *vvvf_profile_definition(vvvf_profile_t id) {
    return id<vvvf_profile_count() ? &vvvf_profile_definitions[id] : NULL;
}
const char *vvvf_profile_name(vvvf_profile_t id) {
    const vvvf_profile_definition_t *p=vvvf_profile_definition(id);
    return p ? p->name : "invalid";
}
bool vvvf_profile_from_name(const char *name, vvvf_profile_t *profile) {
    if (!name || !profile) return false;
    for (size_t i=0;i<vvvf_profile_count();++i) {
        const vvvf_profile_definition_t *p=&vvvf_profile_definitions[i];
        if (!strcmp(name,p->name)) { *profile=i; return true; }
        if (p->aliases) for(size_t j=0;p->aliases[j];++j)
            if (!strcmp(name,p->aliases[j])) { *profile=i; return true; }
    }
    return false;
}
const char *vvvf_waveform_name(vvvf_waveform_t waveform) {
    switch(waveform) {
        case VVVF_SPWM: return "spwm";
        case VVVF_WIDE3: return "wide3";
        case VVVF_ONE_PULSE: return "one-pulse";
        default: return "invalid";
    }
}
float vvvf_curve_evaluate(const vvvf_curve_t *c, float x) {
    if (!c || !isfinite(x)) return NAN;
    if (c->kind==VVVF_CURVE_CONSTANT) return c->y0;
    if (!isfinite(c->x0) || !isfinite(c->x1) || c->x1<=c->x0) return NAN;
    float t=(x-c->x0)/(c->x1-c->x0);
    if (c->clamp_input) t=fminf(fmaxf(t,0),1);
    switch(c->kind) {
        case VVVF_CURVE_LINEAR:
            /* Keep source endpoints independent of segment thresholds. */
            return c->clamp_input ? c->y0+(c->y1-c->y0)*t :
                c->y0+(c->y1-c->y0)*(x-c->x0)/(c->x1-c->x0);
        case VVVF_CURVE_POWER:
            if (!isfinite(c->exponent) || c->exponent<=0 || t<0) return NAN;
            return c->y0+(c->y1-c->y0)*powf(t,c->exponent);
        default: return NAN;
    }
}
static bool curve_valid(const vvvf_curve_t *c) {
    if (!isfinite(c->y0)) return false;
    if (c->kind==VVVF_CURVE_CONSTANT) return true;
    if (c->kind!=VVVF_CURVE_LINEAR && c->kind!=VVVF_CURVE_POWER) return false;
    return isfinite(c->y1) && isfinite(c->x0) && isfinite(c->x1) && c->x1>c->x0 &&
        (c->kind!=VVVF_CURVE_POWER || (isfinite(c->exponent) && c->exponent>0));
}
static bool sequence_valid(const vvvf_profile_definition_t *p,vvvf_profile_sequence_t seq) {
    if (!seq.segments || !seq.count || seq.segments[0].from!=0) return false;
    float maximum=VVVF_MAX_HZ/p->hz_per_unit;
    for (size_t i=0;i<seq.count;++i) {
        const vvvf_profile_segment_t *s=&seq.segments[i];
        if (!isfinite(s->from) || s->from<0 || (i && s->from<=seq.segments[i-1].from)) return false;
        if (!curve_valid(&s->amplitude)) return false;
        const vvvf_random_carrier_t *r=&s->random_carrier;
        if (!isfinite(r->deviation_hz) || r->deviation_hz<0) return false;
        if (r->deviation_hz>0 && (s->waveform!=VVVF_SPWM || s->pulses ||
            !isfinite(r->update_hz) || r->update_hz<1 || r->update_hz>VVVF_SAMPLE_HZ)) return false;
        float end=i+1<seq.count ? seq.segments[i+1].from : maximum;
        if (s->from>maximum) continue;
        if (end>maximum) end=maximum;
        float a=vvvf_curve_evaluate(&s->amplitude,s->from),b=vvvf_curve_evaluate(&s->amplitude,end);
        if (!isfinite(a) || !isfinite(b) || a<0 || b<0) return false;
        switch(s->waveform) {
            case VVVF_SPWM:
                if (!s->pulses) {
                    if (!curve_valid(&s->carrier)) return false;
                    float first=vvvf_curve_evaluate(&s->carrier,s->from);
                    float last=vvvf_curve_evaluate(&s->carrier,end);
                    if (!(first>r->deviation_hz && last>r->deviation_hz && first+r->deviation_hz<VVVF_SAMPLE_HZ/2 && last+r->deviation_hz<VVVF_SAMPLE_HZ/2)) return false;
                } else if (end*p->hz_per_unit*s->pulses>=VVVF_SAMPLE_HZ/2) return false;
                break;
            case VVVF_WIDE3:
                if (s->pulses!=3 || !isfinite(s->angle_law.maximum_degrees) ||
                    s->angle_law.maximum_degrees<0 || s->angle_law.maximum_degrees>60 ||
                    !isfinite(s->angle_law.exponent) || s->angle_law.exponent<=0) return false;
                break;
            case VVVF_ONE_PULSE:
                if (s->pulses!=1) return false;
                break;
            default: return false;
        }
    }
    return true;
}
bool vvvf_profile_validate(const vvvf_profile_definition_t *p) {
    if (!p || !p->name || !p->name[0] || !isfinite(p->hz_per_unit) || p->hz_per_unit<=0 ||
        !isfinite(p->amplitude_scale) || p->amplitude_scale<0 ||
        !isfinite(p->amplitude_limit) || p->amplitude_limit<=0 || p->amplitude_limit>65535.0f/32767.0f ||
        (p->amplitude_mode!=VVVF_VOLTAGE_TARGET && p->amplitude_mode!=VVVF_RAW_REFERENCE) ||
        (p->amplitude_mode==VVVF_VOLTAGE_TARGET && p->amplitude_limit>1)) return false;
    if (!sequence_valid(p,p->power)) return false;
    if (!p->brake.count) return p->brake.segments==NULL;
    return sequence_valid(p,p->brake);
}
vvvf_pattern_t vvvf_profile_evaluate(const vvvf_profile_definition_t *p,float hz,bool braking) {
    vvvf_pattern_t result={0};
    if (!p || !isfinite(p->hz_per_unit) || p->hz_per_unit<=0 ||
        !isfinite(p->amplitude_scale) || p->amplitude_scale<0 ||
        !isfinite(p->amplitude_limit) || p->amplitude_limit<=0 || p->amplitude_limit>65535.0f/32767.0f ||
        (p->amplitude_mode!=VVVF_VOLTAGE_TARGET && p->amplitude_mode!=VVVF_RAW_REFERENCE) ||
        (p->amplitude_mode==VVVF_VOLTAGE_TARGET && p->amplitude_limit>1)) return result;
    if (!isfinite(hz) || hz<0) hz=0;
    if (hz>VVVF_MAX_HZ) hz=VVVF_MAX_HZ;
    float x=hz/p->hz_per_unit;
    vvvf_profile_sequence_t seq=braking && p->brake.count ? p->brake : p->power;
    if (!seq.segments || !seq.count) return result;
    size_t i=0;
    while (i+1<seq.count && x>=seq.segments[i+1].from) ++i;
    const vvvf_profile_segment_t *s=&seq.segments[i];
    result.amplitude=vvvf_curve_evaluate(&s->amplitude,x);
    result.waveform=s->waveform;
    result.pulses=s->pulses;
    result.carrier_hz=s->waveform==VVVF_SPWM ?
        (s->pulses ? hz*s->pulses : vvvf_curve_evaluate(&s->carrier,x)) : 0;
    result.amplitude_mode=p->amplitude_mode;
    result.amplitude_limit=p->amplitude_limit;
    result.amplitude_scale=p->amplitude_scale;
    result.random_carrier=s->random_carrier;
    result.angle_law=s->angle_law;
    result.valid=isfinite(result.amplitude) && result.amplitude>=0 && isfinite(result.carrier_hz);
    if (s->waveform==VVVF_SPWM)
        result.valid=result.valid && result.carrier_hz<VVVF_SAMPLE_HZ/2 &&
            (s->pulses ? result.carrier_hz>=0 : result.carrier_hz>0);
    else if (s->waveform==VVVF_WIDE3)
        result.valid=result.valid && s->pulses==3 && isfinite(s->angle_law.maximum_degrees) &&
            s->angle_law.maximum_degrees>=0 && s->angle_law.maximum_degrees<=60 &&
            isfinite(s->angle_law.exponent) && s->angle_law.exponent>0;
    else if (s->waveform==VVVF_ONE_PULSE) result.valid=result.valid && s->pulses==1;
    else result.valid=false;
    const vvvf_random_carrier_t *r=&s->random_carrier;
    result.valid=result.valid && isfinite(r->deviation_hz) && r->deviation_hz>=0;
    if(r->deviation_hz>0) result.valid=result.valid && s->waveform==VVVF_SPWM && !s->pulses &&
        isfinite(r->update_hz) && r->update_hz>=1 && r->update_hz<=VVVF_SAMPLE_HZ &&
        result.carrier_hz>r->deviation_hz && result.carrier_hz+r->deviation_hz<VVVF_SAMPLE_HZ/2;
    return result.valid ? result : (vvvf_pattern_t){0};
}
vvvf_pattern_t vvvf_pattern(float hz,vvvf_profile_t profile) {
    return vvvf_pattern_direction(hz,profile,false);
}
vvvf_pattern_t vvvf_pattern_direction(float hz,vvvf_profile_t profile,bool braking) {
    const vvvf_profile_definition_t *p=vvvf_profile_definition(profile);
    /* A malformed ID produces a disabled command, not an unrelated preset. */
    return vvvf_profile_evaluate(p,hz,braking);
}
