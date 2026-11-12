#pragma once
#include "vvvf/control.h"
void vvvf_output_init(vvvf_command_t initial);
void vvvf_output_command(vvvf_command_t command);
bool vvvf_output_fault(void);
uint32_t vvvf_output_max_render_us(void);
