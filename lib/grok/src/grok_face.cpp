#include "grok_face.h"

#include <algorithm>
#include <cmath>

namespace grokface {
namespace {

constexpr float kPi = 3.14159265f;

// The eyes, measured from the reference picture, in units of the ball's radius from its
// centre (y grows downwards). Both lean the same way, top towards the left, and the far
// eye is narrower, as if the ball were turned slightly to its left.
struct EyeShape {
    float x, y;      // Centre.
    float half;      // Half the length of the straight middle part.
    float round;     // Radius of the rounded ends, which is also half the width.
    float tilt_deg;  // Lean from upright.
};
constexpr EyeShape kEyes[2] = {
    {0.214f, -0.414f, 0.105f, 0.090f, 24.0f},
    {0.638f, -0.495f, 0.136f, 0.059f, 26.0f},
};

float wave(uint32_t t_ms, float period_ms) {
    return std::sin(2 * kPi * std::fmod(static_cast<float>(t_ms), period_ms) / period_ms);
}

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float unit(uint32_t h) { return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000); }

float smoothstep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// One blink in every 3.5 s, at a random moment within it.
float blink_at(uint32_t t) {
    constexpr uint32_t kSlot = 3500;
    constexpr uint32_t kBlink = 180;
    const uint32_t slot = t / kSlot;
    const uint32_t start = slot * kSlot + static_cast<uint32_t>(unit(hash32(slot)) * (kSlot - kBlink));
    if (t < start || t >= start + kBlink) return 0;
    const float p = static_cast<float>(t - start) / kBlink;
    return 1 - std::fabs(2 * p - 1);
}

// Where the eyes rest during one 2.6 s slot: straight ahead half the time, otherwise a
// random spot within range.
void glance_target(uint32_t slot, float range, float& x, float& y) {
    const uint32_t h = hash32(slot * 2654435761U + 17);
    if (h & 1) {
        x = y = 0;
        return;
    }
    x = (unit(hash32(h)) * 2 - 1) * range;
    y = (unit(hash32(h + 1)) * 2 - 1) * range * 0.6f;
}

// Glances around, moving to each new spot over 250 ms.
void glance(uint32_t t, float range, float& x, float& y) {
    constexpr uint32_t kSlot = 2600;
    constexpr uint32_t kMove = 250;
    const uint32_t slot = t / kSlot;
    float from_x, from_y, to_x, to_y;
    glance_target(slot == 0 ? 0 : slot - 1, range, from_x, from_y);
    glance_target(slot, range, to_x, to_y);
    const float k = smoothstep(static_cast<float>(t % kSlot) / kMove);
    x = from_x + (to_x - from_x) * k;
    y = from_y + (to_y - from_y) * k;
}

struct Rgb {
    float r, g, b;
};

Rgb rgb_of(uint32_t c) {
    return {static_cast<float>((c >> 16) & 0xFF), static_cast<float>((c >> 8) & 0xFF), static_cast<float>(c & 0xFF)};
}

Rgb mix(const Rgb& a, const Rgb& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

uint16_t pack(const Rgb& c) {
    const auto channel = [](float v) { return static_cast<uint32_t>(std::clamp(v + 0.5f, 0.0f, 255.0f)); };
    return panel_colour((channel(c.r) << 16) | (channel(c.g) << 8) | channel(c.b));
}

// One eye prepared for drawing a frame.
struct Eye {
    float x, y;            // Centre, in ball units.
    float axis_x, axis_y;  // Unit vector along the eye, towards its top.
    float half, round;     // In ball units.
    float squash;          // Vertical squash from blinking, 1 = none.
    float box_x, box_y;    // Centre on screen, for a quick bounding-box test.
    float box_reach;       // Half-size of that box, in pixels.
};

// Coverage (0..1) of the pixel at (ux, uy) in ball units by the eye. The distance to the
// eye's edge is converted to screen pixels so edges stay one pixel soft whatever the
// squash.
float eye_cover(const Eye& e, float ux, float uy, float rx, float ry) {
    const float vx = ux - e.x;
    const float vy = (uy - e.y) / e.squash;
    const float along = std::clamp(vx * e.axis_x + vy * e.axis_y, -e.half, e.half);
    const float wx = vx - along * e.axis_x;
    const float wy = vy - along * e.axis_y;
    const float len = std::sqrt(wx * wx + wy * wy);
    const float dist = len - e.round;
    if (len < 1e-6f) return 1;
    const float gx = wx / len / rx;
    const float gy = wy / len / (ry * e.squash);
    const float px = dist / std::sqrt(gx * gx + gy * gy);
    return std::clamp(0.5f - px, 0.0f, 1.0f);
}

}  // namespace

uint16_t panel_colour(uint32_t rgb) {
    const uint16_t c = static_cast<uint16_t>((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
    return static_cast<uint16_t>((c >> 8) | (c << 8));
}

Pose animate(Mood mood, uint32_t t, float loudness) {
    const float loud = std::clamp(loudness, 0.0f, 1.0f);
    Pose p;
    switch (mood) {
        case Mood::Idle: {
            const float b = wave(t, 3600);
            p.scale_y = 1 + 0.012f * b;
            p.scale_x = 1 - 0.008f * b;
            p.dy = -1.2f * b;
            p.blink = blink_at(t);
            glance(t, 0.05f, p.look_x, p.look_y);
            break;
        }
        case Mood::Hearing:
            p.eye_scale = 1.12f + 0.12f * loud;
            p.look_x = -0.05f;
            p.look_y = 0.03f;
            p.scale_y = 1 + 0.02f * loud;
            p.dy = -2 * loud;
            p.blink = blink_at(t);
            break;
        case Mood::Thinking:
            p.look_x = 0.05f * wave(t, 1800);
            p.look_y = -0.08f;
            p.dy = -1.5f * wave(t, 900);
            p.eye_scale = 0.95f;
            p.blink = blink_at(t);
            break;
        case Mood::Talking:
            p.scale_y = 1 + 0.08f * loud;
            p.scale_x = 1 - 0.05f * loud;
            p.dy = -6 * loud;
            p.eye_scale = 1 + 0.06f * loud;
            p.blink = blink_at(t);
            glance(t, 0.03f, p.look_x, p.look_y);
            break;
        case Mood::Asleep: {
            const float b = wave(t, 5200);
            p.scale_y = 1 + 0.02f * b;
            p.scale_x = 1 - 0.012f * b;
            p.dy = 2 - 1.5f * b;
            p.look_y = 0.04f;
            p.blink = 0.9f;
            break;
        }
        case Mood::Connecting:
            p.look_x = 0.07f * wave(t, 1400);
            p.blink = blink_at(t);
            break;
    }
    return p;
}

Pose ease(const Pose& a, const Pose& b, float k) {
    const auto lerp = [k](float x, float y) { return x + (y - x) * k; };
    Pose p;
    p.dx = lerp(a.dx, b.dx);
    p.dy = lerp(a.dy, b.dy);
    p.scale_x = lerp(a.scale_x, b.scale_x);
    p.scale_y = lerp(a.scale_y, b.scale_y);
    p.look_x = lerp(a.look_x, b.look_x);
    p.look_y = lerp(a.look_y, b.look_y);
    p.blink = lerp(a.blink, b.blink);
    p.eye_scale = lerp(a.eye_scale, b.eye_scale);
    return p;
}

void render(uint16_t* pixels, const Style& s, const Pose& pose) {
    const float cx = s.centre_x + pose.dx;
    const float cy = s.centre_y + pose.dy;
    const float rx = s.radius * pose.scale_x;
    const float ry = s.radius * pose.scale_y;
    const float soft = 1.5f / std::min(rx, ry);  // Beyond this, in ball units, no edge maths needed.
    const float inside2 = (1 - soft) * (1 - soft);
    const float outside2 = (1 + soft) * (1 + soft);

    Eye eyes[2];
    for (int i = 0; i < 2; ++i) {
        const EyeShape& shape = kEyes[i];
        const float tilt = shape.tilt_deg * kPi / 180;
        Eye& e = eyes[i];
        e.x = shape.x + pose.look_x;
        e.y = shape.y + pose.look_y;
        e.axis_x = -std::sin(tilt);
        e.axis_y = -std::cos(tilt);
        e.half = shape.half * pose.eye_scale;
        e.round = shape.round * pose.eye_scale;
        e.squash = std::max(0.08f, 1 - 0.9f * std::clamp(pose.blink, 0.0f, 1.0f));
        e.box_x = cx + e.x * rx;
        e.box_y = cy + e.y * ry;
        e.box_reach = (e.half + e.round) * std::max(rx, ry) + 2;
    }

    const Rgb background = rgb_of(s.background);
    const Rgb ball = rgb_of(s.ball);
    const Rgb eye_colour = rgb_of(s.eyes);
    const uint16_t background_px = pack(background);
    const uint16_t ball_px = pack(ball);

    for (int y = 0; y < s.region_h; ++y) {
        const float sy = static_cast<float>(s.region_y + y) + 0.5f;
        const float uy = (sy - cy) / ry;
        uint16_t* row = pixels + static_cast<size_t>(y) * s.region_w;
        for (int x = 0; x < s.region_w; ++x) {
            const float sx = static_cast<float>(s.region_x + x) + 0.5f;
            const float ux = (sx - cx) / rx;
            const float k2 = ux * ux + uy * uy;
            if (k2 >= outside2) {
                row[x] = background_px;
                continue;
            }
            float ball_cover = 1;
            if (k2 > inside2) {
                const float k = std::sqrt(k2);
                const float gx = ux / (k * rx);
                const float gy = uy / (k * ry);
                ball_cover = std::clamp(0.5f - (k - 1) / std::sqrt(gx * gx + gy * gy), 0.0f, 1.0f);
            }
            float eye_cover_max = 0;
            for (const Eye& e : eyes) {
                if (std::fabs(sx - e.box_x) > e.box_reach || std::fabs(sy - e.box_y) > e.box_reach) continue;
                eye_cover_max = std::max(eye_cover_max, eye_cover(e, ux, uy, rx, ry));
            }
            if (ball_cover >= 1 && eye_cover_max <= 0) {
                row[x] = ball_px;
                continue;
            }
            row[x] = pack(mix(mix(background, ball, ball_cover), eye_colour, eye_cover_max));
        }
    }
}

}  // namespace grokface
