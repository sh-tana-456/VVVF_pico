#include "vvvf/modulator.h"
#include "vvvf/programmed.h"
#include <math.h>
#define TABLE_BITS 11u
#define TABLE_SIZE (1u << TABLE_BITS)
static int16_t sine[TABLE_SIZE];
void vvvf_modulator_init(void) {
    for (unsigned i=0; i<TABLE_SIZE; ++i)
        sine[i] = (int16_t)(32767.0 * sin(6.283185307179586 * i / TABLE_SIZE));
}
/* xorshift32: deterministic reproducible noise, no division or float in IRQ.
   Hold time is counted in output frames, so DMA buffer boundaries do not matter. */
static uint32_t asynchronous_step(vvvf_modulator_t *s,const vvvf_command_t *c) {
    if(s->previous_random_deviation!=c->random_step_deviation || s->previous_random_hold!=c->random_hold_samples) {
        s->random_remaining=0;
        s->previous_random_deviation=c->random_step_deviation;
        s->previous_random_hold=c->random_hold_samples;
    }
    if (!c->random_step_deviation || !c->random_hold_samples) {
        s->random_remaining=0; s->random_offset=0;
        return c->carrier_step;
    }
    if (!s->random_remaining) {
        uint32_t x=s->random_state ? s->random_state : 0x6d2b79f5u;
        x^=x<<13; x^=x>>17; x^=x<<5; s->random_state=x;
        s->random_offset=(int32_t)(((int64_t)((int32_t)(x>>16)-32768)*c->random_step_deviation)/32768);
        s->random_remaining=c->random_hold_samples;
    }
    --s->random_remaining;
    return (uint32_t)((int64_t)c->carrier_step+s->random_offset);
}
uint32_t vvvf_sample(vvvf_modulator_t *s, const vvvf_command_t *c) {
    if (!c->enabled) {
        *s = (vvvf_modulator_t){0};
        return 0;
    }
    if (c->waveform!=VVVF_SPWM) {
        s->random_remaining=0;
        uint32_t pins=vvvf_programmed_pins(s->fundamental,c->pulse_angle);
        s->fundamental+=c->fundamental_step;
        return pins;
    }
    /* Fix the carrier/reference relation, independent of transition history.
       Falling carrier zero at fundamental zero gives symmetric odd-N PWM. */
    if (c->pulses) s->random_remaining=0;
    if (s->previous_pulses != c->pulses) {
        s->carrier_offset = 0xc0000000u;
        s->previous_pulses = c->pulses;
    }
    uint32_t carrier = c->pulses ? s->fundamental * c->pulses + s->carrier_offset : s->carrier;
    uint32_t folded = carrier < 0x80000000u ? carrier : ~carrier;
    int32_t triangle = (int32_t)(folded >> 15) - 32768;
    uint32_t output = 0;
    /* Reverse direction retained from the original: V=theta+240, W=theta+120. */
    const uint32_t offsets[3] = {0x55555555u, 0xaaaaaaaau, 0};
    for (unsigned bit=0; bit<3; ++bit) {
        unsigned index = (s->fundamental + offsets[bit]) >> (32-TABLE_BITS);
        int32_t reference = ((int32_t)sine[index] * c->modulation) / 32768;
        if (reference > triangle) output |= 1u << bit;
    }
    s->fundamental += c->fundamental_step;
    s->carrier = c->pulses ? s->fundamental * c->pulses + s->carrier_offset : carrier + asynchronous_step(s,c);
    return output;
}
void vvvf_render(vvvf_modulator_t *s, const vvvf_command_t *c, uint32_t *out, size_t n) {
    for (size_t i=0; i<n; ++i) out[i] = vvvf_sample(s,c);
}

/* Interpolate comparator crossings within each control frame. The reference
   is piecewise linear over 10 us; the carrier is split at every peak/valley.
   Only crossings/cusps require division, not every output tick. */
typedef struct { uint32_t tick, mask; bool high; } edge_t;
static void differences(uint32_t phase, uint32_t carrier, uint16_t modulation,
                        int32_t result[3]) {
    uint32_t folded = carrier < 0x80000000u ? carrier : ~carrier;
    int32_t triangle = (int32_t)(folded >> 15)-32768;
    const uint32_t offsets[3] = {0x55555555u,0xaaaaaaaau,0};
    for (unsigned b=0;b<3;++b) {
        unsigned i = (phase+offsets[b]) >> (32-TABLE_BITS);
        result[b] = ((int32_t)sine[i]*modulation)/32768-triangle;
    }
}
static unsigned crossings(edge_t *edges, unsigned n, uint32_t begin, uint32_t end,
                           const int32_t a[3], const int32_t b[3]) {
    for (unsigned bit=0;bit<3;++bit) {
        if ((a[bit]>0) == (b[bit]>0)) continue;
        uint32_t left = a[bit]<0 ? (uint32_t)-a[bit] : (uint32_t)a[bit];
        uint32_t right = b[bit]<0 ? (uint32_t)-b[bit] : (uint32_t)b[bit];
        uint32_t divisor = left+right;
        uint32_t t = begin+(uint32_t)(((uint64_t)left*(end-begin)+divisor/2)/divisor);
        edges[n++] = (edge_t){t,1u<<bit,b[bit]>0};
    }
    return n;
}
static size_t timed_frame(vvvf_modulator_t *s, const vvvf_command_t *c,
                          uint32_t *out, uint32_t ticks) {
    if (!c->enabled) {
        *s = (vvvf_modulator_t){0};
        out[0] = (ticks-3)<<3;
        return 1;
    }
    if (c->waveform!=VVVF_SPWM) {
        s->random_remaining=0;
        size_t words=vvvf_programmed_frame(s->fundamental,c->fundamental_step,
                                           c->pulse_angle,out,ticks);
        s->fundamental+=c->fundamental_step;
        return words;
    }
    if (c->pulses) s->random_remaining=0;
    if (s->previous_pulses != c->pulses) {
        s->carrier_offset = 0xc0000000u;
        s->previous_pulses = c->pulses;
    }
    uint32_t carrier = c->pulses ? s->fundamental*c->pulses+s->carrier_offset : s->carrier;
    uint32_t delta = c->pulses ? c->fundamental_step*c->pulses : asynchronous_step(s,c);
    int32_t a[3],b[3];
    differences(s->fundamental,carrier,c->modulation,a);
    differences(s->fundamental+c->fundamental_step,carrier+delta,c->modulation,b);
    edge_t edges[6];
    unsigned n=0;
    uint32_t to_turn = 0x80000000u-(carrier & 0x7fffffffu);
    if (delta && to_turn < delta) {
        uint32_t middle_tick = (uint32_t)(((uint64_t)to_turn*ticks)/delta);
        uint32_t middle_phase = s->fundamental+(uint32_t)(((uint64_t)c->fundamental_step*to_turn)/delta);
        int32_t middle[3];
        differences(middle_phase,carrier+to_turn,c->modulation,middle);
        n = crossings(edges,n,0,middle_tick,a,middle);
        n = crossings(edges,n,middle_tick,ticks,middle,b);
    } else n = crossings(edges,n,0,ticks,a,b);
    for (unsigned i=1;i<n;++i) {
        edge_t e=edges[i]; unsigned j=i;
        while (j && edges[j-1].tick>e.tick) {edges[j]=edges[j-1];--j;}
        edges[j]=e;
    }
    uint32_t pins=0,last=0;
    for (unsigned bit=0;bit<3;++bit) if (a[bit]>0) pins |= 1u<<bit;
    size_t words=0;
    for (unsigned i=0;i<n;++i) {
        uint32_t t=edges[i].tick;
        if (t-last>=3) {
            out[words++] = ((t-last-3)<<3)|pins;
            last=t;
        }
        /* Pulses shorter than the three-instruction PIO loop are merged. */
        pins = edges[i].high ? pins|edges[i].mask : pins & ~edges[i].mask;
    }
    if (ticks-last>=3) out[words++] = ((ticks-last-3)<<3)|pins;
    else {
        /* Extend the previous run to preserve exactly ticks/frame. */
        out[words-1] += (ticks-last)<<3;
    }
    s->fundamental += c->fundamental_step;
    s->carrier = carrier+delta;
    return words;
}
size_t vvvf_render_timed(vvvf_modulator_t *s, const vvvf_command_t *c,
                         uint32_t *out, size_t count, uint32_t ticks) {
    size_t used=0;
    for (size_t i=0;i<count;++i) used += timed_frame(s,c,out+used,ticks);
    return used;
}
