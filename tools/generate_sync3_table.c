#include <stdio.h>
#include <math.h>
#include "vvvf/modulator.h"
static double gain(unsigned m) {
 vvvf_command_t c=vvvf_command(100,VVVF_IGBT);c.pulses=3;c.modulation=m;
 vvvf_modulator_t s={0};
 double re=0,im=0;unsigned long long t=0;
 const double omega=6.283185307179586*100/125000000.;
 for(unsigned frame=0;frame<1000;++frame) {
  uint32_t words[8];size_t n=vvvf_render_timed(&s,&c,words,1,1250);
  for(size_t j=0;j<n;++j) {
   unsigned dt=(words[j]>>3)+3;
   int v=(int)((words[j]>>2)&1)-(int)((words[j]>>1)&1);
   re+=v*(sin(omega*(t+dt))-sin(omega*t))/omega;
   im+=v*(cos(omega*t)-cos(omega*(t+dt)))/omega;t+=dt;
  }
 }
 return 4*hypot(re,im)/t/sqrt(3);
}
int main(void) {
 vvvf_modulator_init();puts("/* Q15 reference for requested fundamental 0,1/32,...,1. */");
 for(unsigned i=0;i<=32;++i) {
  unsigned low=0,high=32767;
  while(high-low>1) {unsigned mid=(low+high)/2;if(gain(mid)<i/32.) low=mid;else high=mid;}
  printf("%u%s",i ? high : 0,i==32 ? "\n" : ", ");
 }
}
