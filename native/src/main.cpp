// SPDX-FileCopyrightText: 2026 bmorcelli
//
// SPDX-License-Identifier: MIT
//
// Launcher UI on the desktop.
//
// Compiles the REAL src/main.cpp and src/display.cpp — real setup(), real
// loop(), real menu items, real actions (loopSD, ota_function,
// loopOptionsWebUi, settings_menu, ...) — against DisplayDrivers'
// USE_NATIVE_SDL backend, so the actual UI/navigation code can be driven in
// an SDL2 window instead of on a board. Everything ESP32/Arduino-only that
// those translation units need just to compile (SD, WiFi, NVS, FreeRTOS,
// ArduinoJson-adjacent types) is faked out by the headers in native/sources/,
// which sit ahead of the real src/include on the include path (see
// native/platformio.ini) so `#include "settings.h"` resolves to the real
// Launcher header while `#include <SD.h>` resolves to the fake.
//
// The one piece a board would normally supply (boards/<board>/interface.cpp)
// that this harness provides itself is InputHandler() — see below — which
// polls the SDL window's keyboard state instead of real GPIO/I2C.
//
// This does NOT run the real Launcher app end to end: WiFi, SD, BLE, NVS,
// and OTA are all stubbed (see src/stubs.cpp), not implemented. Screens that
// read real data from those (file lists, firmware search, ...) show nothing
// or fall through immediately. The menu, navigation, and every screen's
// *drawing* code is real.

#include <DisplayDrivers.h>
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include <Arduino.h>
#include <FFat.h>
#include <SD.h>
#include <SPIFFS.h>
#include <globals.h>

#include <cstdlib>
#include <vector>

// ---------------------------------------------------------------------------
// Fake filesystem/serial globals the shim headers declared extern. Real
// main.cpp defines everything else (FGCOLOR, tftWidth, KeyStroke, ...).
// ---------------------------------------------------------------------------
HardwareSerial Serial;
SDClass SD;
SPIClass SPI;
FFatFS FFat;
SPIFFSFS SPIFFS;

// The real thing, compiled as-is against the fakes above.
#include "../../src/display.cpp"
#include "../../src/main.cpp"
#include "../../src/omarchy_screensaver.cpp"
#include "../../src/omarchy_tile_menu.cpp"
#include "../../src/omarchy_ui.cpp"

// ---------------------------------------------------------------------------
// InputHandler() — what a board's interface.cpp normally provides. Arrow
// keys are a 5-button pad (mapped in by Panel_sdl itself, pins 36-39, active
// low); Enter is Sel, Esc is Esc. -DHAS_5_BUTTONS=1 (native/platformio.ini)
// is what makes real main.cpp's loop() compile in the Up/Down grid-navigation
// branch that consumes UpPress/DownPress.
// ---------------------------------------------------------------------------
enum {
    GPIO_UP = 36,
    GPIO_RIGHT = 37,
    GPIO_DOWN = 38,
    GPIO_LEFT = 39,
    GPIO_SEL = 1,
    GPIO_ESC = 2,
};

static void mapKeys() {
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_RETURN, GPIO_SEL);
    lgfx::Panel_sdl::addKeyCodeMapping(SDLK_ESCAPE, GPIO_ESC);
}

static bool pressedEdge(uint8_t gpio, bool &wasDown) {
    bool down = !lgfx::gpio_in(gpio);
    bool edge = down && !wasDown;
    wasDown = down;
    return edge;
}

// ---------------------------------------------------------------------------
// Headless screenshots, for checking UI changes without a window or a board.
// LAUNCHER_SCRIPT is a space-separated list of steps, run from InputHandler():
//   <ms>         wait that long          up down left right sel esc  press a key
//   shot:<name>  save the screen as <name>.ppm      quit  exit the program
// Run with SDL_VIDEODRIVER=offscreen so no window opens, for example:
//   LAUNCHER_SCRIPT="1500 shot:boot sel 800 shot:menu quit" .pio/build/native/program
// ---------------------------------------------------------------------------
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static void saveScreen(const std::string &name) {
    const int w = tft->width(), h = tft->height();
    std::vector<lgfx::rgb888_t> px(static_cast<size_t>(w * h));
    tft->native()->readRect(0, 0, w, h, px.data());
    std::ofstream f(name + ".ppm", std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (const auto &p : px) {
        f.put(static_cast<char>(p.r));
        f.put(static_cast<char>(p.g));
        f.put(static_cast<char>(p.b));
    }
}

// Returns the key the script presses in this poll, or an empty string.
static std::string scriptedKey() {
    using clock = std::chrono::steady_clock;
    static std::vector<std::string> steps;
    static size_t next = 0;
    static bool loaded = false;
    static clock::time_point resumeAt = clock::now();
    if (!loaded) {
        loaded = true;
        if (const char *script = std::getenv("LAUNCHER_SCRIPT")) {
            std::istringstream in(script);
            for (std::string step; in >> step;) steps.push_back(step);
        }
    }
    while (next < steps.size() && clock::now() >= resumeAt) {
        const std::string step = steps[next++];
        if (std::isdigit(static_cast<unsigned char>(step[0]))) {
            resumeAt = clock::now() + std::chrono::milliseconds(std::stoi(step));
        } else if (step.rfind("shot:", 0) == 0) {
            saveScreen(step.substr(5));
        } else if (step == "quit") {
            std::_Exit(0);
        } else {
            return step; // One key per poll, so each press is a separate edge.
        }
    }
    return "";
}

void InputHandler(void) {
    static bool upDown = false, downDown = false, leftDown = false, rightDown = false, selDown = false,
                escDown = false;
    const std::string key = scriptedKey();
    PrevPress = pressedEdge(GPIO_LEFT, leftDown) || key == "left";
    NextPress = pressedEdge(GPIO_RIGHT, rightDown) || key == "right";
    UpPress = pressedEdge(GPIO_UP, upDown) || key == "up";
    DownPress = pressedEdge(GPIO_DOWN, downDown) || key == "down";
    SelPress = pressedEdge(GPIO_SEL, selDown) || key == "sel";
    EscPress = pressedEdge(GPIO_ESC, escDown) || key == "esc";
    AnyKeyPress = PrevPress || NextPress || UpPress || DownPress || SelPress || EscPress;
    if (AnyKeyPress) wakeUpScreen(); // Restarts the idle timer, as a board's InputHandler does.
}

// ---------------------------------------------------------------------------
// Panel_sdl owns main(): it pumps the SDL event/render loop on this thread
// and runs setup()/loop() on a second one, closing both when the window does.
// ---------------------------------------------------------------------------
__attribute__((weak)) int user_func(bool *running) {
    mapKeys();
    setup();
    do {
        loop();
    } while (*running);
    return 0;
}

int main(int, char **) { return lgfx::Panel_sdl::main(user_func, 128); }
