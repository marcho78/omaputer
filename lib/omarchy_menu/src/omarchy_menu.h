// The Launcher's tile menu for the Cardputer (240x135): a row of coloured icon tiles that
// slides left and right, the selected tile large and centred with its name and a line of
// description under it, dots for the position, and a status bar (battery, WiFi, time).
// Plain C++ with no device code, so tools/menu_preview.cpp draws exactly what the device
// shows.
#pragma once

#include <cstdint>

namespace omenu {

constexpr int kWidth = 240;
constexpr int kHeight = 135;
constexpr int kStatusHeight = 19;  // Rows the status bar covers.
constexpr uint32_t kSlideMs = 380; // How long a slide to the next tile takes.

enum class Icon : uint8_t { Sd, Install, Web, Usb, Partitions, Settings, Grok, Screensaver, Power, App, Count };

struct Tile {
    Icon icon;
    const char *label;
    const char *description;
    uint32_t colour; // 0xRRGGBB.
    bool active;     // False draws the tile faded, like SD Card with no card in.
};

struct Status {
    const char *name; // At the top left.
    int battery;      // Percent; below 0 when unknown.
    bool charging;
    bool wifi;        // Connected.
    const char *time; // Like "4:21 PM"; empty while the clock isn't set.
};

// Pixels are RGB565 with their two bytes swapped, the order the Cardputer's panel takes.
class Renderer {
public:
    ~Renderer() { end(); }
    bool begin(); // Draws the icon shapes once, into about 16 KB. False when out of memory.
    void end();   // Frees them.
    bool ready() const { return masks_ != nullptr; }

    // The whole screen into kWidth * kHeight pixels. position is the selected tile's index,
    // fractional while sliding from one tile to the next.
    void draw(uint16_t *frame, const Tile *tiles, int count, float position, const Status &status) const;

    // Only the status bar, into kWidth * kStatusHeight pixels.
    void drawStatus(uint16_t *strip, const Status &status) const;

private:
    uint8_t *masks_ = nullptr; // One coverage map per icon.
};

// Easing for a slide: 0..1 to 0..1 with a slight overshoot, like a spring settling.
float spring(float t);

} // namespace omenu
