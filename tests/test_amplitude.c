#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "vvvf/config.h"
#include "vvvf/modulator.h"
static const double tau=6.283185307179586;
static void validate_frame(uint32_t before,const vvvf_command_t *c,const uint32_t *words,size_t used) {
    assert(used>0 && used<=VVVF_WORDS_PER_FRAME);
    unsigned ticks=0;
    for(size_t j=0;j<used;++j) {
        unsigned dt=(words[j]>>3)+3;
        if(dt>=8) {
            double phase=(before+(double)c->fundamental_step*(ticks+dt*.5)/1250)/4294967296.;
            double alpha=c->pulse_angle/4294967296.;
            const double offsets[]={1.0/3,2.0/3,0};
            for(unsigned bit=0;bit<3;++bit) {
                double x=fmod(phase+offsets[bit],1.0);
                bool expected=(x>=alpha && x<.5-alpha) || (x>=.5 && x<.5+alpha) || x>=1-alpha;
                assert(expected==!!(words[j]&(1u<<bit)));
            }
        }
        ticks+=dt;
    }
    assert(ticks==1250);
}
static void steady(vvvf_profile_t profile) {
    const float amplitudes[]={0,.01f,.1f,.25f,.5f,.7f,.8f,.9f,.99f,.99999f,.999999f,1,1.1f};
    uint32_t previous_angle=UINT32_MAX;
    for(unsigned k=0;k<sizeof(amplitudes)/sizeof(*amplitudes);++k) {
        float m=amplitudes[k];
        vvvf_command_t c=vvvf_command(65,profile);
        uint32_t original_step=c.fundamental_step;
        vvvf_command_set_amplitude(&c,m);
        assert(c.fundamental_step==original_step && c.enabled);
        assert(c.pulse_angle<=previous_angle);previous_angle=c.pulse_angle;
        double limited=fmin((double)m,1.0);
        double expected_angle=profile==VVVF_JR_FALL_SOUND ? (1-limited)/6 : pow(1-limited,.72)*38/360;
        assert(fabs(c.pulse_angle/4294967296.-expected_angle)<1e-7);
        /* Exactly one cycle per 1024 frames avoids DFT leakage. */
        c.fundamental_step=1u<<22;
        vvvf_modulator_t s={0};
        double re=0,im=0;unsigned long long tick=0;
        const double omega=6.283185307179586/(1024*1250);
        for(unsigned frame=0;frame<1024;++frame) {
            uint32_t words[VVVF_WORDS_PER_FRAME],before=s.fundamental;
            size_t used=vvvf_render_timed(&s,&c,words,1,1250);
            validate_frame(before,&c,words,used);
            for(size_t j=0;j<used;++j) {
                unsigned dt=(words[j]>>3)+3;
                int line=(int)((words[j]>>2)&1)-(int)((words[j]>>1)&1);
                re+=line*(sin(omega*(tick+dt))-sin(omega*tick))/omega;
                im+=line*(cos(omega*tick)-cos(omega*(tick+dt)))/omega;
                tick+=dt;
            }
        }
        assert(s.fundamental==0);
        double actual=4*hypot(re,im)/tick/sqrt(3);
        double expected=4/3.141592653589793*(2*cos(tau*expected_angle)-1);
        assert(fabs(actual-expected)<.0002);
        if(k==0 && profile==VVVF_JR_FALL_SOUND) assert(actual<.0002);
        /* Public PMSM source uses a 38-degree maximum, so its raw m=0 is NOT
           a general-purpose zero-voltage command. Normal wide3 entry is ~.77. */
        if(k==0 && profile==VVVF_PMSM_SOUND) assert(actual>.7);
    }
}
static void changing(vvvf_profile_t profile) {
    vvvf_command_t c=vvvf_command(65,profile);
    vvvf_modulator_t s={0};
    uint32_t step=c.fundamental_step;
    unsigned frames=0;
    for(unsigned block=0;block<512;++block) {
        float m=block<256 ? block/255.0f : (511-block)/255.0f;
        if(block==100) m=.999999f; /* near-collapse jump */
        if(block==101) m=.01f;     /* immediate restoration of narrow pulses */
        vvvf_command_set_amplitude(&c,m);
        assert(c.fundamental_step==step);
        for(unsigned i=0;i<256;++i) {
            uint32_t words[VVVF_WORDS_PER_FRAME],before=s.fundamental;
            size_t used=vvvf_render_timed(&s,&c,words,1,1250);
            validate_frame(before,&c,words,used);
            ++frames;
            assert(s.fundamental==(uint32_t)((uint64_t)frames*step));
        }
    }
}
int main(void) {
    vvvf_modulator_init();
    steady(VVVF_JR_FALL_SOUND);steady(VVVF_PMSM_SOUND);
    changing(VVVF_JR_FALL_SOUND);changing(VVVF_PMSM_SOUND);
    vvvf_command_t c=vvvf_command(75,VVVF_JR_FALL_SOUND);
    assert(c.waveform==VVVF_ONE_PULSE);
    vvvf_command_set_amplitude(&c,0);assert(c.pulse_angle==0);
    vvvf_command_set_amplitude(&c,1);assert(c.pulse_angle==0);
    vvvf_command_set_amplitude(&c,NAN);assert(c.requested_modulation==0);
    vvvf_command_set_amplitude(&c,-1);assert(c.requested_modulation==0);
    puts("PASS: amplitude sweep, pulse-angle/fundamental oracle, changing blocks, phase continuity, near-one collapse and one-pulse limits");
}
