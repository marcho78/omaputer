// Omarchy's screensaver for the Launcher. Omarchy starts its screensaver after 150 s without
// input (idle.screensaver in its shell.json), or on demand with `omarchy launch screensaver`.
// It plays its logo with a random terminal text effect, a new one each time, until a key is
// pressed. This does the same with the effects in lib/omarchy_fx.
#if defined(OMARCHY_SCREENSAVER)

#include "omarchy_screensaver.h"

#include "display.h"
#include "effect.h"
#include "idf/launcher_platform.h"
#include "logo.h"
#include "mykeyboard.h"
#include "raster.h"
#include <algorithm>
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
// Playing took at most 122.7 KB of heap on the Cardputer (the 64.8 KB frame plus the hungriest
// effect, measured over all six), so start only with that plus room for the rest of the
// Launcher, which needs more while WiFi is on. Starting short could abort in an effect.
constexpr unsigned kMinFreeHeap = 150000;

// A dimmed or dark screen means the Dim setting got there first, so dimming wins.
bool screenAwake() { return !dimmer && !isScreenOff && !isSleeping; }

void clearInput() {
    launcherInputLock();
    resetGlobals();
    launcherInputUnlock();
}

// Swallows input until no key has been reported for kQuietMillis, so a key that is still
// held (the one that started or stopped the screensaver) does not act on anything else.
void waitForQuiet() {
    uint32_t quietSince = launcherMillis();
    while (launcherMillis() - quietSince < kQuietMillis) {
        if (AnyKeyPress) {
            clearInput();
            quietSince = launcherMillis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    clearInput();
}

unsigned freeHeap() {
#if defined(USE_NATIVE_SDL)
    return 0;
#else
    return static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
#endif
}

unsigned largestBlock() {
#if defined(USE_NATIVE_SDL)
    return 0;
#else
    return static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
}

bool enoughMemory() {
#if defined(USE_NATIVE_SDL)
    return true;
#else
    return freeHeap() >= kMinFreeHeap;
#endif
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

// Plays until a key is pressed, then clears the screen. Returns false, having drawn nothing,
// if there is not enough memory to play.
bool play() {
    launcherConsolePrintf("Screensaver: start, free heap %u, largest block %u\n", freeHeap(), largestBlock());
    std::unique_ptr<ofx::Framebuffer> frame;
    if (enoughMemory()) frame.reset(new (std::nothrow) ofx::Framebuffer());
    if (!frame) {
        launcherConsolePrintln("Screensaver: not enough memory, skipped");
        previousMillis = launcherMillis(); // The idle start tries again after the next idle period.
        return false;
    }
    waitForQuiet(); // The key that started it, or one left over that no menu used.

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
    unsigned lowest = freeHeap();
    const unsigned long since = previousMillis; // Every key press moves it (wakeUpScreen).

    while (previousMillis == since && !AnyKeyPress && screenAwake()) {
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
            lowest = std::min(lowest, freeHeap());
        }
        vTaskDelay(1); // Lets the input task run.
    }
    waitForQuiet(); // The key that stopped it, and its repeats while it is held.

    effect.reset();
    frame.reset();
    tft->fillScreen(BGCOLOR);
    previousMillis = launcherMillis();
    launcherConsolePrintf(
        "Screensaver: stop, free heap %u, largest block %u, lowest while playing %u\n",
        freeHeap(),
        largestBlock(),
        lowest
    );
    return true;
}

} // namespace

bool omarchyScreensaver() {
    return screenAwake() && launcherMillis() - previousMillis >= OMARCHY_SCREENSAVER_IDLE_MS && play();
}

void omarchyScreensaverNow() {
    if (!play()) displayError("Not enough memory");
}

#endif
