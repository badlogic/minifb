#include "MiniFB.h"
#include "MiniFB_enums.h"
#include "MiniFB_internal.h"
#include "WindowData.h"

#include <dpmi.h>
#include <pc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/segments.h>
#include <time.h>

#include "vesa.h"

static uint32_t scancode_to_mfb_key[] = {
    KB_KEY_UNKNOWN,
    KB_KEY_ESCAPE,
    KB_KEY_1,
    KB_KEY_2,
    KB_KEY_3,
    KB_KEY_4,
    KB_KEY_5,
    KB_KEY_6,
    KB_KEY_7,
    KB_KEY_8,
    KB_KEY_9,
    KB_KEY_0,
    KB_KEY_MINUS,
    KB_KEY_EQUAL,
    KB_KEY_BACKSPACE,
    KB_KEY_TAB,
    KB_KEY_Q,
    KB_KEY_W,
    KB_KEY_E,
    KB_KEY_R,
    KB_KEY_T,
    KB_KEY_Y,
    KB_KEY_U,
    KB_KEY_I,
    KB_KEY_O,
    KB_KEY_P,
    KB_KEY_LEFT_BRACKET,
    KB_KEY_RIGHT_BRACKET,
    KB_KEY_ENTER,
    KB_KEY_LEFT_CONTROL,
    KB_KEY_A,
    KB_KEY_S,
    KB_KEY_D,
    KB_KEY_F,
    KB_KEY_G,
    KB_KEY_H,
    KB_KEY_J,
    KB_KEY_K,
    KB_KEY_L,
    KB_KEY_SEMICOLON,
    KB_KEY_APOSTROPHE,
    KB_KEY_GRAVE_ACCENT,
    KB_KEY_LEFT_SHIFT,
    KB_KEY_BACKSLASH,
    KB_KEY_Z,
    KB_KEY_X,
    KB_KEY_C,
    KB_KEY_V,
    KB_KEY_B,
    KB_KEY_N,
    KB_KEY_M,
    KB_KEY_COMMA,
    KB_KEY_PERIOD,
    KB_KEY_SLASH,
    KB_KEY_RIGHT_SHIFT,
    KB_KEY_PRINT_SCREEN,
    KB_KEY_LEFT_ALT,
    KB_KEY_SPACE,
    KB_KEY_CAPS_LOCK,
    KB_KEY_F1,
    KB_KEY_F2,
    KB_KEY_F3,
    KB_KEY_F4,
    KB_KEY_F5,
    KB_KEY_F6,
    KB_KEY_F7,
    KB_KEY_F8,
    KB_KEY_F9,
    KB_KEY_F10,
    KB_KEY_NUM_LOCK,
    KB_KEY_SCROLL_LOCK,
    KB_KEY_HOME,
    KB_KEY_UP,
    KB_KEY_PAGE_UP,
    KB_KEY_MINUS, // ??
    KB_KEY_LEFT,
    KB_KEY_UNKNOWN, // CENTER??
    KB_KEY_RIGHT,
    KB_KEY_KP_ADD,
    KB_KEY_END,
    KB_KEY_DOWN,
    KB_KEY_PAGE_DOWN,
    KB_KEY_INSERT,
    KB_KEY_DELETE,
    KB_KEY_KP_DIVIDE,
    KB_KEY_KP_ENTER,
    KB_KEY_F11,
    KB_KEY_F12,
};

char scancode_to_ascii[] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9',  '0', '-', '=',  0,
    0,   'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',  '[', ']', 0,    0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,   0,    0,   ' ',
};

char scancode_to_ascii_shift[] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0,
    0,   'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,   0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z',
    'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   0,   0,   ' ',
};

#define RING_BUFFER_SIZE 512
typedef struct ring_buffer {
  uint32_t read_index;
  uint32_t write_index;
  uint8_t buffer[RING_BUFFER_SIZE];
} ring_buffer;

bool ring_buffer_push(ring_buffer *buffer, uint8_t value) {
  uint32_t next = buffer->write_index + 1;
  if (next >= RING_BUFFER_SIZE)
    next = 0;

  if (next == buffer->read_index)
    return false;

  buffer->buffer[buffer->write_index] = value;
  buffer->write_index = next;
  return true;
}

bool ring_buffer_pop(ring_buffer *buffer, uint8_t *value) {
  if (buffer->write_index == buffer->read_index)
    return false;

  uint32_t next = buffer->read_index + 1;
  if (next >= RING_BUFFER_SIZE)
    next = 0;

  *value = buffer->buffer[buffer->read_index];
  buffer->read_index = next;
  return true;
}

typedef struct keyboard_state {
  bool initialized;
  ring_buffer buffer;
  _go32_dpmi_seginfo old_keyboard_handler;
  _go32_dpmi_seginfo new_keyboard_handler;
  bool last_scancode_was_extended;
  bool caps_lock;
} keyboard_state;

static keyboard_state g_keyboard = {0};

typedef struct SWindowData_DOS {
  uint32_t actual_width, actual_height;
  uint32_t *scale_buffer;
} SWindowData_DOS;

static SWindowData *g_window = NULL;

__attribute__((destructor)) static void tear_down() {
  vesa_dispose();
  if (g_keyboard.initialized) {
    _go32_dpmi_set_protected_mode_interrupt_vector(
        0x9, &g_keyboard.old_keyboard_handler);
    _go32_dpmi_free_iret_wrapper(&g_keyboard.new_keyboard_handler);
  }
}

static void init_mouse(SWindowData *window_data) {
  __dpmi_regs regs;
  regs.x.ax = 7;
  regs.x.cx = 0;
  regs.x.dx = window_data->window_width - 1;
  __dpmi_int(0x33, &regs);

  regs.x.ax = 8;
  regs.x.cx = 0;
  regs.x.dx = window_data->window_height - 1;
  __dpmi_int(0x33, &regs);

  regs.x.ax = 2;
  __dpmi_int(0x33, &regs);
}

static void keyboard_handler() {
  ring_buffer_push(&g_keyboard.buffer, inp(0x60));
  outportb(0x20, 0x20);
}

static void init_keyboard() {
  if (g_keyboard.initialized)
    return;
  _go32_dpmi_lock_data(&g_keyboard, sizeof(g_keyboard));
  _go32_dpmi_lock_code(keyboard_handler, 4096);

  _go32_dpmi_get_protected_mode_interrupt_vector(
      0x9, &g_keyboard.old_keyboard_handler);

  g_keyboard.new_keyboard_handler.pm_offset = (int)keyboard_handler;
  g_keyboard.new_keyboard_handler.pm_selector = _my_cs();

  _go32_dpmi_allocate_iret_wrapper(&g_keyboard.new_keyboard_handler);
  _go32_dpmi_set_protected_mode_interrupt_vector(
      0x9, &g_keyboard.new_keyboard_handler);

  g_keyboard.initialized = true;
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

struct mfb_window *mfb_open_ex(const char *title, unsigned width,
                               unsigned height, unsigned flags) {
  if (g_window)
    return NULL;

  uint32_t actual_width, actual_height;
  if (!vesa_init(width, height, &actual_width, &actual_height)) {
    printf("Couldn't set VESA mode %ix%i\n", width, height);
    return NULL;
  }

  SWindowData *window_data;

  window_data = malloc(sizeof(SWindowData));
  if (window_data == 0x0) {
    printf("Cannot allocate window data\n");
    return 0x0;
  }
  memset(window_data, 0, sizeof(SWindowData));
  window_data->window_width = width;
  window_data->window_height = height;
  window_data->buffer_width = width;
  window_data->buffer_height = height;

  SWindowData_DOS *specific = malloc(sizeof(SWindowData_DOS));
  if (!specific) {
    printf("Cannot allocate DOS window data\n");
    return 0x0;
  }
  specific->actual_width = actual_width;
  specific->actual_height = actual_height;
  specific->scale_buffer =
      (uint32_t *)malloc(actual_width * actual_height * sizeof(uint32_t));
  window_data->specific = specific;

  mfb_set_keyboard_callback((struct mfb_window *)window_data, keyboard_default);

  window_data->is_active = true;
  window_data->is_initialized = true;

  g_window = window_data;

  init_mouse(window_data);
  init_keyboard(specific);

  return (struct mfb_window *)window_data;
}

bool mfb_set_viewport(struct mfb_window *window, unsigned offset_x,
                      unsigned offset_y, unsigned width, unsigned height) {
  return false;
}

static void update_mouse(SWindowData *window_data) {
  __dpmi_regs regs;
  regs.x.ax = 0x3;
  __dpmi_int(0x33, &regs);
  int32_t old_x = window_data->mouse_pos_x;
  int32_t old_y = window_data->mouse_pos_y;
  uint8_t old_left_pressed = window_data->mouse_button_status[MOUSE_LEFT];
  uint8_t old_right_pressed = window_data->mouse_button_status[MOUSE_RIGHT];
  uint8_t left_pressed = regs.x.bx & 1;
  uint8_t right_pressed = regs.x.bx & 2;
  window_data->mouse_button_status[MOUSE_LEFT] = left_pressed;
  window_data->mouse_button_status[MOUSE_RIGHT] = right_pressed;
  window_data->mouse_pos_x = regs.x.cx;
  window_data->mouse_pos_y = regs.x.dx;

  if (old_left_pressed != left_pressed && window_data->mouse_btn_func)
    window_data->mouse_btn_func((struct mfb_window *)window_data, MOUSE_LEFT, 0,
                                left_pressed);

  if (old_right_pressed != right_pressed && window_data->mouse_btn_func)
    window_data->mouse_btn_func((struct mfb_window *)window_data, MOUSE_RIGHT,
                                0, right_pressed);

  if ((old_x != regs.x.cx || old_y != regs.x.dx) &&
      window_data->mouse_move_func)
    window_data->mouse_move_func((struct mfb_window *)window_data, regs.x.cx,
                                 regs.x.dx);
}

static void update_keyboard(SWindowData *window_data) {
  uint8_t raw_scancode;
  while (ring_buffer_pop(&g_keyboard.buffer, &raw_scancode)) {
    if (raw_scancode == 0xe0 || raw_scancode == 0xe1 || raw_scancode == 0xe2) {
      g_keyboard.last_scancode_was_extended = raw_scancode + 1 - 0xe0;
      continue;
    }

    uint8_t scancode = raw_scancode & 0x7f;
    if (scancode >= sizeof(scancode_to_mfb_key) / sizeof(uint32_t))
      continue;

    bool pressed = raw_scancode & 0x80 ? false : true;
    uint32_t mfb_key = scancode_to_mfb_key[scancode];
    char ascii = 0;
    if (scancode < sizeof(scancode_to_ascii) / sizeof(char)) {
      if ((window_data->mod_keys & KB_MOD_SHIFT) ||
          (window_data->mod_keys & KB_MOD_CAPS_LOCK)) {
        ascii = scancode_to_ascii_shift[scancode];
      } else {
        ascii = scancode_to_ascii[scancode];
      }
    }

    // printf("scancode %i, mfb key: %s, ascii: %c, pressed: %i\n", scancode,
    //       mfb_get_key_name(mfb_key), ascii, pressed);

    window_data->key_status[mfb_key] = pressed;
    if (mfb_key == KB_KEY_LEFT_SHIFT || mfb_key == KB_KEY_RIGHT_SHIFT) {
      if (pressed)
        window_data->mod_keys |= KB_MOD_SHIFT;
      else
        window_data->mod_keys &= ~KB_MOD_SHIFT;
    }
    if (mfb_key == KB_KEY_LEFT_ALT || mfb_key == KB_KEY_RIGHT_ALT) {
      if (pressed)
        window_data->mod_keys |= KB_MOD_ALT;
      else
        window_data->mod_keys &= ~KB_MOD_ALT;
    }
    if (mfb_key == KB_KEY_LEFT_CONTROL || mfb_key == KB_KEY_RIGHT_CONTROL) {
      if (pressed)
        window_data->mod_keys |= KB_MOD_CONTROL;
      else
        window_data->mod_keys &= ~KB_MOD_CONTROL;
    }

    if (mfb_key == KB_KEY_CAPS_LOCK && !pressed) {
      g_keyboard.caps_lock = !g_keyboard.caps_lock;
      if (g_keyboard.caps_lock)
        window_data->mod_keys |= KB_MOD_CAPS_LOCK;
      else
        window_data->mod_keys &= ~KB_MOD_CAPS_LOCK;
    }

    if (window_data->keyboard_func)
      window_data->keyboard_func((struct mfb_window *)window_data, mfb_key,
                                 window_data->mod_keys, pressed);

    if (window_data->char_input_func && !pressed && ascii != 0)
      window_data->char_input_func((struct mfb_window *)window_data, ascii);

    // FIXME we currently ignore extended keys
    g_keyboard.last_scancode_was_extended = false;
  }
}

mfb_update_state mfb_update_events(struct mfb_window *window) {
  if (!window)
    return STATE_INVALID_WINDOW;
  SWindowData *window_data = (SWindowData *)window;
  mfb_update_state state = check_window_closed(window_data);
  if (state)
    return state;

  update_mouse(window_data);
  update_keyboard(window_data);

  return STATE_OK;
}

mfb_update_state mfb_update_ex(struct mfb_window *window, void *buffer,
                               unsigned width, unsigned height) {
  if (!window)
    return STATE_INVALID_WINDOW;

  SWindowData *window_data = (SWindowData *)window;
  mfb_update_state state = check_window_closed(window_data);
  if (state)
    return state;

  mfb_update_events(window);

  SWindowData_DOS *dos_window_data = window_data->specific;
  if (dos_window_data->actual_width == width &&
      dos_window_data->actual_height == height) {
    movedata(_my_ds(), (unsigned int)buffer, vesa_get_frame_buffer_selector(),
             0, width * height * sizeof(uint32_t));
  } else {
    if (dos_window_data->actual_width == width * 2 &&
        dos_window_data->actual_height == height * 2) {
      uint32_t *src_buffer = (uint32_t *)buffer;
      uint32_t *scale_buffer = dos_window_data->scale_buffer;
      uint32_t pitch_bytes = dos_window_data->actual_width * sizeof(uint32_t);

      for (int y = 0, ye = height; y < ye; y++, src_buffer += width) {
        for (int x = 0, xe = width; x < xe; x++) {
          uint32_t color = src_buffer[x];
          *scale_buffer++ = color;
          *scale_buffer++ = color;
        }
        memcpy(scale_buffer, scale_buffer - dos_window_data->actual_width,
               pitch_bytes);
        scale_buffer += dos_window_data->actual_width;
      }
      movedata(_my_ds(), (unsigned int)dos_window_data->scale_buffer,
               vesa_get_frame_buffer_selector(), 0,
               dos_window_data->actual_width * dos_window_data->actual_height *
                   sizeof(uint32_t));
    } else {
      printf("Unsupported display mode %ix%i\n",
             (int)dos_window_data->actual_width,
             (int)dos_window_data->actual_height);
    }
  }
  return STATE_OK;
}

bool mfb_wait_sync(struct mfb_window *window) {
  if (!window)
    return STATE_INVALID_WINDOW;

  SWindowData *window_data = (SWindowData *)window;
  mfb_update_state state = check_window_closed(window_data);
  if (state)
    return state;

  return true;
}

void mfb_get_monitor_scale(struct mfb_window *window, float *scale_x,
                           float *scale_y) {
  if (!window)
    return;
  if (scale_x)
    *scale_x = 1.0f;
  if (scale_y)
    *scale_y = 1.0f;
}

extern double g_timer_frequency;
extern double g_timer_resolution;

void mfb_timer_init(void) {
  g_timer_frequency = UCLOCKS_PER_SEC;
  g_timer_resolution = 1.0 / g_timer_frequency;
}

uint64_t mfb_timer_tick(void) { return uclock(); }