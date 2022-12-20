#include "MiniFB_enums.h"
#include "MiniFB.h"
#include "MiniFB_internal.h"
#include "WindowData.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/segments.h>
#include <time.h>

#include "vesa.h"

typedef struct SWindowData_DOS {
	uint32_t actual_width, actual_height;
	uint32_t *scale_buffer;
} SWindowData_DOS;

SWindowData *g_window = NULL;

__attribute__((destructor))
static void tear_down() {
    vesa_dispose();
}

static mfb_update_state check_window_closed(SWindowData *window_data) {
    if (window_data->close) {
        if (window_data->specific) {
            g_window = NULL;
            vesa_dispose();
            free(window_data->specific);
            window_data->specific = NULL;
            return STATE_EXIT;
        } else {
            return STATE_INVALID_WINDOW;
        }
    }
    return STATE_OK;
}

struct mfb_window *mfb_open_ex(const char *title, unsigned width, unsigned height, unsigned flags) {
    if (g_window) return NULL;

	uint32_t actual_width, actual_height;
	if (!vesa_init(width, height, &actual_width, &actual_height)) {
		printf("Couldn't set VESA mode %ix%i\n", width, height);
		return NULL;
	}

    SWindowData *window_data;

    window_data = malloc(sizeof(SWindowData));
    if(window_data == 0x0) {
        printf("Cannot allocate window data\n");
        return 0x0;
    }
    memset(window_data, 0, sizeof(SWindowData));
    window_data->window_width = width;
	window_data->window_height = height;

    SWindowData_DOS *specific = malloc(sizeof(SWindowData_DOS));
    if (!specific) {
        printf("Cannot allocate DOS window data\n");
        return 0x0;
    }
	specific->actual_width = actual_width;
	specific->actual_height = actual_height;
	specific->scale_buffer = (uint32_t *) malloc(actual_width * actual_height * sizeof(uint32_t));
    window_data->specific = specific;

    mfb_set_keyboard_callback((struct mfb_window *) window_data, keyboard_default);

    window_data->is_active = true;
    window_data->is_initialized = true;

    g_window = window_data;

    return (struct mfb_window*)window_data;
}

bool mfb_set_viewport(struct mfb_window *window, unsigned offset_x, unsigned offset_y, unsigned width, unsigned height) {
    return false;
}

mfb_update_state mfb_update_events(struct mfb_window *window) {
    SWindowData* window_data = (SWindowData *) window;
    mfb_update_state state = check_window_closed(window_data);
    if (!state) return state;

    return STATE_OK;
}

mfb_update_state mfb_update_ex(struct mfb_window *window, void *buffer, unsigned width, unsigned height) {
    if (!window) return STATE_INVALID_WINDOW;

    SWindowData* window_data = (SWindowData *) window;
    mfb_update_state state = check_window_closed(window_data);
    if (!state) return state;

    SWindowData_DOS *dos_window_data = window_data->specific;
	if (dos_window_data->actual_width == width && dos_window_data->actual_height == height) {
		movedata(_my_ds(), (unsigned int) buffer, vesa_get_frame_buffer_selector(), 0, width * height * sizeof(uint32_t));
	} else {
		if (dos_window_data->actual_width == width * 2 && dos_window_data->actual_height == height * 2) {
			uint32_t *src_buffer = (uint32_t *) buffer;
			uint32_t *scale_buffer = dos_window_data->scale_buffer;
			uint32_t pitch_bytes = dos_window_data->actual_width * sizeof(uint32_t);

			for (int y = 0, ye = height; y < ye; y++, src_buffer += width) {
				for (int x = 0, xe = width; x < xe; x++) {
					uint32_t color = src_buffer[x];
					*scale_buffer++ = color;
					*scale_buffer++ = color;
				}
				memcpy(scale_buffer, scale_buffer - dos_window_data->actual_width, pitch_bytes);
				scale_buffer += dos_window_data->actual_width;
			}
			movedata(_my_ds(), (unsigned int) dos_window_data->scale_buffer, vesa_get_frame_buffer_selector(), 0, dos_window_data->actual_width * dos_window_data->actual_height * sizeof(uint32_t));
		}
	}
    return STATE_OK;
}

bool mfb_wait_sync(struct mfb_window *window) {
    if (!window) return STATE_INVALID_WINDOW;

    SWindowData* window_data = (SWindowData *) window;
    mfb_update_state state = check_window_closed(window_data);
    if (!state) return state;

    return true;
}

void mfb_get_monitor_scale(struct mfb_window *window, float *scale_x, float *scale_y) {
    if (!window) return;
    if (scale_x) *scale_x = 1.0f;
    if (scale_y) *scale_y = 1.0f;
}

extern double g_timer_frequency;
extern double g_timer_resolution;

void mfb_timer_init(void) {
    g_timer_frequency  = UCLOCKS_PER_SEC;
    g_timer_resolution = 1.0 / g_timer_frequency;
}

uint64_t mfb_timer_tick(void) {
    return uclock();
}
