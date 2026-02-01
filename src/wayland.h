#include "render.h"
#include <wayland-client.h>
int wayland_backend();
void wayland_dispatch();
void frame_done(void *data, struct wl_callback *cb, uint32_t time);
