#include "omarchy_menu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "grok_face.h"
#include "menu_fonts.h"

namespace omenu {
namespace {

constexpr float kPi = 3.14159265f;
constexpr int kMaskSize = 52; // Icons are drawn once at the big tile's icon size (72 * 0.72).
constexpr int kIcons = static_cast<int>(Icon::Count);

// The Colour look (approved mockup).
constexpr uint32_t kBg = 0x12141A, kSurface = 0x242832, kText = 0xF0F2F6, kMuted = 0x8C92A0, kAccent = 0x5AC8FA;
constexpr uint32_t kCharge = 0x34C759, kLow = 0xEB4646;
constexpr float kRowY = 57;                    // Tile centre line.
constexpr float kSmall = 48, kBig = 72, kGap = 12;
constexpr float kStep = kSmall + kGap;
constexpr float kIconShare = 0.72f;            // Icon size as a share of its tile.
constexpr int kLabelY = 92, kDescY = 111, kDotsY = 131; // Tops of the text lines; the dots' centre.

struct Rgb {
    int r, g, b;
};

Rgb rgb(uint32_t c) { return {static_cast<int>(c >> 16 & 255), static_cast<int>(c >> 8 & 255), static_cast<int>(c & 255)}; }

uint16_t pack(Rgb c) {
    const uint16_t v = static_cast<uint16_t>(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
    return static_cast<uint16_t>(v >> 8 | v << 8);
}

Rgb unpack(uint16_t s) {
    const uint16_t v = static_cast<uint16_t>(s >> 8 | s << 8);
    return {((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31};
}

Rgb mix(Rgb a, Rgb b, float t) {
    return {static_cast<int>(a.r + (b.r - a.r) * t + 0.5f), static_cast<int>(a.g + (b.g - a.g) * t + 0.5f),
            static_cast<int>(a.b + (b.b - a.b) * t + 0.5f)};
}

float cover(float distance) { return std::clamp(0.5f - distance, 0.0f, 1.0f); }

// Rows top .. top + h - 1 of the screen, so the status strip shares the drawing code.
struct Canvas {
    uint16_t *px;
    int h;
    int top;

    void blend(int x, int y, Rgb c, float a) {
        y -= top;
        if (a <= 0 || x < 0 || x >= kWidth || y < 0 || y >= h) return;
        uint16_t &p = px[y * kWidth + x];
        p = a >= 1 ? pack(c) : pack(mix(unpack(p), c, a));
    }
    void fill(Rgb c) { std::fill(px, px + kWidth * h, pack(c)); }
};

// ---------- signed distances (negative inside) ----------

float sdSegment(float px, float py, float ax, float ay, float bx, float by) {
    const float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    const float len2 = bax * bax + bay * bay;
    const float h = len2 > 0 ? std::clamp((pax * bax + pay * bay) / len2, 0.0f, 1.0f) : 0.0f;
    const float dx = pax - bax * h, dy = pay - bay * h;
    return std::sqrt(dx * dx + dy * dy);
}

float sdRoundBox(float px, float py, float cx, float cy, float hw, float hh, float r) {
    const float qx = std::fabs(px - cx) - (hw - r), qy = std::fabs(py - cy) - (hh - r);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

// Distance to a polygon's outline, negative inside (even-odd rule).
float sdPolygon(float px, float py, const float *xy, int n) {
    float best = 1e9f;
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
        best = std::min(best, sdSegment(px, py, xj, yj, xi, yi));
        if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
    }
    return inside ? -best : best;
}

// Distance to an arc of radius r swept clockwise from start to end (degrees, 0 = right,
// 90 = down), with round ends.
float sdArc(float px, float py, float cx, float cy, float r, float start, float end) {
    const float dx = px - cx, dy = py - cy;
    float a = std::atan2(dy, dx) * 180 / kPi;
    float sweep = end - start;
    float rel = std::fmod(a - start + 720.0f, 360.0f);
    if (rel <= sweep) return std::fabs(std::sqrt(dx * dx + dy * dy) - r);
    const float sa = start * kPi / 180, ea = end * kPi / 180;
    const float d1 = std::hypot(px - (cx + r * std::cos(sa)), py - (cy + r * std::sin(sa)));
    const float d2 = std::hypot(px - (cx + r * std::cos(ea)), py - (cy + r * std::sin(ea)));
    return std::min(d1, d2);
}

// ---------- icons: drawn once into coverage masks, in a 64 x 64 design grid ----------

class Painter {
public:
    explicit Painter(uint8_t *mask) : m_(mask) { std::memset(m_, 0, kMaskSize * kMaskSize); }

    static constexpr float kLine = 4; // Stroke width, in grid units.

    void segment(float ax, float ay, float bx, float by, float w = kLine) {
        shape(std::min(ax, bx) - w, std::min(ay, by) - w, std::max(ax, bx) + w, std::max(ay, by) + w,
              [&](float x, float y) { return sdSegment(x, y, ax, ay, bx, by) - w / 2; });
    }
    void polyline(std::initializer_list<float> xy, float w = kLine) {
        const float *p = xy.begin();
        for (size_t i = 0; i + 3 < xy.size(); i += 2) segment(p[i], p[i + 1], p[i + 2], p[i + 3], w);
    }
    void ring(float cx, float cy, float r, float w = kLine) {
        shape(cx - r - w, cy - r - w, cx + r + w, cy + r + w,
              [&](float x, float y) { return std::fabs(std::hypot(x - cx, y - cy) - r) - w / 2; });
    }
    void disc(float cx, float cy, float r) {
        shape(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, [&](float x, float y) { return std::hypot(x - cx, y - cy) - r; });
    }
    void ellipseRing(float cx, float cy, float rx, float ry, float w = kLine) {
        shape(cx - rx - w, cy - ry - w, cx + rx + w, cy + ry + w, [&](float x, float y) {
            const float px = x - cx, py = y - cy;
            const float k0 = std::hypot(px / rx, py / ry), k1 = std::hypot(px / (rx * rx), py / (ry * ry));
            return std::fabs(k1 > 0 ? k0 * (k0 - 1) / k1 : -std::min(rx, ry)) - w / 2;
        });
    }
    void box(float x0, float y0, float x1, float y1, float r, bool filled, float w = kLine) {
        const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
        shape(x0 - w, y0 - w, x1 + w, y1 + w, [&](float x, float y) {
            const float d = sdRoundBox(x, y, cx, cy, hw, hh, r);
            return filled ? d : std::fabs(d) - w / 2;
        });
    }
    void polygon(const float *xy, int n, bool filled, float w = kLine) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int i = 0; i < n; ++i) {
            x0 = std::min(x0, xy[2 * i]), x1 = std::max(x1, xy[2 * i]);
            y0 = std::min(y0, xy[2 * i + 1]), y1 = std::max(y1, xy[2 * i + 1]);
        }
        shape(x0 - w, y0 - w, x1 + w, y1 + w, [&](float x, float y) {
            const float d = sdPolygon(x, y, xy, n);
            return filled ? d : std::fabs(d) - w / 2;
        });
    }
    void arc(float cx, float cy, float r, float start, float end, float w = kLine) {
        shape(cx - r - w, cy - r - w, cx + r + w, cy + r + w,
              [&](float x, float y) { return sdArc(x, y, cx, cy, r, start, end) - w / 2; });
    }
    // Cuts a round hole through what is drawn so far.
    void hole(float cx, float cy, float r) {
        const float unit = 64.0f / kMaskSize;
        for (int j = 0; j < kMaskSize; ++j) {
            for (int i = 0; i < kMaskSize; ++i) {
                const float c = cover((std::hypot((i + 0.5f) * unit - cx, (j + 0.5f) * unit - cy) - r) / unit);
                m_[j * kMaskSize + i] = static_cast<uint8_t>(m_[j * kMaskSize + i] * (1 - c));
            }
        }
    }

private:
    // Coverage of each mask pixel in the box, from a distance function in grid units.
    template <typename Fn> void shape(float gx0, float gy0, float gx1, float gy1, Fn distance) {
        const float unit = 64.0f / kMaskSize; // Grid units per mask pixel.
        const int i0 = std::max(0, static_cast<int>(gx0 / unit) - 1), i1 = std::min(kMaskSize - 1, static_cast<int>(gx1 / unit) + 1);
        const int j0 = std::max(0, static_cast<int>(gy0 / unit) - 1), j1 = std::min(kMaskSize - 1, static_cast<int>(gy1 / unit) + 1);
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const float c = cover(distance((i + 0.5f) * unit, (j + 0.5f) * unit) / unit);
                uint8_t &m = m_[j * kMaskSize + i];
                m = std::max(m, static_cast<uint8_t>(c * 255 + 0.5f));
            }
        }
    }
    uint8_t *m_;
};

void paintIcon(Icon icon, uint8_t *mask) {
    Painter p(mask);
    constexpr float L = Painter::kLine;
    switch (icon) {
        case Icon::Sd: {
            const float card[] = {18, 10, 38, 10, 46, 18, 46, 54, 18, 54};
            p.polygon(card, 5, false);
            for (float x : {25.0f, 31.0f, 37.0f}) p.segment(x, 16, x, 24, L * 0.9f);
            break;
        }
        case Icon::Install:
            p.segment(32, 10, 32, 38);
            p.polyline({21, 28, 32, 39, 43, 28});
            p.polyline({14, 40, 14, 52, 50, 52, 50, 40});
            break;
        case Icon::Web:
            p.ring(32, 32, 21);
            p.ellipseRing(32, 32, 9, 21);
            p.segment(11, 32, 53, 32, L * 0.9f);
            p.segment(15, 21, 49, 21, L * 0.8f);
            p.segment(15, 43, 49, 43, L * 0.8f);
            break;
        case Icon::Usb: {
            p.segment(32, 12, 32, 52);
            p.polyline({32, 30, 20, 22, 20, 16});
            p.polyline({32, 36, 44, 28, 44, 22});
            p.disc(32, 52, 5);
            const float arrow[] = {32, 6, 27, 14, 37, 14};
            p.polygon(arrow, 3, true);
            p.box(41, 16, 47, 22, 0, true);
            p.disc(20, 14, 4);
            break;
        }
        case Icon::Partitions: {
            const float rows[3][2] = {{14, 0.8f}, {29, 0.55f}, {44, 0.3f}};
            for (const auto &row : rows) {
                p.box(12, row[0], 52, row[0] + 10, 3, false);
                p.box(12, row[0], 12 + 40 * row[1], row[0] + 10, 3, true);
            }
            break;
        }
        case Icon::Settings: {
            constexpr int kTeeth = 8;
            float gear[kTeeth * 4 * 2];
            int n = 0;
            for (int i = 0; i < kTeeth * 2; ++i) {
                const float a = 2 * kPi * i / (kTeeth * 2);
                const float r = i % 2 == 0 ? 21 : 15;
                for (float k : {-0.22f, 0.22f}) {
                    gear[2 * n] = 32 + r * std::cos(a + k * kPi / kTeeth);
                    gear[2 * n + 1] = 32 + r * std::sin(a + k * kPi / kTeeth);
                    ++n;
                }
            }
            p.polygon(gear, n, true);
            p.hole(32, 32, 7);
            break;
        }
        case Icon::Screensaver:
            p.box(10, 12, 54, 44, 4, false);
            p.segment(26, 54, 38, 54);
            p.segment(32, 45, 32, 53, L * 0.9f);
            p.box(24, 20, 40, 36, 0, false);
            p.box(29, 25, 35, 31, 0, true);
            break;
        case Icon::Power:
            p.arc(32, 32, 19, -60, 240);
            p.segment(32, 8, 32, 30);
            break;
        case Icon::App:
            p.box(14, 14, 30, 30, 3, true);
            p.box(34, 14, 50, 30, 3, true);
            p.box(14, 34, 30, 50, 3, true);
            p.box(34, 34, 50, 50, 3, false);
            break;
        case Icon::Grok: // Drawn live with the Grok face instead (lib/grok).
        case Icon::Count: break;
    }
}

// ---------- text ----------

int textWidth(const Font &f, const char *s) {
    int w = 0;
    for (; *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        w += f.glyphs[(c < f.first || c > f.last ? '?' : c) - f.first].advance;
    }
    return w;
}

void drawText(Canvas &cv, const Font &f, int x, int y, const char *s, Rgb colour) {
    for (; *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        const Glyph &g = f.glyphs[(c < f.first || c > f.last ? '?' : c) - f.first];
        for (int row = 0; row < g.height; ++row) {
            for (int col = 0; col < g.width; ++col) {
                const uint32_t i = g.offset + row * g.width + col;
                const int level = (f.bits[i / 2] >> (i % 2 ? 0 : 4)) & 15;
                if (level) cv.blend(x + g.x + col, y + g.y + row, colour, level / 15.0f);
            }
        }
        x += g.advance;
    }
}

// Centred, shortened with "..." when wider than maxWidth.
void drawCentred(Canvas &cv, const Font &f, int y, const char *s, Rgb colour, int maxWidth) {
    char buf[64];
    std::strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';
    size_t len = std::strlen(buf);
    if (textWidth(f, buf) > maxWidth) {
        while (len > 0 && (buf[len] = '\0', textWidth(f, buf) + textWidth(f, "...") > maxWidth)) --len;
        std::strcat(buf, "...");
    }
    drawText(cv, f, (kWidth - textWidth(f, buf)) / 2, y, buf, colour);
}

// ---------- shapes drawn straight onto the screen ----------

void fillRoundRect(Canvas &cv, float cx, float cy, float w, float h, float r, Rgb colour) {
    const int x0 = static_cast<int>(std::floor(cx - w / 2)) - 1, x1 = static_cast<int>(std::ceil(cx + w / 2)) + 1;
    const int y0 = static_cast<int>(std::floor(cy - h / 2)) - 1, y1 = static_cast<int>(std::ceil(cy + h / 2)) + 1;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) cv.blend(x, y, colour, cover(sdRoundBox(x + 0.5f, y + 0.5f, cx, cy, w / 2, h / 2, r)));
    }
}

void strokeRoundRect(Canvas &cv, float x0, float y0, float x1, float y1, float r, float w, Rgb colour) {
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
    for (int y = static_cast<int>(y0) - 1; y <= static_cast<int>(y1) + 1; ++y) {
        for (int x = static_cast<int>(x0) - 1; x <= static_cast<int>(x1) + 1; ++x) {
            cv.blend(x, y, colour, cover(std::fabs(sdRoundBox(x + 0.5f, y + 0.5f, cx, cy, hw, hh, r)) - w / 2));
        }
    }
}

void strokeArc(Canvas &cv, float cx, float cy, float r, float start, float end, float w, Rgb colour) {
    for (int y = static_cast<int>(cy - r - w); y <= static_cast<int>(cy + r + w); ++y) {
        for (int x = static_cast<int>(cx - r - w); x <= static_cast<int>(cx + r + w); ++x) {
            cv.blend(x, y, colour, cover(sdArc(x + 0.5f, y + 0.5f, cx, cy, r, start, end) - w / 2));
        }
    }
}

void fillDisc(Canvas &cv, float cx, float cy, float r, Rgb colour) {
    for (int y = static_cast<int>(cy - r) - 1; y <= static_cast<int>(cy + r) + 1; ++y) {
        for (int x = static_cast<int>(cx - r) - 1; x <= static_cast<int>(cx + r) + 1; ++x) {
            cv.blend(x, y, colour, cover(std::hypot(x + 0.5f - cx, y + 0.5f - cy) - r));
        }
    }
}

void drawStatusBar(Canvas &cv, const Status &s) {
    const Rgb text = rgb(kText);
    drawText(cv, kFontBrand, 5, 2, s.name ? s.name : "", rgb(kAccent));
    int right = kWidth - 5;
    if (s.time && *s.time) {
        const int w = textWidth(kFontSmall, s.time);
        drawText(cv, kFontSmall, right - w, 2, s.time, text);
        right -= w + 8;
    }
    // Battery: outline, tip, and a fill for the charge left.
    const float bx = right - 25, by = 4;
    strokeRoundRect(cv, bx, by, bx + 22, by + 11, 2.5f, 1.2f, text);
    fillRoundRect(cv, bx + 23.5f, by + 5.5f, 2, 4.5f, 0.6f, text);
    if (s.battery >= 0) {
        const float level = std::clamp(s.battery / 100.0f, 0.0f, 1.0f);
        const uint32_t c = s.charging ? kCharge : s.battery <= 15 ? kLow : kAccent;
        const float w = std::max(2.0f, 16.0f * level);
        fillRoundRect(cv, bx + 3 + w / 2, by + 5.5f, w, 6, 1.2f, rgb(c));
    }
    // WiFi: three arcs, dim while not connected.
    const Rgb wifi = s.wifi ? text : rgb(kMuted);
    const float wx = bx - 14, wy = 15;
    for (float r : {3.0f, 6.5f, 10.0f}) strokeArc(cv, wx, wy, r, 225, 315, 1.9f, wifi);
    for (int x = 0; x < kWidth; ++x) cv.blend(x, kStatusHeight - 1, rgb(kSurface), 1);
}

} // namespace

bool Renderer::begin() {
    if (masks_) return true;
    masks_ = new (std::nothrow) uint8_t[kIcons * kMaskSize * kMaskSize];
    if (!masks_) return false;
    for (int i = 0; i < kIcons; ++i) paintIcon(static_cast<Icon>(i), masks_ + i * kMaskSize * kMaskSize);
    return true;
}

void Renderer::end() {
    delete[] masks_;
    masks_ = nullptr;
}

void Renderer::drawStatus(uint16_t *strip, const Status &status) const {
    Canvas cv{strip, kStatusHeight, 0};
    cv.fill(rgb(kBg));
    drawStatusBar(cv, status);
}

void Renderer::draw(uint16_t *frame, const Tile *tiles, int count, float position, const Status &status) const {
    Canvas cv{frame, kHeight, 0};
    const Rgb bg = rgb(kBg);
    cv.fill(bg);
    drawStatusBar(cv, status);
    if (count <= 0 || !masks_) return;

    for (int i = 0; i < count; ++i) {
        const float off = i - position;
        if (std::fabs(off) > 3) continue;
        const float near = std::max(0.0f, 1 - std::fabs(off)); // 1 when centred.
        const float size = kSmall + (kBig - kSmall) * near;
        const float cx = kWidth / 2.0f + off * kStep + std::copysign(1.0f, off) * (kBig - kSmall) / 2 * std::min(1.0f, std::fabs(off));
        const Tile &t = tiles[i];
        float strength = 0.55f + 0.45f * near; // Tiles away from the centre fade into the background.
        if (!t.active) strength *= 0.45f;
        const Rgb fill = mix(bg, rgb(t.colour), strength);
        fillRoundRect(cv, cx, kRowY, size, size, 10 + 4 * near, fill);

        const float icon = size * kIconShare;
        const int ix0 = static_cast<int>(std::floor(cx - icon / 2)), iy0 = static_cast<int>(std::floor(kRowY - icon / 2));
        const int isize = static_cast<int>(std::ceil(icon)) + 1;
        if (t.icon == Icon::Grok) {
            // Grok's own face: a black ball with two slanted eyes.
            constexpr int kFaceMax = 56; // Covers the big tile's icon (72 * 0.72, rounded up).
            static uint16_t face[kFaceMax * kFaceMax];
            grokface::Style st;
            st.region_x = ix0;
            st.region_y = iy0;
            st.region_w = std::min(isize, kFaceMax);
            st.region_h = std::min(isize, kFaceMax);
            st.centre_x = cx;
            st.centre_y = kRowY + icon / 64;
            st.radius = icon * 22 / 64;
            st.background = (static_cast<uint32_t>(fill.r) << 16) | (fill.g << 8) | fill.b;
            const Rgb ball = mix(fill, rgb(0x000000), t.active ? 1.0f : 0.5f);
            st.ball = (static_cast<uint32_t>(ball.r) << 16) | (ball.g << 8) | ball.b;
            const Rgb eyes = mix(ball, rgb(0xFFFFFF), 0.92f);
            st.eyes = (static_cast<uint32_t>(eyes.r) << 16) | (eyes.g << 8) | eyes.b;
            grokface::render(face, st, grokface::Pose{});
            for (int y = 0; y < st.region_h; ++y) {
                for (int x = 0; x < st.region_w; ++x) cv.blend(ix0 + x, iy0 + y, unpack(face[y * st.region_w + x]), 1);
            }
            continue;
        }
        const uint8_t *mask = masks_ + static_cast<int>(t.icon) * kMaskSize * kMaskSize;
        const Rgb ink = mix(fill, rgb(0xFFFFFF), t.active ? 1.0f : 0.5f);
        const float scale = kMaskSize / icon;
        for (int y = iy0; y < iy0 + isize; ++y) {
            for (int x = ix0; x < ix0 + isize; ++x) {
                // Bilinear sample of the mask at this pixel's centre.
                const float u = ((x + 0.5f) - (cx - icon / 2)) * scale - 0.5f;
                const float v = ((y + 0.5f) - (kRowY - icon / 2)) * scale - 0.5f;
                if (u < -1 || v < -1 || u >= kMaskSize || v >= kMaskSize) continue;
                const int u0 = static_cast<int>(std::floor(u)), v0 = static_cast<int>(std::floor(v));
                const float fu = u - u0, fv = v - v0;
                auto at = [&](int a, int b) {
                    return a < 0 || b < 0 || a >= kMaskSize || b >= kMaskSize ? 0.0f : mask[b * kMaskSize + a] / 255.0f;
                };
                const float c = (at(u0, v0) * (1 - fu) + at(u0 + 1, v0) * fu) * (1 - fv) +
                                (at(u0, v0 + 1) * (1 - fu) + at(u0 + 1, v0 + 1) * fu) * fv;
                cv.blend(x, y, ink, c);
            }
        }
    }

    const int nearest = std::clamp(static_cast<int>(std::lround(position)), 0, count - 1);
    drawCentred(cv, kFontLabel, kLabelY, tiles[nearest].label, rgb(kText), kWidth - 10);
    drawCentred(cv, kFontSmall, kDescY, tiles[nearest].description, rgb(kMuted), kWidth - 10);
    for (int i = 0; i < count; ++i) {
        const float x = kWidth / 2.0f + (i - (count - 1) / 2.0f) * 6;
        fillDisc(cv, x, kDotsY, i == nearest ? 1.6f : 1.0f, i == nearest ? rgb(kAccent) : rgb(kSurface));
    }
}

float spring(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t >= 1 ? 1 : 1 - std::exp(-6 * t) * std::cos(4.5f * t);
}

} // namespace omenu
