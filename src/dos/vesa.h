#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <dpmi.h>


bool vesa_init(uint32_t width, uint32_t height, uint32_t *actual_width, uint32_t *actual_height);

int vesa_get_frame_buffer_selector();

void vesa_dispose();