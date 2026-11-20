#include "omarchy_ui.h"
#include "display.h"
#include "omarchy_logo.h"

namespace omarchy {
namespace {

int red(uint16_t c) { return ((c >> 11) & 0x1F) * 255 / 31; }
int green(uint16_t c) { return ((c >> 5) & 0x3F) * 255 / 63; }
int blue(uint16_t c) { return (c & 0x1F) * 255 / 31; }
uint16_t rgb(int r, int g, int b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }

// The border gradient at position t of span: the accent at 0, kAccentEnd at span.
uint16_t gradient(int t, int span, uint8_t alpha) {
    const uint16_t c = blend(kAccentEnd, FGCOLOR, span > 0 ? t * 255 / span : 0);
    return alpha == 255 ? c : blend(c, BGCOLOR, alpha);
}

// 8x8 icons, one byte per row, the MSB is the left pixel.
const uint8_t kIconSd[8] = {0x3F, 0x41, 0xA9, 0xA9, 0x81, 0x81, 0x81, 0xFF};
const uint8_t kIconInstall[8] = {0x18, 0x18, 0x5A, 0x3C, 0x18, 0x81, 0x81, 0xFF};
const uint8_t kIconWeb[8] = {0x3C, 0x5A, 0x99, 0xFF, 0x99, 0x5A, 0x3C, 0x00};
const uint8_t kIconUsb[8] = {0x3C, 0x24, 0x7E, 0x42, 0x42, 0x24, 0x18, 0x18};
const uint8_t kIconPartitions[8] = {0x00, 0xFF, 0xA5, 0xA5, 0xA5, 0xFF, 0x00, 0x00};
const uint8_t kIconSettings[8] = {0x18, 0x7E, 0x66, 0xC3, 0xC3, 0x66, 0x7E, 0x18};
const uint8_t kIconPower[8] = {0x18, 0x5A, 0x99, 0x99, 0x81, 0x81, 0x42, 0x3C};
const uint8_t kIconApp[8] = {0xFF, 0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF};
const uint8_t kIconWifi[8] = {0x00, 0x7E, 0x81, 0x3C, 0x42, 0x18, 0x18, 0x00};

void bitmap8(const uint8_t *rows, int x, int y, uint16_t color, int scale) {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (rows[r] & (0x80 >> c)) tft->fillRect(x + c * scale, y + r * scale, scale, scale, color);
        }
    }
}

} // namespace

uint16_t blend(uint16_t fg, uint16_t bg, uint8_t alpha) {
    const int a = alpha, b = 255 - alpha;
    return rgb(
        (red(fg) * a + red(bg) * b) / 255,
        (green(fg) * a + green(bg) * b) / 255,
        (blue(fg) * a + blue(bg) * b) / 255
    );
}

uint16_t selectionFill() { return blend(ALCOLOR, BGCOLOR, 20); } // 8%, Omarchy's selected-background-alpha
uint16_t mutedText() { return blend(ALCOLOR, BGCOLOR, 150); }
uint16_t inactiveText() { return blend(ALCOLOR, BGCOLOR, 85); }

void frame(int x, int y, int w, int h, int thickness, uint8_t alpha) {
    // In 4 px steps: smooth enough on a small panel, with far fewer draw calls.
    const int span = w + h;
    for (int i = 0; i < w; i += 4) {
        const int n = i + 4 <= w ? 4 : w - i;
        tft->fillRect(x + i, y, n, thickness, gradient(i, span, alpha));
        tft->fillRect(x + i, y + h - thickness, n, thickness, gradient(i + h, span, alpha));
    }
    for (int j = thickness; j < h - thickness; j += 4) {
        const int n = j + 4 <= h - thickness ? 4 : h - thickness - j;
        tft->fillRect(x, y + j, thickness, n, gradient(j, span, alpha));
        tft->fillRect(x + w - thickness, y + j, thickness, n, gradient(w + j, span, alpha));
    }
}

void selection(int x, int y, int w, int h) {
    tft->fillRect(x, y, w, h, selectionFill());
    frame(x, y, w, h, 1, 64); // 25%, Omarchy's selected-border-alpha
}

void bar() {
    tft->fillRect(0, 0, tftWidth, kBarHeight, BGCOLOR);
    for (int r = 0; r < kSquareLogoSize; ++r) {
        int run = -1; // Start column of the current run of set pixels, or -1.
        for (int c = 0; c <= kSquareLogoSize; ++c) {
            const bool on = c < kSquareLogoSize && (kSquareLogo[r] & (1 << (kSquareLogoSize - 1 - c)));
            if (on && run < 0) run = c;
            if (!on && run >= 0) {
                tft->fillRect(3 + run, r, c - run, 1, FGCOLOR);
                run = -1;
            }
        }
    }
}

void menuIcon(const String &name, int x, int y, uint16_t color, int scale) {
    const uint8_t *icon = kIconApp;
    if (name == "SD") icon = kIconSd;
    else if (name == "OTA") icon = kIconInstall;
    else if (name == "WUI") icon = kIconWeb;
    else if (name == "USB") icon = kIconUsb;
    else if (name == "PMan" || name == "PM") icon = kIconPartitions;
    else if (name == "CFG") icon = kIconSettings;
    else if (name == "OFF") icon = kIconPower;
    bitmap8(icon, x, y, color, scale);
}

String menuLabel(const String &name) {
    if (name == "SD") return "SD Card";
    if (name == "OTA") return "Install";
    if (name == "WUI") return "Web UI";
    if (name == "USB") return "USB Drive";
    if (name == "PMan" || name == "PM") return "Partitions";
    if (name == "CFG") return "Settings";
    if (name == "OFF") return "Power Off";
    return "";
}

void wifiIcon(int x, int y, uint16_t color) { bitmap8(kIconWifi, x, y, color, 1); }

void wordmark(int cx, int y, int scale) {
    const int x0 = cx - kWordmarkWidth * scale / 2;
    const int span = kWordmarkWidth + kWordmarkHeight;
    for (int r = 0; r < kWordmarkHeight; ++r) {
        for (int c = 0; c < kWordmarkWidth; ++c) {
            if (kWordmark[r][c / 8] & (0x80 >> (c % 8))) {
                tft->fillRect(x0 + c * scale, y + r * scale, scale, scale, gradient(c + r, span, 255));
            }
        }
    }
}

} // namespace omarchy
