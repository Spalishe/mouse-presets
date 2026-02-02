#include "render.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

uint64_t last_timestamp = 0;
char *text = "";
uint32_t color = 0xFFFFFFFF;

uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

float get_y_pos() {
  uint64_t t = now_ms();
  float diff = (t - last_timestamp) / 1000.0f;
  float val = 0;
  if (diff < 1) {
    val = 1.0 - pow(2.0, -10.0 * diff);
  } else if (diff >= 1 && diff < 5)
    val = 1.0;
  else if (diff >= 5 && diff <= 6) {
    val = 1.0 - pow(2.0 * (diff - 5.0), 10.0);
  }

  return val;
}
char *get_text() { return text; }
uint32_t get_color() { return color; }
void update_text(char *new_text, uint32_t col) {
  text = new_text;
  color = col;
  last_timestamp = now_ms();
}
