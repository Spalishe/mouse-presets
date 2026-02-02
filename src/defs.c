#include "defs.h"
#include <linux/input.h>

struct device devices[2] = {{0, 0, "usb-Evision_RGB_Keyboard-event-kbd"},
                            {1, 0, "usb-04d9_USB_Gaming_Mouse-if01-event-kbd"}};

char mouse_id = 1;

int profile_ind = 0;
int profile_max_ind = 2;
int index_render_offset = -1;

struct profile presets[] = {
    {0,
     {{9, "Switches profile to next", SwitchProfile, Short, {}}},
     "None",
     0x7A7A7AFF},
    {0,
     {
         {2,
          "\"Thanks!\"",
          Default,
          Short,
          {{1, 0, KEY_Z, EV_KEY}, {1, 0, KEY_2, EV_KEY}}},
         {3,
          "\"Spy!\"",
          Default,
          Short,
          {{1, 0, KEY_X, EV_KEY}, {1, 0, KEY_2, EV_KEY}}},
         {4,
          "\"Yes\"",
          Default,
          Short,
          {{1, 0, KEY_Z, EV_KEY}, {1, 0, KEY_7, EV_KEY}}},
         {5,
          "\"No\"",
          Default,
          Short,
          {{1, 0, KEY_Z, EV_KEY}, {1, 0, KEY_8, EV_KEY}}},
         {6,
          "\"Incoming\"",
          Default,
          Short,
          {{1, 0, KEY_X, EV_KEY}, {1, 0, KEY_1, EV_KEY}}},
         {7,
          "\"Sentry Ahead!\"",
          Default,
          Short,

          {{1, 0, KEY_X, EV_KEY}, {1, 0, KEY_3, EV_KEY}}},
         {8, "Shows help", Help, Short, {}},
         {9, "Switches profile to next", SwitchProfile, Short, {}},
     },
     "Team Fortress 2",
     0xF86A21FF},
    {0,
     {
         {2,
          "Operator 1 Skill/Ultimate",
          Default,
          Long,
          {{1, 0, KEY_1, EV_KEY}}},
         {3,
          "Operator 2 Skill/Ultimate",
          Default,
          Long,
          {{1, 0, KEY_2, EV_KEY}}},
         {4,
          "Operator 3 Skill/Ultimate",
          Default,
          Long,
          {{1, 0, KEY_3, EV_KEY}}},
         {5,
          "Operator 4 Skill/Ultimate",
          Default,
          Long,
          {{1, 0, KEY_4, EV_KEY}}},

         {8, "Shows help", Help, Short, {}},
         {9, "Switches profile to next", SwitchProfile, Short, {}},
     },
     "Arknights: Endfield",
     0xDDF72FFF}};
