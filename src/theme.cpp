#include "theme.h"
#include <GxEPD2_BW.h>

static bool g_dark = false;

void theme_set_dark(bool enabled) { g_dark = enabled; }
bool theme_is_dark()              { return g_dark; }

uint16_t theme_fg() { return g_dark ? GxEPD_WHITE : GxEPD_BLACK; }
uint16_t theme_bg() { return g_dark ? GxEPD_BLACK : GxEPD_WHITE; }

