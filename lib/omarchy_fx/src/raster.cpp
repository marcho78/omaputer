#include "raster.h"

#include "tomthumb.h"

namespace ofx {
namespace {

uint16_t encode(Rgb c) {
    const uint16_t v = static_cast<uint16_t>(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

Rgb decode(uint16_t swapped) {
    const uint16_t v = static_cast<uint16_t>((swapped >> 8) | (swapped << 8));
    const int r5 = v >> 11;
    const int g6 = (v >> 5) & 0x3F;
    const int b5 = v & 0x1F;
    return {static_cast<uint8_t>((r5 << 3) | (r5 >> 2)), static_cast<uint8_t>((g6 << 2) | (g6 >> 4)),
            static_cast<uint8_t>((b5 << 3) | (b5 >> 2))};
}

int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Cell {
    int x, y, w, h;
};

// Quadrant blocks U+2596..U+259F as bit sets: 1 upper-left, 2 upper-right, 4 lower-left, 8 lower-right.
constexpr uint8_t kQuadrants[] = {4, 8, 1, 13, 9, 7, 11, 2, 6, 14};

bool draw_block_element(Framebuffer& fb, Cell c, uint32_t cp, Rgb color) {
    if (cp < 0x2580 || cp > 0x259F) return false;

    if (cp == 0x2580) {  // ▀ upper half
        fb.fill_rect(c.x, c.y, c.w, c.h / 2, color);
    } else if (cp <= 0x2588) {  // ▁▂▃▄▅▆▇█ lower eighths, up to full
        const int h = (c.h * static_cast<int>(cp - 0x2580) + 4) / 8;
        fb.fill_rect(c.x, c.y + c.h - h, c.w, h, color);
    } else if (cp <= 0x258F) {  // ▉▊▋▌▍▎▏ left seven-eighths down to one-eighth
        const int eighths = 8 - static_cast<int>(cp - 0x2588);
        const int w = (c.w * eighths + 4) / 8;
        fb.fill_rect(c.x, c.y, w < 1 ? 1 : w, c.h, color);
    } else if (cp == 0x2590) {  // ▐ right half
        fb.fill_rect(c.x + c.w / 2, c.y, c.w - c.w / 2, c.h, color);
    } else if (cp <= 0x2593) {  // ░▒▓ shades, as fixed dither patterns
        for (int y = c.y; y < c.y + c.h; ++y) {
            for (int x = c.x; x < c.x + c.w; ++x) {
                const bool on = cp == 0x2591 ? (x + 2 * y) % 4 == 0
                              : cp == 0x2592 ? (x + y) % 2 == 0
                                             : (x + 2 * y) % 4 != 0;
                if (on) fb.set_pixel(x, y, color);
            }
        }
    } else if (cp == 0x2594) {  // ▔ upper eighth
        fb.fill_rect(c.x, c.y, c.w, 1, color);
    } else if (cp == 0x2595) {  // ▕ right eighth
        fb.fill_rect(c.x + c.w - 1, c.y, 1, c.h, color);
    } else {  // ▖▗▘▙▚▛▜▝▞▟ quadrants
        const uint8_t q = kQuadrants[cp - 0x2596];
        const int hw = c.w / 2, hh = c.h / 2;
        if (q & 1) fb.fill_rect(c.x, c.y, c.w - hw, hh, color);
        if (q & 2) fb.fill_rect(c.x + c.w - hw, c.y, hw, hh, color);
        if (q & 4) fb.fill_rect(c.x, c.y + hh, c.w - hw, c.h - hh, color);
        if (q & 8) fb.fill_rect(c.x + c.w - hw, c.y + hh, hw, c.h - hh, color);
    }
    return true;
}

void draw_ascii(Framebuffer& fb, Cell c, uint32_t cp, Rgb color) {
    const tomthumb::Glyph& g = tomthumb::kGlyphs[cp - tomthumb::kFirst];
    const int baseline = c.y + tomthumb::kLineHeight - 1;
    int bit = 0;
    for (int gy = 0; gy < g.height; ++gy) {
        for (int gx = 0; gx < g.width; ++gx, ++bit) {
            if (!(tomthumb::kBitmaps[g.offset + bit / 8] & (0x80 >> (bit % 8)))) continue;
            const int x = c.x + g.x_offset + gx;
            if (x < c.x + c.w) fb.set_pixel(x, baseline + g.y_offset + gy, color);
        }
    }
}

// A 3x5 pattern hashed from the code point. The centre pixel is always lit so no
// symbol comes out blank.
void draw_pseudo_glyph(Framebuffer& fb, Cell c, uint32_t cp, Rgb color) {
    uint32_t h = cp * 2654435761u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    for (int gy = 0; gy < 5; ++gy) {
        for (int gx = 0; gx < 3 && gx < c.w; ++gx) {
            if ((gy == 2 && gx == 1) || ((h >> (gy * 3 + gx)) & 1)) fb.set_pixel(c.x + gx, c.y + gy, color);
        }
    }
}

}  // namespace

void Framebuffer::clear(Rgb color) {
    const uint16_t v = encode(color);
    for (auto& p : px_) p = v;
}

void Framebuffer::fill_rect(int x, int y, int w, int h, Rgb color) {
    const int x0 = clamp(x, 0, kScreenWidth), x1 = clamp(x + w, 0, kScreenWidth);
    const int y0 = clamp(y, 0, kScreenHeight), y1 = clamp(y + h, 0, kScreenHeight);
    const uint16_t v = encode(color);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) px_[yy * kScreenWidth + xx] = v;
    }
}

void Framebuffer::set_pixel(int x, int y, Rgb color) {
    if (x < 0 || x >= kScreenWidth || y < 0 || y >= kScreenHeight) return;
    px_[y * kScreenWidth + x] = encode(color);
}

Rgb Framebuffer::pixel(int x, int y) const {
    if (x < 0 || x >= kScreenWidth || y < 0 || y >= kScreenHeight) return {};
    return decode(px_[y * kScreenWidth + x]);
}

CellGrid::CellGrid(int cols, int rows)
    : cols_(clamp(cols, 1, kScreenWidth)),
      rows_(clamp(rows, 1, kMaxRows)),
      top_((kScreenHeight - rows_ * kRowHeight) / 2) {}

void fill_cell(Framebuffer& fb, const CellGrid& grid, int col, int row, Rgb color) {
    if (col < 0 || col >= grid.cols() || row < 0 || row >= grid.rows()) return;
    fb.fill_rect(grid.x(col), grid.y(row), grid.width(col), CellGrid::kRowHeight, color);
}

void draw_symbol(Framebuffer& fb, const CellGrid& grid, int col, int row, uint32_t symbol, Rgb color) {
    if (col < 0 || col >= grid.cols() || row < 0 || row >= grid.rows() || symbol == ' ') return;
    const Cell cell{grid.x(col), grid.y(row), grid.width(col), CellGrid::kRowHeight};
    if (draw_block_element(fb, cell, symbol, color)) return;
    if (symbol > tomthumb::kFirst && symbol <= tomthumb::kLast) {
        draw_ascii(fb, cell, symbol, color);
    } else {
        draw_pseudo_glyph(fb, cell, symbol, color);
    }
}

}  // namespace ofx
