#pragma once
#include <stdint.h>
/* Convert desired normalized fundamental to the comparator's Q15 amplitude.
   N=3 uses the inverse gain table; all other modes pass the input through. */
uint16_t vvvf_voltage_reference(uint16_t requested, uint8_t pulses);
