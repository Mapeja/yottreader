#!/usr/bin/env python3
"""
Inspect and patch Adafruit GFX font headers.

Usage:
  # Show all glyphs as ASCII art
  python font_inspect.py src/fonts/AtkinsonHyperlegible_Bold8pt7b.h

  # Show a single character
  python font_inspect.py src/fonts/AtkinsonHyperlegible_Bold8pt7b.h --char a

  # Show a range of characters
  python font_inspect.py src/fonts/AtkinsonHyperlegible_Bold8pt7b.h --chars "abcdefg"

  # Patch a single pixel (toggle on/off) then write back to the file
  python font_inspect.py src/fonts/AtkinsonHyperlegible_Bold8pt7b.h --char l --set 2,3 --set 2,4

  # Clear a pixel
  python font_inspect.py src/fonts/AtkinsonHyperlegible_Bold8pt7b.h --char l --clear 2,3

Pixel coordinates: col,row  (0,0 = top-left of glyph bounding box)
"""

import sys, re, argparse, math

# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def _extract_array_body(text: str, keyword: str) -> str:
    """Find 'keyword = {' then return everything up to the matching closing brace."""
    m = re.search(keyword + r'\s*=\s*\{', text)
    if not m:
        return ""
    start = m.end()  # position just after the opening {
    depth = 1
    i = start
    while i < len(text) and depth > 0:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    return text[start:i - 1]  # body without the outer braces


def parse_font(path: str):
    with open(path, "r") as f:
        text = f.read()

    # Extract flat bitmap array (contains only hex bytes, no nested braces)
    bm_body = _extract_array_body(text, r'Bitmaps\[\]\s+PROGMEM')
    if not bm_body:
        sys.exit("Could not find Bitmaps array in " + path)
    bitmap = [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', bm_body)]

    # Extract glyph table — use brace-counting so ';' in comments doesn't truncate
    glyph_body = _extract_array_body(text, r'Glyphs\[\]\s+PROGMEM')
    if not glyph_body:
        sys.exit("Could not find Glyphs array in " + path)
    glyphs = []
    for m in re.finditer(r'\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}', glyph_body):
        glyphs.append(tuple(int(x) for x in m.groups()))  # offset,w,h,xa,xo,yo

    # Extract first/last char and yAdvance from GFXfont struct
    font_match = re.search(r'GFXfont\s+\w+\s+PROGMEM\s*=\s*\{[^,]*,[^,]*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*(\d+)', text)
    if not font_match:
        sys.exit("Could not find GFXfont struct in " + path)
    first = int(font_match.group(1), 16)
    last  = int(font_match.group(2), 16)
    y_adv = int(font_match.group(3))

    return bitmap, glyphs, first, last, y_adv, text


# ---------------------------------------------------------------------------
# Render
# ---------------------------------------------------------------------------

def glyph_bits(bitmap, glyph):
    offset, w, h, xa, xo, yo = glyph
    if w == 0 or h == 0:
        return []
    row_bytes = math.ceil(w / 8)
    rows = []
    for r in range(h):
        row = []
        for c in range(w):
            byte_idx = offset + r * row_bytes + (c >> 3)
            bit_idx  = 7 - (c & 7)
            row.append(1 if (bitmap[byte_idx] & (1 << bit_idx)) else 0)
        rows.append(row)
    return rows


def print_glyph(char_code, glyph, bitmap, y_adv):
    offset, w, h, xa, xo, yo = glyph
    c = chr(char_code)
    print(f"  '{c}'  (0x{char_code:02X})  w={w} h={h} xAdv={xa} xOff={xo} yOff={yo}  "
          f"offset={offset}  bytes={math.ceil(w/8)*h if w and h else 0}")

    if w == 0 or h == 0:
        print("  (empty glyph)\n")
        return

    rows = glyph_bits(bitmap, glyph)
    # Baseline indicator: yo+h == 0 means last row is baseline
    baseline_row = -yo - 1  # row index of baseline (0-indexed)

    for r, row in enumerate(rows):
        bar  = ">" if r == baseline_row else " "
        line = "".join("#" if p else "." for p in row)
        # Show col numbers every 4 cols
        print(f"  {bar} row{r:2d} | {line}")

    # Column index guide
    guide = "".join(str(c % 10) for c in range(w))
    print(f"          col  {guide}")
    print()


# ---------------------------------------------------------------------------
# Patch
# ---------------------------------------------------------------------------

def set_pixel(bitmap, glyph, col, row, value: int):
    offset, w, h, xa, xo, yo = glyph
    if col < 0 or col >= w or row < 0 or row >= h:
        print(f"  !! pixel ({col},{row}) out of bounds (w={w} h={h})")
        return
    row_bytes = math.ceil(w / 8)
    byte_idx  = offset + row * row_bytes + (col >> 3)
    bit_mask  = 1 << (7 - (col & 7))
    if value:
        bitmap[byte_idx] |=  bit_mask
    else:
        bitmap[byte_idx] &= ~bit_mask


def write_back(path: str, original_text: str, bitmap: list[int]):
    # Replace just the bitmap hex values, preserving everything else
    bm_match = re.search(r'(Bitmaps\[\]\s+PROGMEM\s*=\s*\{)([^}]+)(\})', original_text)
    if not bm_match:
        sys.exit("Cannot locate Bitmaps array for write-back")

    hex_values = [f"0x{b:02X}" for b in bitmap]
    rows = []
    for i in range(0, len(hex_values), 16):
        rows.append("  " + ", ".join(hex_values[i:i+16]) + ",")
    new_body = "\n" + "\n".join(rows) + "\n"

    new_text = (original_text[:bm_match.start(2)] +
                new_body +
                original_text[bm_match.end(2):])

    with open(path, "w") as f:
        f.write(new_text)
    print(f"Written: {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(description="Inspect/patch Adafruit GFX font headers")
    p.add_argument("font",   help="Path to font .h file")
    p.add_argument("--char",  help="Single character to inspect/patch")
    p.add_argument("--chars", help="String of characters to inspect")
    p.add_argument("--set",   action="append", metavar="col,row",
                   help="Set pixel at col,row (can repeat)")
    p.add_argument("--clear", action="append", metavar="col,row",
                   help="Clear pixel at col,row (can repeat)")
    args = p.parse_args()

    bitmap, glyphs, first, last, y_adv, original_text = parse_font(args.font)

    def get_glyph(ch):
        code = ord(ch)
        if code < first or code > last:
            print(f"Character '{ch}' (0x{code:02X}) not in font range 0x{first:02X}–0x{last:02X}")
            return None, None
        return code, glyphs[code - first]

    patched = False

    if args.char:
        code, glyph = get_glyph(args.char[0])
        if glyph:
            for spec in (args.set or []):
                col, row = map(int, spec.split(","))
                set_pixel(bitmap, glyph, col, row, 1)
                patched = True
            for spec in (args.clear or []):
                col, row = map(int, spec.split(","))
                set_pixel(bitmap, glyph, col, row, 0)
                patched = True
            print_glyph(code, glyph, bitmap, y_adv)

    elif args.chars:
        for ch in args.chars:
            code, glyph = get_glyph(ch)
            if glyph:
                print_glyph(code, glyph, bitmap, y_adv)

    else:
        # Print all
        for i, glyph in enumerate(glyphs):
            print_glyph(first + i, glyph, bitmap, y_adv)

    if patched:
        write_back(args.font, original_text, bitmap)


if __name__ == "__main__":
    main()
