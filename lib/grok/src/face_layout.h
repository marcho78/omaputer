// Where the face sits on the Cardputer's 240x135 screen, shared by the device and the
// preview. The ball rests just above centre to leave a status line at the bottom, and
// the drawn region covers its widest stretch and highest bounce.
#pragma once

#include "grok_face.h"

namespace grokface {

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 135;
constexpr int kStatusTop = 120;  // The status line uses the rows below this.

inline Style screen_style() {
    Style s;
    s.region_x = 64;
    s.region_y = 2;
    s.region_w = 112;
    s.region_h = 116;
    s.centre_x = 120;
    s.centre_y = 62;
    s.radius = 48;
    return s;
}

}  // namespace grokface
