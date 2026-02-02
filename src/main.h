#pragma once
#include <stdint.h>

char *mouse_name = "usb-04d9_USB_Gaming_Mouse-if01-event-kbd";
char profile_switch_button = 9;

enum ActionType { Nothing = 0, RunCommand = 1, PressButtons = 2 };

struct key_bind {
  uint8_t key;
  enum ActionType action;
  uint8_t args;
};
struct profile {
  uint8_t size;
  struct key_bind binds[16];
  char name[32];
  uint32_t color;
};

int profile_ind = 0;
int profile_max_ind = 1;

struct profile presets[2] = {{0, {}, "None", 0x7A7A7AFF},
                             {0, {}, "test", 0xFFFFFFFF}};
