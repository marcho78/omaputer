// One frame of character cells. Effects paint visible characters in ascending id order,
// so the highest id wins a shared cell, as in ttfx. render() redraws only changed cells.
#pragma once

#include <cstdint>
#include <vector>

#include "raster.h"

namespace ofx {

class CellCanvas {
public:
    void reset(int cols, int rows);  // Also forces a full redraw on the next render().
    void clear();

    // Puts a character at canvas position (col, row): 1-based with row 1 at the bottom,
    // as in ttfx. Positions off the canvas are ignored.
    void put(int col, int row, uint32_t glyph, Rgb color);

    // Draws the cells that changed since the previous render, or all of them the first time.
    void render(Framebuffer& fb, const CellGrid& grid);

private:
    struct Cell {
        uint32_t glyph = ' ';
        Rgb color;
        bool operator==(const Cell& o) const { return glyph == o.glyph && color == o.color; }
    };

    int cols_ = 0;
    int rows_ = 0;
    std::vector<Cell> now_;
    std::vector<Cell> shown_;
    bool redraw_all_ = true;
};

}  // namespace ofx
