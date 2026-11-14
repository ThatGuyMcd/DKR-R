#pragma once
#include <stdint.h>
void* dkr_visual_read(uint64_t address, unsigned width);
void dkr_visual_write(uint64_t address, unsigned width, uint32_t value);
void dkr_visual_step(void);
