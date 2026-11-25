// Renders the tile menu on the laptop with the same code as the device (lib/omarchy_menu),
// to preview/menu_<item>.png and a sliding animation, preview/menu_slide.gif.
// Needs ImageMagick (`magick`). From the launcher folder:
//   g++ -std=c++17 -O2 -Ilib/omarchy_menu/src -Ilib/grok/src tools/menu_preview.cpp
//       lib/omarchy_menu/src/omarchy_menu.cpp lib/grok/src/grok_face.cpp -o /tmp/menu_preview
//   /tmp/menu_preview
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "omarchy_menu.h"

using namespace omenu;

namespace {

// The Cardputer's main menu, as src/omarchy_ui.cpp maps it.
const Tile kTiles[] = {
    {Icon::Sd, "SD Card", "Install apps from the card", 0xF58C32, true},
    {Icon::Install, "Install", "Download apps online", 0x3C82F5, true},
    {Icon::Web, "Web UI", "Manage files from a browser", 0x1EB4AA, true},
    {Icon::Usb, "USB Drive", "Open the card on a computer", 0x965AE6, true},
    {Icon::Partitions, "Partitions", "Space used by apps", 0xE6BE28, true},
    {Icon::Settings, "Settings", "Launcher options", 0x78808C, true},
    {Icon::Grok, "Grok", "Talk to Grok out loud", 0xFAFAFA, true},
    {Icon::Screensaver, "Screensaver", "Omarchy logo effects", 0x00C878, true},
    {Icon::Power, "Power Off", "Turn the Cardputer off", 0xEB4646, true},
};
constexpr int kCount = sizeof(kTiles) / sizeof(kTiles[0]);
const Status kStatus = {"omarchy", 76, false, true, "4:21 PM"};

bool writePpm(const std::string &path, const std::vector<uint16_t> &frame) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", kWidth, kHeight);
    for (uint16_t s : frame) {
        const uint16_t v = static_cast<uint16_t>(s >> 8 | s << 8);
        const unsigned char rgb[3] = {static_cast<unsigned char>(((v >> 11) & 31) * 255 / 31),
                                      static_cast<unsigned char>(((v >> 5) & 63) * 255 / 63),
                                      static_cast<unsigned char>((v & 31) * 255 / 31)};
        std::fwrite(rgb, 1, 3, f);
    }
    return std::fclose(f) == 0;
}

int run(const std::string &cmd) { return std::system(cmd.c_str()); }

} // namespace

int main() {
    namespace fs = std::filesystem;
    fs::create_directories("preview/frames");
    Renderer r;
    if (!r.begin()) return 1;
    std::vector<uint16_t> frame(kWidth * kHeight);
    int failures = 0;

    for (int i = 0; i < kCount; ++i) {
        r.draw(frame.data(), kTiles, kCount, static_cast<float>(i), kStatus);
        const std::string name = "preview/menu_" + std::to_string(i);
        if (!writePpm(name + ".ppm", frame) || run("magick " + name + ".ppm -scale 300% " + name + ".png") != 0) ++failures;
        fs::remove(name + ".ppm");
    }

    // Slide right from Settings to Power Off and back, at 30 frames a second.
    const int path[] = {5, 6, 7, 8, 7, 6};
    int n = 0;
    auto shot = [&](float pos, int hold) {
        r.draw(frame.data(), kTiles, kCount, pos, kStatus);
        char file[64];
        for (int k = 0; k < hold; ++k) {
            std::snprintf(file, sizeof file, "preview/frames/%04d.ppm", n++);
            if (!writePpm(file, frame)) ++failures;
        }
    };
    const int steps = static_cast<int>(kSlideMs / 33);
    for (size_t p = 0; p + 1 < sizeof(path) / sizeof(path[0]); ++p) {
        shot(static_cast<float>(path[p]), 20);
        for (int k = 1; k <= steps; ++k) shot(path[p] + (path[p + 1] - path[p]) * spring(static_cast<float>(k) / steps), 1);
    }
    if (run("magick -delay 3 -loop 0 'preview/frames/*.ppm' -scale 300% -layers Optimize preview/menu_slide.gif") != 0) ++failures;
    fs::remove_all("preview/frames");
    std::printf("%d stills and preview/menu_slide.gif\n", kCount);
    return failures ? 1 : 0;
}
