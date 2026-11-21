#include "cell_canvas.h"

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

void CellCanvas::render(Framebuffer& fb, const CellGrid& grid) {
    if (redraw_all_) fb.clear(kBackground);
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const size_t i = static_cast<size_t>(r * cols_ + c);
            if (!redraw_all_) {
                if (now_[i] == shown_[i]) continue;
                fill_cell(fb, grid, c, r, kBackground);
            }
            draw_symbol(fb, grid, c, r, now_[i].glyph, now_[i].color);
            shown_[i] = now_[i];
        }
    }
    redraw_all_ = false;
}

}  // namespace ofx
