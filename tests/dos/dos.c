#include "MiniFB_enums.h"
#include <MiniFB.h>
#include <stdlib.h>
#include <string.h>
#define GDB_IMPLEMENTATION
#include "../src/dos/gdbstub.h"

int main(void) {
  gdb_start();
  uint32_t *pixels = (uint32_t *)malloc(sizeof(uint32_t) * 320 * 240);
  struct mfb_window *window = mfb_open("Noise Test", 320, 240);

  do {
    for (int i = 0; i < 2000; i++) {
      int x = rand() % 320;
      int y = rand() % 240;
      int color = MFB_ARGB(0xff, rand() % 0xff, rand() % 0xff, rand() % 0xff);
      pixels[x + y * 320] = color;
    }

    if (mfb_get_mouse_button_buffer(window)[MOUSE_LEFT]) {
      memset(pixels, 0, 320 * 240 * 4);
    }
    mfb_update(window, pixels);
    gdb_checkpoint();
  } while (mfb_wait_sync(window));
  return 0;
}