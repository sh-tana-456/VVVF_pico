#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "vvvf/config.h"
#include "vvvf/modulator.h"
#include "vvvf/voltage.h"
#include "vvvf/programmed.h"
static void commands(void) {
    assert(!vvvf_command(NAN,VVVF_IGBT).enabled);
    assert(!vvvf_command(-1,VVVF_IGBT).enabled);
    assert(vvvf_command(39.99f,VVVF_IGBT).pulses == 0);
    assert(vvvf_command(40,VVVF_IGBT).pulses == 9);
    assert(vvvf_command(50,VVVF_IGBT).pulses == 3);
    assert(vvvf_command(46,VVVF_GTO).pulses == 15);
    assert(vvvf_command(60,VVVF_GTO).pulses == 9);
    assert(vvvf_command(70,VVVF_GTO).pulses == 3);
    assert(vvvf_command(1000,VVVF_IGBT).fundamental_step == vvvf_command(110,VVVF_IGBT).fundamental_step);
    assert(vvvf_ramp(0,true,false,0.01f) > 0.049f);
    assert(vvvf_ramp(10,true,true,0.01f) == 10);
    assert(vvvf_ramp(0,false,true,0.01f) == 0);
    assert(vvvf_ramp(110,true,false,0.01f) == 110);
}
static void periodicity(void) {
    /* Exactly representable phase increment: one fundamental period=2048 samples. */
    for (unsigned n=3; n<=15; n+=2) {
        vvvf_command_t c = {.enabled=true,.fundamental_step=1u<<21,.modulation=28000,.pulses=n};
        vvvf_modulator_t s = {0};
        uint32_t first[2048], second[2048];
        vvvf_render(&s,&c,first,2048);
        assert(s.fundamental == 0 && s.carrier == s.carrier_offset);
        vvvf_render(&s,&c,second,2048);
        unsigned rises=0;
        uint32_t prev_carrier = 0;
        for (unsigned i=0; i<2048; ++i) {
            assert(first[i] == second[i]);
            assert(first[i] <= 7);
            uint32_t carrier = (i*(1u<<21))*n;
            if (carrier < prev_carrier) ++rises;
            prev_carrier = carrier;
        }
        assert(rises == n-1); /* final wrap is at sample 2048 */
    }
}
static void oracle_and_balance(void) {
    /* Compare actual async PWM against an independent floating point model.
       LUT/quantisation differences are permitted only near switching edges. */
    vvvf_command_t c = vvvf_command(20,VVVF_IGBT);
    vvvf_modulator_t s = {0};
    unsigned mismatch=0, high[3]={0};
    double re[3]={0}, im[3]={0};
    const double tau=6.283185307179586;
    for (unsigned i=0; i<VVVF_SAMPLE_HZ; ++i) {
        double theta = tau*s.fundamental/4294967296.0;
        double p = s.carrier/4294967296.0;
        double tri = (p<0.5 ? 4*p : 4*(1-p))-1;
        uint32_t bits=vvvf_sample(&s,&c);
        const double offset[3]={tau/3,2*tau/3,0};
        for (unsigned b=0;b<3;++b) {
            bool actual = bits & (1u<<b);
            bool expected = sin(theta+offset[b])*(c.modulation/32768.0)>tri;
            mismatch += actual != expected;
            high[b] += actual;
            re[b] += (actual ? 1 : -1)*cos(theta);
            im[b] += (actual ? 1 : -1)*sin(theta);
        }
    }
    assert(mismatch < 1000);
    for (unsigned b=0;b<3;++b) {
        assert(high[b] > 49000 && high[b] < 51000); /* centered, no MR-scaled DC */
        assert(hypot(re[b],im[b]) > 8000);
    }
    for (unsigned b=0;b<2;++b) {
        double diff = atan2(im[b],re[b])-atan2(im[2],re[2]);
        assert(fabs(cos(diff)+0.5) < 0.04); /* 120/240 degree separation */
    }
}
static void transitions_and_stop(void) {
    vvvf_modulator_t s={0};
    const float sweep[]={0,1,39.99f,40,49.99f,50,110,50,49.99f,40,39.99f,0};
    for (unsigned k=0;k<sizeof(sweep)/sizeof(*sweep);++k) {
        vvvf_command_t c=vvvf_command(sweep[k],VVVF_IGBT);
        vvvf_sample(&s,&c);
        if (c.enabled && c.pulses) {
            assert(s.carrier == s.fundamental*c.pulses+0xc0000000u);
        }
        for (unsigned i=0;i<10000;++i) {
            uint32_t out=vvvf_sample(&s,&c);
            assert(out<=7);
            if (!c.enabled) assert(out==0 && s.fundamental==0);
        }
    }
}
static void timed_waveform(void) {
    const unsigned ticks=1250;
    uint32_t words[VVVF_WORDS_PER_FRAME];
    /* All modes/turn positions must preserve frame time and storage bounds. */
    const float hz_values[]={0,.05f,.1f,.5f,1,20,39.99f,40,49.99f,50,55,100,110,0};
    vvvf_modulator_t state={0};
    for (unsigned k=0;k<sizeof(hz_values)/sizeof(*hz_values);++k) {
        vvvf_command_t c=vvvf_command(hz_values[k],VVVF_IGBT);
        for (unsigned frame=0;frame<10000;++frame) {
            size_t n=vvvf_render_timed(&state,&c,words,1,ticks);
            assert(n>=1 && n<=VVVF_WORDS_PER_FRAME);
            unsigned total=0;
            for (size_t j=0;j<n;++j) {
                total+=(words[j]>>3)+3;
                if (!c.enabled) assert((words[j]&7)==0);
            }
            assert(total==ticks);
        }
    }
    /* At very low modulation, differential pulse area must be tens of ticks,
       rather than the old 1250-tick (10 us) quantisation floor. */
    for (unsigned m=16;m<=32767;m = m==16 ? 328 : m==328 ? 32767 : 32768) {
        vvvf_command_t c=vvvf_command(1,VVVF_IGBT);
        c.random_step_deviation=0; /* Fixed-carrier pulse-area oracle. */
        c.fundamental_step=0; c.modulation=m;
        state=(vvvf_modulator_t){0};
        long long high[3]={0};
        for (unsigned f=0;f<100;++f) {
            size_t n=vvvf_render_timed(&state,&c,words,1,ticks);
            for (size_t j=0;j<n;++j) {
                unsigned t=(words[j]>>3)+3;
                for (unsigned b=0;b<3;++b) if(words[j]&(1u<<b)) high[b]+=t;
            }
        }
        double expected=125000.0*0.5*0.866025403784*m/32768.0;
        assert(fabs((high[2]-high[1])-expected)<100);
        if(m==16) assert(high[2]-high[1]>15 && high[2]-high[1]<40);
    }
}
static void synchronous_voltage(void) {
    const float requested[]={.5f,.55f,.7f,1.0f};
    const double omega=6.283185307179586*100/125000000.;
    for (unsigned k=0;k<sizeof(requested)/sizeof(*requested);++k) {
        vvvf_command_t c=vvvf_command(requested[k]*100,VVVF_IGBT);
        c.fundamental_step=vvvf_command(100,VVVF_IGBT).fundamental_step;
        vvvf_modulator_t s={0};
        double re=0,im=0;unsigned long long t=0;
        for (unsigned f=0;f<1000;++f) {
            uint32_t words[VVVF_WORDS_PER_FRAME];
            size_t n=vvvf_render_timed(&s,&c,words,1,1250);
            for(size_t j=0;j<n;++j) {
                unsigned dt=(words[j]>>3)+3;
                int level=(int)((words[j]>>2)&1)-(int)((words[j]>>1)&1);
                re+=level*(sin(omega*(t+dt))-sin(omega*t))/omega;
                im+=level*(cos(omega*t)-cos(omega*(t+dt)))/omega;
                t+=dt;
            }
        }
        double effective=4*hypot(re,im)/t/sqrt(3);
        assert(fabs(effective-requested[k])<0.003);
    }
}
static void audio_profiles(void) {
    vvvf_profile_t profile=VVVF_GTO;
    assert(vvvf_profile_from_name("jr-fall",&profile) && profile==VVVF_JR_FALL_SOUND);
    assert(vvvf_profile_from_name("pmsm-demo",&profile) && profile==VVVF_PMSM_SOUND);
    assert(!vvvf_profile_from_name("unknown",&profile));
    assert(!vvvf_profile_from_name(NULL,&profile));
    assert(vvvf_pattern(0,VVVF_JR_FALL_SOUND).carrier_hz==1045);
    assert(vvvf_pattern(23,VVVF_JR_FALL_SOUND).carrier_hz==1045);
    assert(fabsf(vvvf_pattern(48.9f,VVVF_JR_FALL_SOUND).carrier_hz-710)<.01f);
    assert(vvvf_pattern(49,VVVF_JR_FALL_SOUND).carrier_hz==710);
    assert(vvvf_pattern(60,VVVF_JR_FALL_SOUND).waveform==VVVF_WIDE3);
    assert(vvvf_pattern(67,VVVF_JR_FALL_SOUND).waveform==VVVF_ONE_PULSE);
    assert(vvvf_pattern(30,VVVF_PMSM_SOUND).carrier_hz==750);
    assert(vvvf_pattern_direction(56,VVVF_JR_FALL_SOUND,true).carrier_hz==700);
    assert(vvvf_pattern_direction(67,VVVF_JR_FALL_SOUND,true).waveform==VVVF_WIDE3);
    assert(vvvf_pattern_direction(73,VVVF_JR_FALL_SOUND,true).waveform==VVVF_ONE_PULSE);
    assert(vvvf_voltage_reference(65535,3)==vvvf_voltage_reference(32767,3));
    assert(vvvf_voltage_reference(32767,9)==32767);
    uint32_t words[VVVF_WORDS_PER_FRAME];
    for (unsigned p=0;p<vvvf_profile_count();++p) {
        vvvf_modulator_t state={0};
        for (unsigned h=0;h<=11000;++h) {
            float hz=h*.01f;
            vvvf_command_t c=vvvf_command(hz,(vvvf_profile_t)p);
            vvvf_pattern_t pattern=vvvf_pattern(hz,(vvvf_profile_t)p);
            assert(pattern.waveform==VVVF_SPWM ? (pattern.carrier_hz>0 && pattern.carrier_hz<=3000) : pattern.carrier_hz==0);
            assert(c.pulses==1 || !c.pulses || (c.pulses%3==0 && c.pulses%2==1));
            assert(c.modulation<=45875);
            size_t n=vvvf_render_timed(&state,&c,words,1,1250);
            assert(n>=1 && n<=VVVF_WORDS_PER_FRAME);
            unsigned duration=0;
            for (size_t j=0;j<n;++j) duration+=(words[j]>>3)+3;
            assert(duration==1250);
        }
    }
}
static void programmed_patterns(void) {
    /* Independent continuous-angle oracle, checked inside each timed run. */
    const float frequencies[]={60,60.01f,63,66.99f,67,80,110};
    for(unsigned k=0;k<sizeof(frequencies)/sizeof(*frequencies);++k) {
        vvvf_command_t c=vvvf_command(frequencies[k],VVVF_JR_FALL_SOUND);
        vvvf_modulator_t state={0};
        for(unsigned frame=0;frame<3000;++frame) {
            uint32_t words[VVVF_WORDS_PER_FRAME];uint32_t before=state.fundamental;
            size_t used=vvvf_render_timed(&state,&c,words,1,1250);
            assert(used>=1 && used<=VVVF_WORDS_PER_FRAME);
            unsigned tick=0;
            for(size_t j=0;j<used;++j) {
                unsigned dt=(words[j]>>3)+3;
                /* Midpoint is away from all switching boundaries by >=1 tick. */
                double phase=(before+(double)c.fundamental_step*(tick+dt*.5)/1250)/4294967296.;
                double alpha=c.pulse_angle/4294967296.;
                const double offsets[]={1.0/3,2.0/3,0};
                for(unsigned bit=0;bit<3;++bit) {
                    double x=fmod(phase+offsets[bit],1.0);
                    bool expected=(x>=alpha && x<.5-alpha) || (x>=.5 && x<.5+alpha) || x>=1-alpha;
                    bool actual=words[j]&(1u<<bit);
                    /* Quantized edge placement can move a very narrow pulse. */
                    if(dt>=8) assert(expected==actual);
                }
                tick+=dt;
            }
            assert(tick==1250);
        }
    }
    /* Verify the Hitachi angle form against the independent sine/triangle
       comparison represented by the source SYNC3 Alt1 parameters. */
    vvvf_command_t source=vvvf_command(65,VVVF_JR_FALL_SOUND);
    double m=.7+(65.0-60)/8*.3;
    for(unsigned i=1;i<4096;++i) {
        if(i==2048) continue;
        double theta=6.283185307179586*i/4096;
        double saw=-2/3.141592653589793*asin(sin(theta));
        double carrier=saw>0 ? saw-1 : saw+1;
        double reference=(sin(theta)>0 ? 1 : -1)*(2*m/3+1.0/3);
        bool expected=reference>carrier;
        bool actual=vvvf_programmed_pins(i*(1u<<20),source.pulse_angle)&4;
        if(fabs(reference-carrier)>.001) assert(expected==actual);
    }
    /* Zero angle is a square wave, not a one-carrier sine comparison. */
    for(unsigned i=0;i<2048;++i) {
        uint32_t p=i*(1u<<21);
        assert(((vvvf_programmed_pins(p,0)>>2)&1)==(p<0x80000000u));
    }
}
static void random_carrier_and_gain(void) {
    vvvf_command_t c=vvvf_command(20,VVVF_IGBT);
    assert(c.random_hold_samples==100 && c.random_step_deviation>0);
    vvvf_modulator_t a={0},b={0};
    uint32_t words[VVVF_WORDS_PER_FRAME],initial=0;
    bool changed=false;
    double sum=0;
    for(unsigned i=0;i<100000;++i) {
        uint32_t previous=a.carrier;
        vvvf_sample(&a,&c);
        size_t n=vvvf_render_timed(&b,&c,words,1,1250);
        unsigned ticks=0;
        for(size_t j=0;j<n;++j) ticks+=(words[j]>>3)+3;
        assert(ticks==1250 && a.carrier==b.carrier && a.fundamental==b.fundamental);
        uint32_t delta=a.carrier-previous;
        assert(delta>=c.carrier_step-c.random_step_deviation && delta<=c.carrier_step+c.random_step_deviation);
        if(i%100==0) { if(i && initial!=delta) changed=true; initial=delta; }
        else assert(delta==initial);
        sum+=delta;
    }
    assert(changed && fabs(sum/100000-c.carrier_step)<c.random_step_deviation*.1);
    c=vvvf_command(40,VVVF_IGBT);
    assert(!c.random_step_deviation);
    vvvf_sample(&a,&c);
    assert(a.carrier==a.fundamental*9+0xc0000000u);
    vvvf_profile_definition_t p=*vvvf_profile_definition(VVVF_JR_FALL_SOUND);
    p.amplitude_scale=.5f;
    c=vvvf_command_from_profile(65,&p,false);
    assert(abs((int)c.requested_modulation-(int)(.8875*.5*32767))<=1);
    assert(fabs(c.pulse_angle*(360./4294967296.)-33.375)<.0001);
    vvvf_command_set_amplitude(&c,.8f);
    assert(abs((int)c.requested_modulation-(int)(.4*32767))<=1);
    p.amplitude_scale=0;c=vvvf_command_from_profile(65,&p,false);
    assert(c.requested_modulation==0);
    p.amplitude_scale=2;c=vvvf_command_from_profile(65,&p,false);
    assert(c.requested_modulation==(uint16_t)(1.4f*32767+.5f));
    p.amplitude_scale=NAN;assert(!vvvf_profile_validate(&p));
    assert(!vvvf_command_from_profile(65,&p,false).enabled);
    puts("PASS: random carrier bounds/hold/mean, preview-timed agreement, sync exclusion, amplitude scaling/limit/angles");
}
int main(void) {
    vvvf_modulator_init();
    random_carrier_and_gain();
    commands(); periodicity(); oracle_and_balance(); transitions_and_stop(); timed_waveform(); synchronous_voltage(); audio_profiles(); programmed_patterns();
    puts("PASS: bands, limits, synchronous periodicity, PWM oracle, three phases, transitions, stop, timed edges, low modulation, sync voltage, sourced profiles, programmed-pattern oracle");
}
