#pragma once
#include <stddef.h>
#include <stdint.h>
#include "vvvf/profiles.h"
/* Direct switching-angle patterns. No fictitious fc=N*f1 comparator. */
uint32_t vvvf_programmed_pins(uint32_t phase, uint32_t angle);
size_t vvvf_programmed_frame(uint32_t phase, uint32_t step, uint32_t angle,
                             uint32_t *words, uint32_t ticks);
