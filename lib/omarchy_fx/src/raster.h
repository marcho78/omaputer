// Pixel output shared by the device and the laptop preview, so both show identical frames.
#pragma once

#include <cstdint>

namespace ofx {

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

inline bool operator==(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 135;

// One screen of RGB565 pixels with their two bytes swapped. That is the order the
// Cardputer's ST7789 display expects, so a frame is sent to it without conversion.
class Framebuffer {
public:
    void clear(Rgb color);
    void fill_rect(int x, int y, int w, int h, Rgb color);  // Clipped to the screen.
    void set_pixel(int x, int y, Rgb color);                // Ignored off screen.
    Rgb pixel(int x, int y) const;                          // Reads back at RGB565 precision.
    const uint16_t* data() const { return px_; }

private:
    uint16_t px_[kScreenWidth * kScreenHeight] = {};
};

// Lays a canvas of character cells over the screen. Every row is 6 px tall, matching a
// terminal cell's 1:2 shape; columns share the 240 px width as evenly as possible
// (81 columns come out 2 or 3 px wide). The rows are centered vertically.
class CellGrid {
public:
    static constexpr int kRowHeight = 6;
    static constexpr int kMaxRows = kScreenHeight / kRowHeight;  // 22

    CellGrid(int cols, int rows);  // cols is clamped to 1..240, rows to 1..kMaxRows.

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int x(int col) const { return col * kScreenWidth / cols_; }
    int width(int col) const { return x(col + 1) - x(col); }
    int y(int row) const { return top_ + row * kRowHeight; }

private:
    int cols_;
    int rows_;
    int top_;
};

// Draws one character into its cell; row 0 is the top row. Block elements are drawn as
// shapes, printable ASCII with the 3x5 Tom Thumb font, and any other code point as a
// fixed pseudo-glyph derived from it, so random symbols still read as characters.
// Cells outside the grid are ignored.
void draw_symbol(Framebuffer& fb, const CellGrid& grid, int col, int row, uint32_t symbol, Rgb color);

// Fills one whole cell with a color; row 0 is the top row. Cells outside the grid are ignored.
void fill_cell(Framebuffer& fb, const CellGrid& grid, int col, int row, Rgb color);

}  // namespace ofx
