#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "wayland.h"
#include <cairo/cairo.h>
#include <wayland-client-core.h>
#include <wayland-client.h>
/* generated protocol header for wlr-layer-shell (see notes below) */
#include "defs.h"
#include "layer-shell.h"
#include "render.h"
/* Global Wayland objects (kept simple for example) */
static struct wl_display *display = NULL;
static struct wl_registry *registry = NULL;
static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;
static struct zwlr_layer_shell_v1 *layer_shell = NULL;

struct output_state {
  struct wl_output *output;
  uint32_t name;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  struct wl_buffer *wl_buffer;
  cairo_surface_t *cairo_surface;
  unsigned char *shm_data;
  int buf_fd;
  int width, height;
  uint32_t shm_size;
  bool configured;
  struct wl_callback *frame_cb;
  struct output_state *next;
};

static struct output_state *outputs = NULL;

static void destroy_output(struct output_state *o);

/* helper: create temporary file for shm-backed buffer (mkstemp + unlink) */
static int create_tmpfile_cloexec(char *tmpname) {
  int fd = mkstemp(tmpname);
  if (fd >= 0) {
    unlink(tmpname);
    /* set CLOEXEC */
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0)
      fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  }
  return fd;
}

/* create shm-backed cairo surface and wl_buffer sized w*h */
static bool create_shm_buffer(struct output_state *o, int w, int h) {
  if (w <= 0 || h <= 0)
    return false;

  /* destroy previous */
  if (o->wl_buffer) {
    wl_buffer_destroy(o->wl_buffer);
    o->wl_buffer = NULL;
  }
  if (o->cairo_surface) {
    cairo_surface_destroy(o->cairo_surface);
    o->cairo_surface = NULL;
  }
  if (o->shm_data) {
    munmap(o->shm_data, o->shm_size);
    o->shm_data = NULL;
  }
  if (o->buf_fd >= 0) {
    close(o->buf_fd);
    o->buf_fd = -1;
  }

  uint32_t stride = 4 * w;
  o->shm_size = stride * h;
  char template[] = "/tmp/layer-shm-XXXXXX";
  o->buf_fd = create_tmpfile_cloexec(template);
  if (o->buf_fd < 0) {
    perror("mkstemp");
    return false;
  }
  if (ftruncate(o->buf_fd, o->shm_size) < 0) {
    perror("ftruncate");
    close(o->buf_fd);
    o->buf_fd = -1;
    return false;
  }

  o->shm_data =
      mmap(NULL, o->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, o->buf_fd, 0);
  if (o->shm_data == MAP_FAILED) {
    perror("mmap");
    close(o->buf_fd);
    o->buf_fd = -1;
    o->shm_data = NULL;
    return false;
  }

  /* create Cairo surface (ARGB32 premultiplied matches WL_SHM_FORMAT_ARGB8888)
   */
  o->cairo_surface = cairo_image_surface_create_for_data(
      o->shm_data, CAIRO_FORMAT_ARGB32, w, h, stride);
  if (cairo_surface_status(o->cairo_surface) != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr, "cairo surface create failed\n");
    munmap(o->shm_data, o->shm_size);
    o->shm_data = NULL;
    close(o->buf_fd);
    o->buf_fd = -1;
    o->cairo_surface = NULL;
    return false;
  }

  /* create wl_shm_pool and wl_buffer */
  struct wl_shm_pool *pool = wl_shm_create_pool(shm, o->buf_fd, o->shm_size);
  o->wl_buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride,
                                           WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);

  return true;
}

/* draw test text into cairo_surface */
static struct text_size draw_test_text(cairo_t *cr, const char *text,
                                       uint32_t x, uint32_t y, float r, float g,
                                       float b, float a) {
  if ((int32_t)x < -1000)
    x = -1000;

  /* draw white text near top center */
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  double font_size = 24.0;
  cairo_set_font_size(cr, font_size);

  cairo_text_extents_t ext;
  cairo_text_extents(cr, text, &ext);

  /* black shadow */
  cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
  cairo_move_to(cr, x + 2.0 - ext.x_bearing, y + 2.0);
  cairo_show_text(cr, text);

  /* white text */
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_move_to(cr, x - ext.x_bearing, y);
  cairo_show_text(cr, text);

  struct text_size t = {ext.width, ext.height};

  return t;
}

/* draw test text into cairo_surface */
static void draw_text(struct output_state *o) {
  if (!o->cairo_surface)
    return;
  cairo_t *cr = cairo_create(o->cairo_surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  /* clear transparent */
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  draw_test_text(cr, "Selected profile:", 30, get_y_pos() * 35 - 5, 0.7, 0.7,
                 0.7, 1);

  uint32_t color = get_color();
  float r = ((color & 0xFF000000) >> 24) / 255.0f;
  float g = ((color & 0xFF0000) >> 16) / 255.0f;
  float b = ((color & 0xFF00) >> 8) / 255.0f;
  float a = (color & 0xFF) / 255.0f;
  draw_test_text(cr, get_text(), 225, get_y_pos() * 35 - 5, r, g, b, a);

  // Help menu
  double l =
      draw_test_text(cr, get_text(), get_x_pos() * 35 - 5, 60, r, g, b, a)
          .width;
  double h = draw_test_text(cr, "binds:", get_x_pos() * 35 + 7 + l, 60, 0.7,
                            0.7, 0.7, 1)
                 .height;

  struct profile p = presets[profile_ind];
  for (int i = 0; i < sizeof(p.binds) / sizeof(struct key_bind); i++) {
    struct key_bind kb = p.binds[i];
    if (kb.key == 0)
      continue;
    char bind[64];
    snprintf(bind, 64, "[%i] %s", kb.key + index_render_offset, kb.desc);
    draw_test_text(cr, bind, get_x_pos() * 35, (h + 12) * (i + 3), 1, 1, 1,
                   0.9);
  }

  cairo_destroy(cr);
}

struct wl_callback_listener frame_listener = {.done = frame_done};

void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
  struct output_state *o = data;
  wl_callback_destroy(o->frame_cb); // удаляем старый callback
  o->frame_cb = NULL;

  if (!o->configured || !o->wl_buffer)
    return;

  draw_text(o);

  wl_surface_attach(o->surface, o->wl_buffer, 0, 0);
  wl_surface_damage(o->surface, 0, 0, o->width, o->height);

  o->frame_cb = wl_surface_frame(o->surface);
  wl_callback_add_listener(o->frame_cb, &frame_listener, o);

  wl_surface_commit(o->surface);
}

/* layer-surface configure listener */
static void
layer_surface_handle_configure(void *data,
                               struct zwlr_layer_surface_v1 *surface_v1,
                               uint32_t serial, uint32_t w, uint32_t h) {
  struct output_state *o = data;

  /* compositor gives width/height (0 means "use content size") */
  if (w == 0)
    w = 400;
  if (h == 0)
    h = 50;

  o->width = (int)w;
  o->height = (int)h;

  if (!create_shm_buffer(o, o->width, o->height)) {
    fprintf(stderr, "failed to create shm buffer\n");
    return;
  }
  o->configured = true;

  zwlr_layer_surface_v1_ack_configure(surface_v1, serial);

  /* attach buffer and commit */
  wl_surface_attach(o->surface, o->wl_buffer, 0, 0);
  wl_surface_damage(o->surface, 0, 0, o->width, o->height);

  /* set empty input region so the bar is click-th rough */
  struct wl_region *r = wl_compositor_create_region(compositor);
  /* do not add rects -> empty region */
  wl_surface_set_input_region(o->surface, r);
  wl_region_destroy(r);

  if (!o->frame_cb) {
    o->frame_cb = wl_surface_frame(o->surface);
    wl_callback_add_listener(o->frame_cb, &frame_listener, o);
  }

  wl_surface_commit(o->surface);
}

static void
layer_surface_handle_closed(void *data,
                            struct zwlr_layer_surface_v1 *surface_v1) {
  struct output_state *o = data;
  fprintf(stderr, "layer surface closed by compositor\n");
  destroy_output(o);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed};

static void create_output(struct output_state *o) {
  o->surface = wl_compositor_create_surface(compositor);
  if (!o->surface) {
    fprintf(stderr, "Failed to create wl_surface\n");
    return;
  }

  /* create layer-surface: layer = TOP, namespace string arbitrary */
  o->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, o->surface, o->output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      "example-layer");

  /* set anchors: top + left + right to stretch across top */
  zwlr_layer_surface_v1_set_anchor(o->layer_surface,
                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);

  zwlr_layer_surface_v1_add_listener(o->layer_surface,
                                     &layer_surface_listener, o);

  /* set desired exclusive zone to 0 (non-exclusive) or >0 to reserve space */
  zwlr_layer_surface_v1_set_exclusive_zone(o->layer_surface, 0);

  /* commit so compositor sends initial configure */
  wl_surface_commit(o->surface);
}

static void destroy_output(struct output_state *o) {
  struct output_state **it = &outputs;
  while (*it && *it != o)
    it = &(*it)->next;
  if (*it)
    *it = o->next;

  if (o->frame_cb) {
    wl_callback_destroy(o->frame_cb);
    o->frame_cb = NULL;
  }
  if (o->layer_surface) {
    zwlr_layer_surface_v1_destroy(o->layer_surface);
    o->layer_surface = NULL;
  }
  if (o->surface) {
    wl_surface_destroy(o->surface);
    o->surface = NULL;
  }
  if (o->wl_buffer) {
    wl_buffer_destroy(o->wl_buffer);
    o->wl_buffer = NULL;
  }
  if (o->cairo_surface) {
    cairo_surface_destroy(o->cairo_surface);
    o->cairo_surface = NULL;
  }
  if (o->shm_data) {
    munmap(o->shm_data, o->shm_size);
    o->shm_data = NULL;
  }
  if (o->buf_fd >= 0) {
    close(o->buf_fd);
    o->buf_fd = -1;
  }
  if (o->output) {
    wl_output_destroy(o->output);
    o->output = NULL;
  }
  free(o);
}

/* registry handlers */
static void registry_handle_global(void *data, struct wl_registry *reg,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    layer_shell =
        wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 1);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    struct output_state *o = calloc(1, sizeof(struct output_state));
    if (!o)
      return;
    o->output = wl_registry_bind(reg, name, &wl_output_interface, 3);
    o->name = name;
    o->buf_fd = -1;
    o->next = outputs;
    outputs = o;
    if (compositor && shm && layer_shell)
      create_output(o);
  }
}

static void registry_handle_global_remove(void *data, struct wl_registry *reg,
                                          uint32_t name) {
  (void)data;
  (void)reg;

  struct output_state *o = outputs;
  while (o && o->name != name)
    o = o->next;
  if (o)
    destroy_output(o);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove};

void wayland_dispatch() {
  wl_display_dispatch(display);
  wl_display_flush(display);
}

int wayland_backend() {
  display = wl_display_connect(NULL);
  if (!display) {
    fprintf(stderr, "Failed to connect to Wayland display\n");
    return 1;
  }

  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);

  if (!compositor || !shm || !layer_shell) {
    fprintf(stderr, "Required globals missing (compositor/shm/layer_shell)\n");
    return 1;
  }

  if (!outputs) {
    fprintf(stderr, "No wl_output found\n");
    return 1;
  }

  for (struct output_state *o = outputs; o; o = o->next)
    if (!o->surface)
      create_output(o);

  wl_display_roundtrip(display);
  wl_display_flush(display);

  return 0;
}
