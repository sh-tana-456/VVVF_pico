/* Reference/preset provenance and licensing scope: THIRD_PARTY_NOTICES.md.
 * JR reference permissions remain unconfirmed; see Doc/license-audit.md.
 */
#include "vvvf/programmed.h"
/* The three high intervals are [a,pi-a), [pi,pi+a), [2pi-a,2pi).
   a=0 reduces exactly to the positive-half-cycle one-pulse waveform. */
static bool level(uint32_t phase, uint32_t angle) {
    uint32_t half = phase & 0x7fffffffu;
    bool center = half >= angle && half < 0x80000000u-angle;
    return center != (phase >= 0x80000000u);
}
static const uint32_t offsets[3] = {0x55555555u,0xaaaaaaaau,0};
uint32_t vvvf_programmed_pins(uint32_t phase, uint32_t angle) {
    uint32_t pins=0;
    for (unsigned b=0;b<3;++b) if(level(phase+offsets[b],angle)) pins|=1u<<b;
    return pins;
}
size_t vvvf_programmed_frame(uint32_t phase, uint32_t step, uint32_t angle,
                             uint32_t *out, uint32_t ticks) {
    typedef struct { uint32_t tick,mask; bool high; } edge_t;
    edge_t edges[18]; unsigned n=0;
    const uint32_t boundaries[6]={0,angle,0x80000000u-angle,0x80000000u,
                                  0x80000000u+angle,0u-angle};
    for (unsigned b=0;b<3;++b) {
        uint32_t p=phase+offsets[b];
        for (unsigned j=0;j<6;++j) {
            uint32_t distance=boundaries[j]-p;
            if (!distance || distance>=step) continue;
            edges[n++]=(edge_t){(uint32_t)(((uint64_t)distance*ticks+step/2)/step),
                                1u<<b,level(boundaries[j],angle)};
        }
    }
    for (unsigned i=1;i<n;++i) {
        edge_t e=edges[i];unsigned j=i;
        while(j && edges[j-1].tick>e.tick) {edges[j]=edges[j-1];--j;}
        edges[j]=e;
    }
    uint32_t pins=vvvf_programmed_pins(phase,angle),last=0;size_t used=0;
    for (unsigned i=0;i<n;++i) {
        uint32_t t=edges[i].tick;
        if (t-last>=3) {out[used++]=((t-last-3)<<3)|pins;last=t;}
        pins=edges[i].high ? pins|edges[i].mask : pins & ~edges[i].mask;
    }
    if(ticks-last>=3) out[used++]=((ticks-last-3)<<3)|pins;
    else out[used-1]+=(ticks-last)<<3;
    return used;
}
