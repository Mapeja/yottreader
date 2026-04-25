#pragma once
#include <Adafruit_GFX.h>

// Font family indices
#define FONT_FAMILY_SANS        0   // Atkinson Hyperlegible Regular
#define FONT_FAMILY_SANS_BOLD   1   // Atkinson Hyperlegible Bold
#define FONT_FAMILY_SERIF       2   // Source Serif 4 Regular
#define FONT_FAMILY_SERIF_BOLD  3   // Source Serif 4 SemiBold

// Font size indices
#define FONT_SIZE_SMALL   0   // 6pt
#define FONT_SIZE_MEDIUM  1   // 8pt
#define FONT_SIZE_LARGE   2   // 10pt

// Reading fonts — defined in fonts.cpp (single include point)
extern const GFXfont AtkinsonHyperlegible_Regular6pt7b;
extern const GFXfont AtkinsonHyperlegible_Regular8pt7b;
extern const GFXfont AtkinsonHyperlegible_Regular10pt7b;
extern const GFXfont AtkinsonHyperlegible_Bold6pt7b;
extern const GFXfont AtkinsonHyperlegible_Bold8pt7b;
extern const GFXfont AtkinsonHyperlegible_Bold10pt7b;
extern const GFXfont SourceSerif4_Regular6pt7b;
extern const GFXfont SourceSerif4_Regular7pt7b;
extern const GFXfont SourceSerif4_Regular9pt7b;
extern const GFXfont SourceSerif4_SemiBold6pt7b;
extern const GFXfont SourceSerif4_SemiBold7pt7b;
extern const GFXfont SourceSerif4_SemiBold9pt7b;

// UI fonts
extern const GFXfont SourceCodePro_Regular5pt7b;
extern const GFXfont SourceCodePro_Regular7pt7b;

inline const GFXfont* getReadingFont(int family, int size) {
  static const GFXfont* table[4][3] = {
    { &AtkinsonHyperlegible_Regular6pt7b, &AtkinsonHyperlegible_Regular8pt7b, &AtkinsonHyperlegible_Regular10pt7b },
    { &AtkinsonHyperlegible_Bold6pt7b,    &AtkinsonHyperlegible_Bold8pt7b,    &AtkinsonHyperlegible_Bold10pt7b    },
    { &SourceSerif4_Regular6pt7b,         &SourceSerif4_Regular7pt7b,         &SourceSerif4_Regular9pt7b          },
    { &SourceSerif4_SemiBold6pt7b,        &SourceSerif4_SemiBold7pt7b,        &SourceSerif4_SemiBold9pt7b         },
  };
  if (family < 0 || family > 3) family = 0;
  if (size   < 0 || size   > 2) size   = 1;
  return table[family][size];
}

#define UI_FONT   (&SourceCodePro_Regular7pt7b)
#define UI_FONT_S (&SourceCodePro_Regular5pt7b)
