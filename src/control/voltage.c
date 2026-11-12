#include "vvvf/voltage.h"

/* Inverse of the natural-sampled N=3 fundamental gain for the fixed falling
   carrier zero at theta=0. See tools/generate_sync3_table.c and measurements.
   Input is desired line fundamental normalized by sqrt(3)/2 of DC bus. */
static const uint16_t sync3_reference[33] = {
    0,1012,2002,2969,3914,4842,5753,6647,7527,8394,9240,
    10081,10911,11729,12537,13337,14131,14914,15694,16466,
    17227,17989,18748,19500,20250,20999,21745,22487,23230,
    23973,24714,25451,26194
};
uint16_t vvvf_voltage_reference(uint16_t requested, uint8_t pulses) {
    if (requested > 32767u) requested = 32767u;
    if (pulses != 3) return requested;
    unsigned i = requested >> 10, fraction = requested & 1023u;
    return sync3_reference[i]+((sync3_reference[i+1]-sync3_reference[i])*fraction+512u)/1024u;
}
