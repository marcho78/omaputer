#include "color.h"

#include <algorithm>

#include "fx_math.h"

namespace ofx {
namespace {

uint8_t clamp_channel(int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); }

Rgb step_towards(Rgb from, Rgb to, int steps, int i) {
    return {clamp_channel(from.r + floor_div(to.r - from.r, steps) * i),
            clamp_channel(from.g + floor_div(to.g - from.g, steps) * i),
            clamp_channel(from.b + floor_div(to.b - from.b, steps) * i)};
}

double hue_to_channel(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 1.0 / 2) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

}  // namespace

std::vector<Rgb> spectrum(const std::vector<Rgb>& stops, const std::vector<int>& steps) {
    std::vector<Rgb> out;
    if (stops.empty() || steps.empty()) return out;
    if (stops.size() == 1) {
        out.assign(static_cast<size_t>(std::max(steps[0], 0)), stops[0]);
        return out;
    }
    for (size_t p = 0; p + 1 < stops.size(); ++p) {
        const int s = std::max(steps[std::min(p, steps.size() - 1)], 1);
        for (int i = out.empty() ? 0 : 1; i < s; ++i) out.push_back(step_towards(stops[p], stops[p + 1], s, i));
        out.push_back(stops[p + 1]);
    }
    return out;
}

Rgb spectrum_color(Rgb from, Rgb to, int steps, int i) {
    steps = std::max(steps, 1);
    return i >= steps ? to : step_towards(from, to, steps, i);
}

Rgb color_at(const std::vector<Rgb>& spectrum, double fraction) {
    if (spectrum.empty()) return {};
    const size_t n = spectrum.size();
    for (size_t i = 1; i <= n; ++i) {
        if (fraction <= static_cast<double>(i) / static_cast<double>(n)) return spectrum[i - 1];
    }
    return spectrum.back();
}

double gradient_fraction(Direction direction, const TextBox& box, int col, int row) {
    const double r = row - box.bottom + 1;
    const double c = col - box.left + 1;
    const double h = box.top - box.bottom + 1;
    const double w = box.right - box.left + 1;
    switch (direction) {
        case Direction::Vertical:
            return r / h;
        case Direction::Diagonal:
            return (2 * r + c) / (2 * h + w);
    }
    return 0;
}

Rgb adjust_brightness(Rgb color, double brightness) {
    const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
    const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double h = 0, s = 0, l = (mx + mn) / 2;
    if (mx != mn) {
        const double d = mx - mn;
        s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == r) {
            h = (g - b) / d + (g < b ? 6 : 0);
        } else if (mx == g) {
            h = (b - r) / d + 2;
        } else {
            h = (r - g) / d + 4;
        }
        h /= 6;
    }
    l = std::clamp(l * brightness, 0.0, 1.0);

    double red = l, green = l, blue = l;
    if (s != 0) {
        const double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        const double p = 2 * l - q;
        red = hue_to_channel(p, q, h + 1.0 / 3);
        green = hue_to_channel(p, q, h);
        blue = hue_to_channel(p, q, h - 1.0 / 3);
    }
    return {static_cast<uint8_t>(rhe(red * 255)), static_cast<uint8_t>(rhe(green * 255)),
            static_cast<uint8_t>(rhe(blue * 255))};
}

}  // namespace ofx
