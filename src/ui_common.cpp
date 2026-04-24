#include "ui_common.h"
#include "fonts.h"
#include "theme.h"
#include <GxEPD2_BW.h>

extern GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT> display;

int16_t ui_line_height() { return UI_FONT->yAdvance; }

void ui_draw_header(const char* title, const char* legend) {
  int16_t lh = ui_line_height();
  display.setFont(UI_FONT);
  display.setTextColor(theme_fg());
  display.setCursor(4, lh);
  display.print(title);
  display.setCursor(4, 2 * lh);
  display.print(legend);
  display.drawFastHLine(0, 2 * lh + 3, display.width(), theme_fg());
}

int16_t ui_content_y0()    { return 3 * ui_line_height(); }
int     ui_content_lines() { return (display.height() - ui_content_y0()) / ui_line_height(); }
