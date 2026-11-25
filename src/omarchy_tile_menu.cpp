#if defined(OMARCHY_TILE_MENU)

#include "omarchy_tile_menu.h"

#include "display.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "omarchy_menu.h"
#include "omarchy_ui.h"
#include <memory>
#include <new>
#if defined(OMARCHY_CLOCK)
#include "omarchy_clock.h"
#endif

namespace {

// The screen is drawn a band of rows at a time: a whole frame (65 KB) doesn't fit in memory
// next to WiFi, which after a Grok session can leave no free block over about 21 KB. Bands
// are as tall as the biggest buffer that can be had (40 rows is 19.2 KB), so a sliding tile
// is cut into as few pieces as possible.
constexpr int kBandRowChoices[] = {40, 20, 10};

omenu::Renderer renderer;
int lastIndex = -1;
int lastCount = -1;
bool shown = false;
// What the status bar shows now, so the 3-second poll redraws it only on a change.
int shownBattery = -2;
bool shownCharging = false;
bool shownWifi = false;
String shownTime;

omenu::Tile tileFor(const MenuOptions &o) {
    using omenu::Icon;
    struct Known {
        const char *name;
        Icon icon;
        const char *label;
        const char *description;
        uint32_t colour;
    };
    static const Known kKnown[] = {
        {"SD",          Icon::Sd,          "SD Card",     "Install apps from the card",  0xF58C32},
        {"OTA",         Icon::Install,     "Install",     "Download apps online",        0x3C82F5},
        {"WUI",         Icon::Web,         "Web UI",      "Manage files from a browser", 0x1EB4AA},
        {"USB",         Icon::Usb,         "USB Drive",   "Open the card on a computer", 0x965AE6},
        {"PMan",        Icon::Partitions,  "Partitions",  "Space used by apps",          0xE6BE28},
        {"PM",          Icon::Partitions,  "Partitions",  "Space used by apps",          0xE6BE28},
        {"CFG",         Icon::Settings,    "Settings",    "Launcher options",            0x78808C},
        {"GROKVOICE",   Icon::Grok,        "Grok",        "Talk to Grok out loud",       0xFAFAFA},
        {"SCREENSAVER", Icon::Screensaver, "Screensaver", "Omarchy logo effects",        0x00C878},
        {"OFF",         Icon::Power,       "Power Off",   "Turn the Cardputer off",      0xEB4646},
    };
    for (const Known &k : kKnown) {
        if (o.name == k.name) return {k.icon, k.label, k.description, k.colour, o.active};
    }
    return {Icon::App, o.text.c_str(), "Installed app", 0x5856D6, o.active}; // Apps keep their name.
}

omenu::Status currentStatus(String &time) {
#if defined(OMARCHY_CLOCK)
    time = omarchyClockTime();
#else
    time = "";
#endif
    const int battery = getBattery();
    return {"omarchy", battery > 0 ? battery : -1, isCharging(), launcherWifiIsConnected(), time.c_str()};
}

// The screen in bands of at most maxRows, cut where the layout changes (status bar, tiles,
// text) and then into equal parts.
void drawScreen(
    uint16_t *band, int maxRows, const std::vector<omenu::Tile> &tiles, float position, const omenu::Status &status
) {
    const int edges[] = {0, omenu::kStatusHeight, omenu::kTilesBottom, omenu::kHeight};
    for (int e = 0; e + 1 < 4; ++e) {
        const int span = edges[e + 1] - edges[e];
        const int parts = (span + maxRows - 1) / maxRows;
        for (int p = 0; p < parts; ++p) {
            const int top = edges[e] + span * p / parts;
            const int rows = edges[e] + span * (p + 1) / parts - top;
            renderer.draw(band, top, rows, tiles.data(), static_cast<int>(tiles.size()), position, status);
            omarchy::pushFrame(band, 0, top, omenu::kWidth, rows);
        }
    }
}

void remember(const omenu::Status &s, const String &time) {
    shownBattery = s.battery;
    shownCharging = s.charging;
    shownWifi = s.wifi;
    shownTime = time;
}

} // namespace

bool omarchyTileMenuDraw(std::vector<MenuOptions> &opt, int index, bool fullRedraw) {
    const int count = static_cast<int>(opt.size());
    shown = false;
    if (count < 1 || tftWidth != omenu::kWidth || tftHeight != omenu::kHeight) return false;
    if (!renderer.begin()) {
        launcherConsolePrintln("Tile menu: not enough memory for its icons, showing the list menu");
        return false;
    }
    std::unique_ptr<uint16_t[]> band;
    int bandRows = 0;
    for (int rows : kBandRowChoices) {
        band.reset(new (std::nothrow) uint16_t[omenu::kWidth * rows]);
        if (band) {
            bandRows = rows;
            break;
        }
    }
    if (!band) {
        launcherConsolePrintln("Tile menu: not enough memory to draw, showing the list menu");
        return false;
    }

    std::vector<omenu::Tile> tiles;
    tiles.reserve(count);
    for (MenuOptions &o : opt) {
        tiles.push_back(tileFor(o));
        o.resetCoords();
    }
    String time;
    const omenu::Status status = currentStatus(time);

    if (!fullRedraw && lastIndex >= 0 && lastIndex != index && lastCount == count) {
        const float from = static_cast<float>(lastIndex), to = static_cast<float>(index);
        const uint32_t start = launcherMillis();
        for (;;) {
            const float t = static_cast<float>(launcherMillis() - start) / omenu::kSlideMs;
            if (t >= 1 || AnyKeyPress) break; // Another key: finish at once so it moves on.
            drawScreen(band.get(), bandRows, tiles, from + (to - from) * omenu::spring(t), status);
            launcherDelayMs(1);
        }
    }
    drawScreen(band.get(), bandRows, tiles, static_cast<float>(index), status);
    remember(status, time);
    lastIndex = index;
    lastCount = count;
    shown = true;
    return true;
}

bool omarchyTileMenuShown() { return shown; }

bool omarchyTileMenuStatus() {
    if (!shown) return false;
    String time;
    const omenu::Status status = currentStatus(time);
    if (status.battery == shownBattery && status.charging == shownCharging && status.wifi == shownWifi &&
        time == shownTime) {
        return false;
    }
    std::unique_ptr<uint16_t[]> strip(new (std::nothrow) uint16_t[omenu::kWidth * omenu::kStatusHeight]);
    if (!strip) return false;
    renderer.drawStatus(strip.get(), status);
    omarchy::pushFrame(strip.get(), 0, 0, omenu::kWidth, omenu::kStatusHeight);
    remember(status, time);
    return true;
}

void omarchyTileMenuRelease() {
    renderer.end();
    lastIndex = -1;
    shown = false;
}

#endif
