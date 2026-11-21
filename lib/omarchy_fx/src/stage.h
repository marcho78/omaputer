// The canvas and the logo placed on it, laid out like ttfx with
// --anchor-canvas c --anchor-text c (src/engine/canvas.rs, terminal.rs).
#pragma once

#include <cstdint>
#include <vector>

#include "color.h"
#include "logo.h"

namespace ofx {

// Columns and rows are 1-based and row 1 is the BOTTOM row, as in ttfx.
struct StageChar {
    int16_t col;
    int16_t row;
    uint32_t glyph;
};

struct Stage {
    Stage() = default;
    Stage(const Logo& logo, int cols, int rows);

    int width = 0;
    int height = 0;
    int center_col = 1;
    int center_row = 1;
    TextBox text;                  // Bounding box of the placed characters.
    std::vector<StageChar> chars;  // ttfx id order: top line first, each line left to right.
};

}  // namespace ofx
