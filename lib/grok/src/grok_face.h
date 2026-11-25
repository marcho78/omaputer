// The Grok bot face: a black ball with two slanted white eyes, drawn with smooth edges.
// Plain C++ with no device code, so the same drawing runs on the Cardputer and in the
// laptop preview (src/preview).
#pragma once

#include <cstdint>

namespace grokface {

// What Grok is doing, which sets how the face moves.
enum class Mood : uint8_t {
    Idle,        // Connected and waiting for you: breathes, blinks, glances around.
    Hearing,     // You are talking: eyes open wider, pulsing with your voice.
    Thinking,    // Waiting for the reply: eyes look up and drift side to side.
    Talking,     // Grok is speaking: stretches and rises with the loudness of its voice.
    Asleep,      // Hung up after a quiet spell: eyes closed, slow breathing.
    Connecting,  // Joining WiFi or Grok: eyes look left and right.
};

// Where the face goes on screen and its colours (0xRRGGBB).
struct Style {
    int region_x = 0, region_y = 0;  // Top-left of the area render() fills.
    int region_w = 0, region_h = 0;
    float centre_x = 0, centre_y = 0;  // Ball centre, in screen pixels.
    float radius = 0;                  // Ball radius at rest, in pixels.
    uint32_t background = 0xFFFFFF;
    uint32_t ball = 0x000000;
    uint32_t eyes = 0xFFFFFF;
};

// The face's shape in one frame.
struct Pose {
    float dx = 0, dy = 0;            // Ball offset from its centre, in pixels.
    float scale_x = 1, scale_y = 1;  // Squash and stretch.
    float look_x = 0, look_y = 0;    // Eye offset, in ball radii.
    float blink = 0;                 // 0 = open, 1 = closed.
    float eye_scale = 1;             // Eye size.
};

// The pose for a mood at a moment. loudness (0..1) drives Hearing and Talking. A pure
// function of its arguments, so a preview shows exactly what the device will.
Pose animate(Mood mood, uint32_t now_ms, float loudness);

// Moves part of the way from one pose to another, for smooth changes between moods.
Pose ease(const Pose& from, const Pose& to, float amount);

// Draws the face into pixels: region_w * region_h RGB565 values with their two bytes
// swapped, the order the Cardputer's panel takes.
void render(uint16_t* pixels, const Style& style, const Pose& pose);

// 0xRRGGBB to the panel's byte-swapped RGB565.
uint16_t panel_colour(uint32_t rgb);

}  // namespace grokface
