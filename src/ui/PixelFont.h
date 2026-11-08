// PixelFont.h: a small, plain pixel font for the HUD and story text (ui helper).
//
// raylib's default font is kept for the big titles (it has character), but at small sizes its
// squared "N" reads like "H", its "Y" and "4" look alike, and its apostrophe is slanted like an
// acute accent. This font is drawn in code from a readable glyph table: caps and digits 5 x 7,
// lower case with real ascenders and descenders, straight quote marks.
//
// It uses the same metrics as the default font: baseSize 10, so drawing at 10 * t with
// spacing t makes every font pixel exactly t x t window pixels (crisp), and capital letters
// are 7 font pixels tall starting at the top of the line.
#pragma once
#include "raylib.h"

namespace ui {
const Font& smallFont();   // built on first use (needs the window to exist)
}
