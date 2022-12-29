#include "MiniFB_enums.h"
#include <MiniFB.h>
#include <stdlib.h>
#include <string.h>
#define GDB_IMPLEMENTATION
#include "gdbstub.h"

int main(void) {
  int res_x = 640;
  int res_y = 480;
  gdb_start();
  uint32_t *pixels = (uint32_t *)malloc(sizeof(uint32_t) * res_x * res_y);
  struct mfb_window *window = mfb_open("Noise Test", res_x, res_y);

  do {
    for (int i = 0; i < 2000; i++) {
      int x = rand() % res_x;
      int y = rand() % res_y;
      int color = MFB_ARGB(0xff, rand() % 0xff, rand() % 0xff, rand() % 0xff);
      pixels[x + y * res_x] = color;
    }

    if (mfb_get_mouse_button_buffer(window)[MOUSE_LEFT]) {
      int32_t x = mfb_get_mouse_x(window);
      int32_t y = mfb_get_mouse_y(window);
      x = x >= res_x ? res_x - 1 : x;
      x = x < 0 ? 0 : x;
      y = y >= res_y ? res_y - 1 : y;
      y = y < 0 ? 0 : y;
      uint8_t *dst = (uint8_t *)pixels;
      while (y >= 0) {
        memset(dst, 0, x * 4);
        dst += res_x * 4;
        y--;
      }
    }

    if (mfb_update(window, pixels) != STATE_OK) {
      break;
    }

    gdb_checkpoint();
  } while (mfb_wait_sync(window));
  return 0;
}