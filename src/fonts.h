#pragma once
#include <Adafruit_GFX.h>

// Font family indices
#define FONT_FAMILY_SANS  0   // Atkinson Hyperlegible
#define FONT_FAMILY_SERIF 1   // Source Serif 4

// Font size indices
#define FONT_SIZE_SMALL   0   // 6pt
#define FONT_SIZE_MEDIUM  1   // 8pt
#define FONT_SIZE_LARGE   2   // 10pt

// Forward declarations — defined in fonts.cpp (single include point)
extern const GFXfont AtkinsonHyperlegible_Regular6pt7b;
extern const GFXfont AtkinsonHyperlegible_Regular8pt7b;
extern const GFXfont AtkinsonHyperlegible_Regular10pt7b;
extern const GFXfont SourceSerif4_Regular6pt7b;
extern const GFXfont SourceSerif4_Regular8pt7b;
extern const GFXfont SourceSerif4_Regular10pt7b;
extern const GFXfont SourceCodePro_Regular5pt7b;
extern const GFXfont SourceCodePro_Regular7pt7b;

inline const GFXfont* getReadingFont(int family, int size) {
  static const GFXfont* table[2][3] = {
    { &AtkinsonHyperlegible_Regular6pt7b, &AtkinsonHyperlegible_Regular8pt7b, &AtkinsonHyperlegible_Regular10pt7b },
    { &SourceSerif4_Regular6pt7b,         &SourceSerif4_Regular8pt7b,         &SourceSerif4_Regular10pt7b         },
  };
  if (family < 0 || family > 1) family = 0;
  if (size   < 0 || size   > 2) size   = 1;
  return table[family][size];
}

#define UI_FONT   (&SourceCodePro_Regular7pt7b)
#define UI_FONT_S (&SourceCodePro_Regular5pt7b)
