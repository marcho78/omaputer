// The Omarchy logo as a grid of characters, and its parser.
#pragma once

#include <cstdint>
#include <vector>

namespace ofx {

// One non-space character of a logo. Row 0 is the top line, column 0 the left edge.
struct LogoChar {
    uint16_t row;
    uint16_t col;
    uint32_t symbol;  // Unicode code point, e.g. 0x2588 for a full block.
};

struct Logo {
    int width = 0;   // Longest line, in characters.
    int height = 0;  // Number of lines.
    std::vector<LogoChar> chars;
};

// Parses UTF-8 text into a Logo. Lines may differ in length; spaces are left out of
// `chars` but still count towards positions. A trailing newline is ignored.
Logo parse_logo(const char* utf8);

// Omarchy's stock screensaver logo (/usr/share/omarchy/logo.txt, MIT License).
extern const char kOmarchyLogo[];

}  // namespace ofx
