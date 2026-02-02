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
#include "layer-shell.h"
#include "render.h"

/* Global Wayland objects (kept simple for example) */
static struct wl_display *display = NULL;
static struct wl_registry *registry = NULL;
static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;
static struct zwlr_layer_shell_v1 *layer_shell = NULL;
static struct wl_output *output =
    NULL; /* bind NULL if not choosing specific output */

static struct wl_surface *surface = NULL;
static struct zwlr_layer_surface_v1 *layer_surface = NULL;

static struct wl_buffer *wl_buffer = NULL;
static struct wl_buffer *back_buffer = NULL;
static cairo_surface_t *cairo_surface = NULL;
static unsigned char *shm_data = NULL;
static int buf_fd = -1;
static int width = 0, height = 0;
static uint32_t shm_size = 0;

static bool configured = false;

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
static bool create_shm_buffer(int w, int h) {
  if (w <= 0 || h <= 0)
    return false;

  /* destroy previous */
  if (wl_buffer) {
    wl_buffer_destroy(wl_buffer);
    wl_buffer = NULL;
  }
  if (cairo_surface) {
    cairo_surface_destroy(cairo_surface);
    cairo_surface = NULL;
  }
  if (shm_data) {
    munmap(shm_data, shm_size);
    shm_data = NULL;
  }
  if (buf_fd >= 0) {
    close(buf_fd);
    buf_fd = -1;
  }

  uint32_t stride = 4 * w;
  shm_size = stride * h;
  char template[] = "/tmp/layer-shm-XXXXXX";
  buf_fd = create_tmpfile_cloexec(template);
  if (buf_fd < 0) {
    perror("mkstemp");
    return false;
  }
  if (ftruncate(buf_fd, shm_size) < 0) {
    perror("ftruncate");
    close(buf_fd);
    buf_fd = -1;
    return false;
  }

  shm_data =
      mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
  if (shm_data == MAP_FAILED) {
    perror("mmap");
    close(buf_fd);
    buf_fd = -1;
    return false;
  }

  /* create Cairo surface (ARGB32 premultiplied matches WL_SHM_FORMAT_ARGB8888)
   */
  cairo_surface = cairo_image_surface_create_for_data(
      shm_data, CAIRO_FORMAT_ARGB32, w, h, stride);
  if (cairo_surface_status(cairo_surface) != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr, "cairo surface create failed\n");
    munmap(shm_data, shm_size);
    shm_data = NULL;
    close(buf_fd);
    buf_fd = -1;
    return false;
  }

  /* create wl_shm_pool and wl_buffer */
  struct wl_shm_pool *pool = wl_shm_create_pool(shm, buf_fd, shm_size);
  wl_buffer =
      wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);

  return true;
}

/* draw test text into cairo_surface */
static void draw_test_text(cairo_t *cr, const char *text, uint32_t x,
                           uint32_t y, float r, float g, float b, float a) {

  /* draw white text near top center */
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  double font_size = 24.0;
  cairo_set_font_size(cr, font_size);

  cairo_text_extents_t ext;
  cairo_text_extents(cr, text, &ext);

  /* white text */
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_move_to(cr, x - ext.x_bearing, y);
  cairo_show_text(cr, text);
}

/* layer-surface configure listener */
static void
layer_surface_handle_configure(void *data,
                               struct zwlr_layer_surface_v1 *surface_v1,
                               uint32_t serial, uint32_t w, uint32_t h) {

  /* compositor gives width/height (0 means "use content size") */
  if (w == 0)
    w = 400;
  if (h == 0)
    h = 50;

  width = (int)w;
  height = (int)h;

  if (!create_shm_buffer(width, height)) {
    fprintf(stderr, "failed to create shm buffer\n");
    return;
  }
  configured = true;

  zwlr_layer_surface_v1_ack_configure(surface_v1, serial);

  // draw_test_text("Selected profile:", 0, get_y_pos() * 35 - 5, 1, 1, 1, 0.3);

  /* attach buffer and commit */
  wl_surface_attach(surface, wl_buffer, 0, 0);
  wl_surface_damage(surface, 0, 0, width, height);

  /* set empty input region so the bar is click-th rough */
  struct wl_region *r = wl_compositor_create_region(compositor);
  /* do not add rects -> empty region */
  wl_surface_set_input_region(surface, r);
  wl_region_destroy(r);

  wl_surface_commit(surface);
}

static void
layer_surface_handle_closed(void *data,
                            struct zwlr_layer_surface_v1 *surface_v1) {
  /* the compositor closed the layer surface */ configured = true;
  fprintf(stderr, "layer surface closed by compositor\n");
  exit(0);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed};

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
    /* optional: bind an output if you want to target a specific monitor */
    output = wl_registry_bind(reg, name, &wl_output_interface, 3);
  }
}

static void registry_handle_global_remove(void *data, struct wl_registry *reg,
                                          uint32_t name) {
  (void)data;
  (void)reg;
  (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove};

static struct wl_callback *frame_cb;

void draw_text() {
  if (!cairo_surface)
    return;
  cairo_t *cr = cairo_create(cairo_surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  /* clear transparent */
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  draw_test_text(cr, "Selected profile:", 30, get_y_pos() * 35 - 5, 1, 1, 1,
                 0.7);

  uint32_t color = get_color();
  float r = ((color & 0xFF000000) >> 24) / 255.0f;
  float g = ((color & 0xFF0000) >> 16) / 255.0f;
  float b = ((color & 0xFF00) >> 8) / 255.0f;
  float a = (color & 0xFF) / 255.0f;
  draw_test_text(cr, get_text(), 225, get_y_pos() * 35 - 5, r, g, b, a);

  cairo_destroy(cr);
}

struct wl_callback_listener frame_listener = {.done = frame_done};

void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
  wl_callback_destroy(frame_cb); // удаляем старый callback

  draw_text();

  wl_surface_attach(surface, wl_buffer, 0, 0);
  wl_surface_damage(surface, 0, 0, width, height);

  frame_cb = wl_surface_frame(surface);
  wl_callback_add_listener(frame_cb, &frame_listener, NULL);

  wl_surface_commit(surface);
}

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

  surface = wl_compositor_create_surface(compositor);
  if (!surface) {
    fprintf(stderr, "Failed to create wl_surface\n");
    return 1;
  }

  /* create layer-surface: layer = TOP, namespace string arbitrary */
  layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, surface, output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
      "example-layer");

  /* set anchors: top + left + right to stretch across top */
  zwlr_layer_surface_v1_set_anchor(layer_surface,
                                   ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);

  zwlr_layer_surface_v1_add_listener(layer_surface, &layer_surface_listener,
                                     NULL);

  /* set desired exclusive zone to 0 (non-exclusive) or >0 to reserve space */
  zwlr_layer_surface_v1_set_exclusive_zone(layer_surface, 0);

  /* commit so compositor sends initial configure */
  wl_surface_commit(surface);

  frame_cb = wl_surface_frame(surface);
  wl_callback_add_listener(frame_cb, &frame_listener, NULL);
  wl_display_roundtrip(display);
  wl_surface_commit(surface);

  return 0;
}
