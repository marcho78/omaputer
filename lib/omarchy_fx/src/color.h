// Colors and gradients computed the way ttfx does (src/utils/graphics.rs).
#pragma once

#include <cstdint>
#include <vector>

#include "raster.h"

namespace ofx {

constexpr Rgb hex(uint32_t rgb) {
    return {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8), static_cast<uint8_t>(rgb)};
}

// A gradient's colors: steps[p] colors from stop p towards stop p + 1, landing exactly on
// each stop. If fewer steps than stop pairs are given, the last value repeats. A single
// stop is repeated steps[0] times. Two or more stops give 1 + sum(steps) colors.
std::vector<Rgb> spectrum(const std::vector<Rgb>& stops, const std::vector<int>& steps);

// spectrum({from, to}, {steps})[i] without building the vector; 0 <= i <= steps.
Rgb spectrum_color(Rgb from, Rgb to, int steps, int i);

// The spectrum's color for a fraction in [0, 1].
Rgb color_at(const std::vector<Rgb>& spectrum, double fraction);

// Where a cell falls in a gradient laid over the text's bounding box.
enum class Direction { Vertical, Diagonal };

struct TextBox {
    int left = 1;
    int right = 1;
    int bottom = 1;
    int top = 1;
};

double gradient_fraction(Direction direction, const TextBox& box, int col, int row);

// Scales a color's HSL lightness (ttfx adjust_color_brightness).
Rgb adjust_brightness(Rgb color, double brightness);

}  // namespace ofx
