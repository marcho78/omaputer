#include "logo.h"

namespace ofx {
namespace {

// Decodes the UTF-8 code point at p and advances p past it.
// Malformed or truncated sequences decode as U+FFFD.
uint32_t next_code_point(const unsigned char*& p) {
    uint32_t c = *p++;
    if (c < 0x80) return c;

    int extra;
    if ((c & 0xE0) == 0xC0) {
        c &= 0x1F;
        extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
        c &= 0x0F;
        extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
        c &= 0x07;
        extra = 3;
    } else {
        return 0xFFFD;
    }

    for (int i = 0; i < extra; ++i) {
        if ((*p & 0xC0) != 0x80) return 0xFFFD;  // Leaves the bad byte for the next call.
        c = (c << 6) | (*p++ & 0x3F);
    }
    return c;
}

}  // namespace

Logo parse_logo(const char* utf8) {
    Logo logo;
    const auto* p = reinterpret_cast<const unsigned char*>(utf8);
    int row = 0;
    int col = 0;

    while (*p) {
        const uint32_t cp = next_code_point(p);
        if (cp == '\r') continue;
        if (cp == '\n') {
            ++row;
            col = 0;
            continue;
        }
        if (cp != ' ') {
            logo.chars.push_back({static_cast<uint16_t>(row), static_cast<uint16_t>(col), cp});
        }
        ++col;
        if (col > logo.width) logo.width = col;
    }

    // A final line without a newline still counts; a trailing newline adds nothing.
    logo.height = col > 0 ? row + 1 : row;
    return logo;
}

}  // namespace ofx
