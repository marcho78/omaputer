// Omarchy's screensaver for the Launcher. Omarchy starts its screensaver after 150 s without
// input (idle.screensaver in its shell.json). It plays its logo with a random terminal text
// effect, a new one each time, until a key is pressed. This does the same with the effects in
// lib/omarchy_fx.
#if defined(OMARCHY_SCREENSAVER)

#include "omarchy_screensaver.h"

#include "display.h"
#include "effect.h"
#include "idf/launcher_platform.h"
#include "logo.h"
#include "mykeyboard.h"
#include "raster.h"
#include <esp_random.h>
#include <esp_timer.h>
#include <globals.h>
#include <memory>
#include <new>
#if !defined(USE_NATIVE_SDL)
#include <esp_heap_caps.h>
#endif

#ifndef OMARCHY_SCREENSAVER_IDLE_MS
#define OMARCHY_SCREENSAVER_IDLE_MS 150000 // Omarchy's default idle.screensaver, 150 s.
#endif

namespace {

constexpr int64_t kFrameMicros = 1000000 / 120; // Omarchy runs ttfx at --frame-rate 120.
constexpr int kMaxStepsPerDraw = 4;             // Catch-up limit after a slow frame.
constexpr uint32_t kHoldMillis = 1000;          // A finished logo stays up this long.
constexpr uint32_t kQuietMillis = 300;          // Longer than the keyboard's 200 ms key repeat.

// A dimmed or dark screen means the Dim setting got there first, so dimming wins.
bool idleLongEnough() {
    return !dimmer && !isScreenOff && !isSleeping &&
           launcherMillis() - previousMillis >= OMARCHY_SCREENSAVER_IDLE_MS;
}

void clearInput() {
    launcherInputLock();
    resetGlobals();
    launcherInputUnlock();
}

// The frame holds RGB565 with its two bytes swapped, the order the panel takes, so it is
// sent as it is.
void present(const ofx::Framebuffer &frame) {
    uint16_t *pixels = const_cast<uint16_t *>(frame.data());
#if defined(USE_NATIVE_SDL)
    tft->native()->pushImage(
        0, 0, ofx::kScreenWidth, ofx::kScreenHeight, reinterpret_cast<const lgfx::swap565_t *>(pixels)
    );
#elif defined(USE_ARDUINO_GFX)
    tft->native()->draw16bitBeRGBBitmap(0, 0, pixels, ofx::kScreenWidth, ofx::kScreenHeight);
#else
#error "omarchy_screensaver: no frame push for this display backend"
#endif
}

void logHeap(const char *event) {
#if !defined(USE_NATIVE_SDL)
    launcherConsolePrintf(
        "Screensaver: %s, free heap %u, largest block %u, lowest since boot %u\n",
        event,
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT))
    );
#else
    launcherConsolePrintf("Screensaver: %s\n", event);
#endif
}

} // namespace

bool omarchyScreensaver() {
    if (!idleLongEnough()) return false;

    logHeap("start");
    std::unique_ptr<ofx::Framebuffer> frame(new (std::nothrow) ofx::Framebuffer());
    if (!frame) {
        launcherConsolePrintln("Screensaver: not enough memory, skipped");
        previousMillis = launcherMillis(); // Tries again after the next idle period.
        return false;
    }
    clearInput(); // A key no menu used, left over from earlier, must not stop it at once.

    const ofx::Logo logo = ofx::parse_logo(ofx::kOmarchyLogo);
    const ofx::CellGrid grid(logo.width, ofx::CellGrid::kMaxRows);
    std::unique_ptr<ofx::Effect> effect;
    int lastIndex = -1;
    bool finished = true;
    uint32_t finishedAt = launcherMillis() - kHoldMillis; // The first effect starts at once.
    int64_t nextStep = 0;
    uint32_t startedAt = 0;
    uint32_t frames = 0; // Animation frames stepped in the current effect.
    uint32_t draws = 0;  // Frames actually sent to the display.

    while (idleLongEnough() && !AnyKeyPress) {
        if (finished && launcherMillis() - finishedAt >= kHoldMillis) {
            const int count = ofx::effect_count();
            int index = static_cast<int>(launcherRandom(count));
            if (count > 1 && index == lastIndex) index = (index + 1) % count; // Never twice in a row.
            lastIndex = index;
            effect = ofx::make_effect(index);
            effect->begin(logo, grid.cols(), grid.rows(), esp_random());
            finished = false;
            nextStep = esp_timer_get_time();
            startedAt = launcherMillis();
            frames = draws = 0;
        }

        // Step at a fixed 120 frames per second, however long drawing takes.
        const int64_t now = esp_timer_get_time();
        int steps = 0;
        while (!finished && now >= nextStep && steps < kMaxStepsPerDraw) {
            nextStep += kFrameMicros;
            ++steps;
            if (effect->step()) {
                ++frames;
            } else {
                finished = true;
                finishedAt = launcherMillis();
                launcherConsolePrintf(
                    "Screensaver: %s, %u frames in %u ms, %u draws\n",
                    effect->name(),
                    static_cast<unsigned>(frames),
                    static_cast<unsigned>(finishedAt - startedAt),
                    static_cast<unsigned>(draws)
                );
            }
        }
        if (now >= nextStep) nextStep = now; // Too far behind: drop the backlog.

        if (steps > 0) {
            effect->draw(*frame, grid);
            present(*frame);
            ++draws;
        }
        vTaskDelay(1); // Lets the input task run.
    }

    // Swallow the key that stopped it, and its repeats while it is held, so the menu
    // underneath does not act on it too.
    uint32_t quietSince = launcherMillis();
    while (launcherMillis() - quietSince < kQuietMillis) {
        if (AnyKeyPress) {
            clearInput();
            quietSince = launcherMillis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    clearInput();

    effect.reset();
    frame.reset();
    tft->fillScreen(BGCOLOR);
    previousMillis = launcherMillis();
    logHeap("stop");
    return true;
}

#endif
