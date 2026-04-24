#pragma once
#include <Arduino.h>

void    ui_draw_header(const char* title, const char* legend);
int16_t ui_line_height();   // yAdvance of the UI font
int16_t ui_content_y0();    // baseline y of first content line
int     ui_content_lines(); // content lines that fit below the header
