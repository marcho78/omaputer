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

// One row of character cells: the screen's full width and 6 px (CellGrid::kRowHeight) tall,
// in RGB565 with its two bytes swapped. That is the order the Cardputer's ST7789 display
// expects, so a strip is sent to it without conversion. Effects draw a frame a strip at a
// time: 2.9 KB of RAM, where a whole screen takes 64.8 KB, which the Cardputer can't spare
// while WiFi is on. Drawing takes screen coordinates; move_to() picks the screen rows the
// strip holds, and whatever falls outside them is clipped.
class Strip {
public:
    static constexpr int kHeight = 6;

    void move_to(int top) { top_ = top; }  // The first screen row the strip holds.
    int top() const { return top_; }
    void clear(Rgb color);
    void fill_rect(int x, int y, int w, int h, Rgb color);  // Clipped to the strip.
    void set_pixel(int x, int y, Rgb color);                // Ignored outside the strip.
    Rgb pixel(int x, int y) const;                          // Reads back at RGB565 precision.
    const uint16_t* data() const { return px_; }

private:
    int top_ = 0;
    uint16_t px_[kScreenWidth * kHeight] = {};
};

// Where an effect sends each strip it draws: the display on the Cardputer, a Framebuffer on
// the laptop.
class StripSink {
public:
    virtual void show(const Strip& strip) = 0;

protected:
    ~StripSink() = default;
};

// A whole screen, for the laptop tools that look at complete frames (the tests, the previews,
// the ttfx parity check): it keeps every strip it is shown. It starts black.
class Framebuffer : public StripSink {
public:
    void show(const Strip& strip) override;  // Copies the strip onto its rows.
    Rgb pixel(int x, int y) const;           // Reads back at RGB565 precision.
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

static_assert(Strip::kHeight == CellGrid::kRowHeight, "a strip holds one row of cells");

// Draws one character into its cell; row 0 is the top row, and the strip must hold that row
// (move_to(grid.y(row))). Every character stays inside its cell, which is what lets a frame
// be drawn a row at a time. Block elements are drawn as shapes, printable ASCII with the 3x5
// Tom Thumb font, and any other code point as a fixed pseudo-glyph derived from it, so random
// symbols still read as characters. Cells outside the grid are ignored.
void draw_symbol(Strip& strip, const CellGrid& grid, int col, int row, uint32_t symbol, Rgb color);

}  // namespace ofx
