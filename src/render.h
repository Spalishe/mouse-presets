#pragma once
#include <stdbool.h>
#include <stdint.h>

struct text_size {
  double width;
  double height;
};

void update_text(char *new_text, uint32_t col);
void update_x(bool active);
float get_y_pos();
float get_x_pos();
char *get_text();
uint32_t get_color();
