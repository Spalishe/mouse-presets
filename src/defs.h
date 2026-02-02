#include <stdbool.h>
#include <stdint.h>

struct device {
  uint8_t dev_id;
  int fd;
  char *dev;
};

enum Action {
  Default = 0,
  Help = 1,
  SwitchProfile = 2,
};

enum ClickType {
  Short = 0,
  Long = 1,
};

struct bind {
  bool valid;
  uint8_t dev_id;
  uint16_t code;
  uint16_t type;
};

struct key_bind {
  uint8_t key;
  char *desc;
  enum Action action;
  enum ClickType click;
  struct bind buttons[8];
};
struct profile {
  uint8_t size;
  struct key_bind binds[16];
  char name[32];
  uint32_t color;
};

extern struct device devices[2];

extern char mouse_id;

extern int profile_ind;
extern int profile_max_ind;
extern int index_render_offset;

extern struct profile presets[];
