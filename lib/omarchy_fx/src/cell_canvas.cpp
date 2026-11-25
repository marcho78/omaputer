#include "cell_canvas.h"

#include <algorithm>

namespace ofx {
namespace {
constexpr Rgb kBackground{0, 0, 0};  // Omarchy's screensaver sets a black terminal background.
}

void CellCanvas::reset(int cols, int rows) {
    cols_ = cols;
    rows_ = rows;
    now_.assign(static_cast<size_t>(cols * rows), Cell{});
    shown_ = now_;
    redraw_all_ = true;
}

void CellCanvas::clear() {
    for (Cell& c : now_) c = Cell{};
}

void CellCanvas::put(int col, int row, uint32_t glyph, Rgb color) {
    if (col < 1 || col > cols_ || row < 1 || row > rows_) return;
    // Storage runs top row first, matching the screen grid.
    Cell& cell = now_[static_cast<size_t>((rows_ - row) * cols_ + (col - 1))];
    cell = glyph == ' ' ? Cell{} : Cell{glyph, color};
}

void CellCanvas::render(Strip& strip, const CellGrid& grid, StripSink& sink) {
    for (int r = 0; r < rows_; ++r) {
        const auto now = now_.begin() + r * cols_;
        const auto shown = shown_.begin() + r * cols_;
        if (!redraw_all_ && std::equal(now, now + cols_, shown)) continue;
        // The strip holds only this row, so all of its cells are drawn, not just the changed ones.
        strip.move_to(grid.y(r));
        strip.clear(kBackground);
        for (int c = 0; c < cols_; ++c) draw_symbol(strip, grid, c, r, now[c].glyph, now[c].color);
        std::copy(now, now + cols_, shown);
        sink.show(strip);
    }
    redraw_all_ = false;
}

}  // namespace ofx
