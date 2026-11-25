// Omarchy look for the Launcher: colours, Hyprland-style window frames, the top bar,
// menu-row selection, icons and the Omarchy logos. It follows Omarchy's own shell theme
// tokens (shell.toml): menu background and text, a selected row with the text colour at
// 8% behind accent text and a faint border, and the 2 px active-border gradient around
// focused windows.
#ifndef __OMARCHY_UI_H
#define __OMARCHY_UI_H

#include <globals.h>

namespace omarchy {

// Omarchy's "Hacker Neon" theme. These are the defaults for FGCOLOR (accent), ALCOLOR
// (text) and BGCOLOR (background); everything below reads those globals, so the
// Launcher's colour settings keep working.
constexpr uint16_t kAccent = 0x07F3;     // #00ff9c
constexpr uint16_t kAccentEnd = 0x073F;  // #00e5ff, the far end of the border gradient
constexpr uint16_t kText = 0xB7BA;       // #b6f5d2
constexpr uint16_t kBackground = 0x0041; // #050a08

constexpr int kBarHeight = 13; // The top bar, sized to the 13 px Omarchy logo.
constexpr int kBarTextX = 21;  // Where bar text starts, right of the logo.

// Size of the OMARCHY wordmark in logo pixels (omarchy_logo.h holds the bitmap).
constexpr int kWordmarkWidth = 81;
constexpr int kWordmarkHeight = 20;

// fg drawn over bg at alpha/255.
uint16_t blend(uint16_t fg, uint16_t bg, uint8_t alpha);
uint16_t selectionFill(); // Background of the selected row.
uint16_t mutedText();     // Secondary text.
uint16_t inactiveText();  // Items that can't be used right now.

// A border in the active-border gradient (accent to kAccentEnd, top-left to bottom-right),
// drawn inside the rectangle. alpha < 255 fades it into the background.
void frame(int x, int y, int w, int h, int thickness = 2, uint8_t alpha = 255);

// The Omarchy menu's selected row: tinted fill with a faint gradient outline.
void selection(int x, int y, int w, int h);

// Clears the top bar and draws the square Omarchy logo at its left end.
void bar();

// 8x8 icon for a main menu item by its short name ("SD", "OTA", ...), `scale` times
// larger. Other names (installed apps) get an app-window icon.
void menuIcon(const String &name, int x, int y, uint16_t color, int scale);

// Omarchy-style label for a main menu item ("SD" -> "SD Card"); empty for apps.
String menuLabel(const String &name);

// 8x8 Wi-Fi glyph for the top bar.
void wifiIcon(int x, int y, uint16_t color);

// The OMARCHY wordmark centred on cx, `scale` screen pixels per logo pixel.
void wordmark(int cx, int y, int scale);

#if defined(USE_NATIVE_SDL) || defined(USE_ARDUINO_GFX)
// Sends w x h pixels of RGB565 with their two bytes swapped (the panel's order) to the
// screen at x, y. For the full-screen drawings: the screensaver and the tile menu.
void pushFrame(const uint16_t *pixels, int x, int y, int w, int h);
#endif

} // namespace omarchy

#endif
