#include "display.h"
#include "app_registry.h"
#include "cardkb2.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "omarchy_screensaver.h"
#include "omarchy_ui.h"
#include "onlineLauncher.h"
#include "powerSave.h"
#include "ram_profile.h"
#include "sd_functions.h"
#include "settings.h"
#include "utils.h"
#include <cstring>
#include <globals.h>
#include <vector>

// The panel, its data bus and every pin come from the TFT_* / USE_* build flags
// in boards/<board>/platformio.ini — DisplayDrivers builds the whole chain from
// them. See the library's README for the flag vocabulary.
tft_display *tft = new tft_display();

/***************************************************************************************
** Function name: displayScrollingText
** Description:   Scroll large texts into screen
***************************************************************************************/
void displayScrollingText(const String &text, Opt_Coord &coord) {
    int len = text.length();
    static String displayText = "";
    static int i = 0;
    static long _lastmillis = 0;
#if defined(E_PAPER_DISPLAY)
    const int deadTime = 2000;
#else
    const int deadTime = 200;
#endif
    if (!displayText.startsWith(text)) i = 0;
    displayText = text + "        "; // Add spaces for smooth looping
    int scrollLen = len + 8;         // Full text plus space buffer
    tft->setTextColor(coord.fgcolor, coord.bgcolor);
    if (len < coord.size) {
        // Text fits within limit, no scrolling needed
        return;
    } else if (launcherMillis() > _lastmillis + deadTime) {
        String scrollingPart =
            displayText.substring(i, i + (coord.size - 1)); // Display charLimit characters at a time
        tft->fillRect(
            coord.x,
            coord.y,
            (coord.size - 1) * LW * tft->getTextSize(),
            LH * tft->getTextSize(),
            coord.bgcolor
        ); // Clear display area (the selected row has its own background)
        tft->setCursor(coord.x, coord.y);
        tft->setCursor(coord.x, coord.y);
        tft->print(scrollingPart);
        if (i >= scrollLen - coord.size) i = -1; // Loop back
        _lastmillis = launcherMillis();
#if defined(E_PAPER_DISPLAY)
        if (i == -1) {
            i = 0;
        } else i = i + 5;
#else
        i++;
        if (i == 1) _lastmillis = launcherMillis() + 1000;
#endif

        tft->display(false);
    }
}

/***************************************************************************************
** Function name: drawCharAt
** Description:   one character in explicit colours, current text colours kept
***************************************************************************************/
void drawCharAt(int32_t x, int32_t y, char c, uint16_t fg, uint16_t bg) {
    const uint16_t oldFg = tft->getTextColor();
    const uint16_t oldBg = tft->getTextBgColor();
    tft->setTextColor(fg, bg);
    tft->drawString(String(c), x, y);
    tft->setTextColor(oldFg, oldBg);
}

/***************************************************************************************
** Function name: drawOutlinedText
** Description:   Draw outlined text using a transparent sprite
***************************************************************************************/
#if defined(USE_EPD_PAINTER) || defined(USE_GXEPD2)
static void drawOutlinedText(
    const String &text, int32_t x, int32_t y, uint16_t textColor, uint16_t outlineColor, uint8_t outline
) {
    // The DisplayDrivers software sprite these e-paper backends use
    // (lib/DisplayDrivers/src/backends/swsprite.cpp) has no real font
    // renderer -- its drawString() is a stub that never plots glyph pixels,
    // so the sprite-based outline trick below (used on every other backend)
    // silently draws nothing here. Draw straight onto the real display
    // instead: nothing reaches the physical panel until the caller's own
    // tft->display() call, so offset copies here are free of flicker.
    if (outline == 0) {
        tft->setTextColor(textColor);
        tft->drawString(text, x, y, 1);
        return;
    }

    const int8_t radius = static_cast<int8_t>(outline);
    tft->setTextColor(outlineColor);
    for (int8_t dx = -radius; dx <= radius; ++dx) {
        for (int8_t dy = -radius; dy <= radius; ++dy) {
            if (dx == 0 && dy == 0) continue;
            tft->drawString(text, x + dx, y + dy, 1);
        }
    }
    tft->setTextColor(textColor);
    tft->drawString(text, x, y, 1);
}
#else
static void drawOutlinedText(
    const String &text, int32_t x, int32_t y, uint16_t textColor, uint16_t outlineColor, uint8_t outline
) {
    if (outline == 0) {
        tft->setTextColor(textColor);
        tft->drawString(text, x, y, 1);
        return;
    }

    const uint8_t textSize = tft->getTextSize();
    const int16_t textW = tft->textWidth(text, 1);
    const int16_t textH = tft->fontHeight(1);
    const int16_t spriteW = textW + 2 * outline;
    const int16_t spriteH = textH + 2 * outline;

    static tft_sprite sprite(tft);
    static int16_t cachedW = 0;
    static int16_t cachedH = 0;

    if (cachedW != spriteW || cachedH != spriteH) {
        sprite.deleteSprite();

        if (!sprite.createSprite(spriteW, spriteH)) {
            cachedW = 0;
            cachedH = 0;

            tft->setTextColor(textColor);
            tft->drawString(text, x, y, 1);
            return;
        }

        cachedW = spriteW;
        cachedH = spriteH;
    }

    uint16_t transparentColor = 0;
    while (transparentColor == textColor || transparentColor == outlineColor) { ++transparentColor; }

    sprite.fillScreen(transparentColor);
    sprite.setTextSize(textSize);
    sprite.setTextColor(outlineColor);

    const int8_t radius = static_cast<int8_t>(outline);

    for (int8_t dx = -radius; dx <= radius; ++dx) {
        for (int8_t dy = -radius; dy <= radius; ++dy) {
            if (dx == 0 && dy == 0) continue;

            sprite.drawString(text, outline + dx, outline + dy, 1);
        }
    }

    sprite.setTextColor(textColor);
    sprite.drawString(text, outline, outline, 1);

    sprite.pushSprite(x - outline, y - outline, transparentColor);
}
#endif

static inline void drawOptionsErase(const Opt_Coord &coord) {
    if (coord.boxW == 0 || coord.boxH == 0) return;
    tft->fillRect(coord.boxX, coord.boxY, coord.boxW, coord.boxH, coord.bgcolor);
    tft->display(false);
}

/***************************************************************************************
** Function name: resetTftDisplay
** Description:   set cursor to 0,0, screen and text to default color
***************************************************************************************/
void resetTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg, uint16_t screen) {
    tft->setCursor(x, y);
    tft->fillScreen(screen);
    tft->setTextSize(size);
    tft->setTextColor(fc, bg);
}

/***************************************************************************************
** Function name: setTftDisplay
** Description:   set cursor, font color, size and bg font color
***************************************************************************************/
void setTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg) {
    if (x >= 0 && y < 0) tft->setCursor(x, tft->getCursorY());      // if -1 on x, sets only y
    else if (x < 0 && y >= 0) tft->setCursor(tft->getCursorX(), y); // if -1 on y, sets only x
    else if (x >= 0 && y >= 0) tft->setCursor(x, y);                // if x and y > 0, sets both
    tft->setTextSize(size);
    tft->setTextColor(fc, bg);
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter(uint16_t color) {
#if defined(HAS_TOUCH) && !defined(HAS_TOUCH_NO_BORDER)
    tft->drawRoundRect(5 + RES, tftHeight + 2, tftWidth - 10 - 2 * RES, (_fm * LH + 4), 5, color);
    tft->setTextColor(color);
    tft->setTextSize(_fm);
    tft->drawString("<<<", 11 + RES, tftHeight + 4);
    tft->drawCentreString("SEL", tftWidth / 2, tftHeight + 4, 1);
    tft->drawRightString(">>>", tftWidth - (RES + 11), tftHeight + 4, 1);
#endif
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter2(uint16_t color) {
#if defined(HAS_TOUCH) && !defined(HAS_TOUCH_NO_BORDER)
    tft->drawRoundRect(5 + RES, tftHeight + 2, tftWidth - 10 - 2 * RES, (_fm * LH + 4), 5, color);
    tft->setTextColor(color);
    tft->setTextSize(_fm);
    tft->drawString("<<", 11 + RES, tftHeight + 4);
    tft->drawCentreString("LAUNCHER", tftWidth / 2, tftHeight + 4, 1);
    tft->drawRightString(">>", tftWidth - (RES + 11), tftHeight + 4, 1);
#endif
}

/***************************************************************************************
** Function name: BootScreen
** Description:   Start Display functions and display bootscreen
***************************************************************************************/
void initDisplay(bool doAll) {
#ifndef HEADLESS
    // Credit shown in the corner, as on upstream's bootscreen.
    static uint8_t _name = launcherRandom(0, 3);
    String name = "@Pirata";
    if (_name == 1) name = "u/bmorcelli";
    else if (_name == 2) name = "gh/bmorcelli";

#ifdef E_PAPER_DISPLAY // epaper display draws only once
    static bool runOnce = false;
    if (runOnce && !doAll) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        return;
    }
    runOnce = true;

#endif
    // Omarchy bootscreen: a full-screen window with the OMARCHY wordmark and a blinking
    // cursor after "launcher". The first call draws it; later calls only blink the cursor.
    static int cursorX = 0;
    static int cursorY = 0;
    static bool cursorOn = false;
    static uint32_t lastBlink = 0;
    const int lineH = LH * _fp;
    if (doAll) {
        omarchy::frame(0, 0, tftWidth, tftHeight);
        int top = 6;
#if defined(HAS_TOUCH) || defined(HAS_KEYBOARD) || defined(USE_CARDKB2)
        std::vector<MenuOptions> &bootAppShortcuts = launcherBootAppShortcuts();
        if (!bootAppShortcuts.empty()) top = drawBootAppShortcuts(bootAppShortcuts) + 4;
#endif
        int scale = (tftWidth - 16) / omarchy::kWordmarkWidth;
        if (scale < 1) scale = 1;
        if (scale > 4) scale = 4;
        String selectedAppName = launcherSelectedBootAppName();
        const int logoH = omarchy::kWordmarkHeight * scale;
        const int blockH = logoH + 6 + lineH + (selectedAppName.isEmpty() ? 0 : lineH + 4);
        int y = top + (tftHeight - 6 - lineH - top - blockH) / 2;
        if (y < top) y = top;
        omarchy::wordmark(tftWidth / 2, y, scale);
        y += logoH + 6;

        tft->setTextSize(_fp);
        tft->setTextColor(ALCOLOR, BGCOLOR);
        const String label = "launcher";
        const int labelW = tft->textWidth(label, 1);
        const int labelX = (tftWidth - labelW - LW * _fp) / 2;
        tft->drawString(label, labelX, y);
        cursorX = labelX + labelW + 1;
        cursorY = y;
        cursorOn = false;
        lastBlink = 0;

        if (!selectedAppName.isEmpty()) {
            selectedAppName = selectedAppName.substring(0, tftWidth / (LW * _fp) - 4);
            tft->setTextColor(FGCOLOR, BGCOLOR);
            tft->drawCentreString(selectedAppName, tftWidth / 2, y + lineH + 4, 1);
        }
        tft->setTextColor(omarchy::mutedText(), BGCOLOR);
        tft->drawRightString(name, tftWidth - 6, tftHeight - 4 - lineH, 1);
    }
    if (launcherMillis() - lastBlink >= 500) {
        lastBlink = launcherMillis();
        cursorOn = !cursorOn;
        tft->fillRect(cursorX, cursorY, LW * _fp - 1, lineH, cursorOn ? FGCOLOR : BGCOLOR);
    }

    if (doAll) TouchFooter2();
    tft->display(false);
    vTaskDelay(50 / portTICK_PERIOD_MS);
#endif
}
/***************************************************************************************
** Function name: initDisplayLoop
** Description:   Start Display functions and display bootscreen
***************************************************************************************/
void initDisplayLoop() {
    tft->fillScreen(BGCOLOR);
    initDisplay(true);
    vTaskDelay(pdTICKS_TO_MS(250));
    while (!check(AnyKeyPress)) {
        initDisplay();
        vTaskDelay(pdTICKS_TO_MS(50));
    }
    returnToMenu = true;
}

/***************************************************************************************
** Function name: displayCurrentVersion
** Description:   Display Version on Screen before instalation
***************************************************************************************/
void displayCurrentVersion(
    const String &name, const String &author, const String &version, const String &published_at,
    int versionIndex, JsonArray versions
) {
    // tft->fillScreen(BGCOLOR);
    tft->fillRect(0, tftHeight - 5, tftWidth, 5, BGCOLOR);
    tft->fillRect(5, 5, tftWidth - 10, tftHeight - 10, BGCOLOR);
    omarchy::frame(5, 5, tftWidth - 10, tftHeight - 10); // Omarchy window

    setTftDisplay(10, 10, ~BGCOLOR, _fm, BGCOLOR);
    String name2 = String(name);
    tftprintln(name2, 10, 2);
    // Tall panels have room to put the author on its own line.
    if (panelHeight() > 200) setTftDisplay(10, 50, ALCOLOR, _fm);
    tft->print("by: ");
    tft->setTextColor(~BGCOLOR);
    tft->println(String(author).substring(0, 14));

    tft->setTextColor(ALCOLOR);
    tft->setCursor(10, tft->getCursorY());
    tft->print("v: ");
    tft->setTextColor(~BGCOLOR);
    tft->println(String(version).substring(0, 15));

    tft->setTextColor(ALCOLOR);
    tft->setCursor(10, tft->getCursorY());
    tft->print("from: ");
    tft->setTextColor(~BGCOLOR);
    tft->println(String(published_at));

    if (versions.size() > 1) {
        tft->setTextColor(ALCOLOR);
        drawCharAt(10, tftHeight - (10 + _fm * 9), '<', FGCOLOR, BGCOLOR);
        drawCharAt(tftWidth - (10 + _fm * 6), tftHeight - (10 + _fm * 9), '>', FGCOLOR, BGCOLOR);
        tft->setTextColor(~BGCOLOR);
    }

    setTftDisplay(-1, -1, ALCOLOR, _fm, BGCOLOR);
    tft->drawCentreString("Options", tftWidth / 2, tftHeight - (10 + _fm * 9), 1);
    omarchy::frame(
        tftWidth / 2 - 3 * _fm * 11, tftHeight - (12 + _fm * 9), _fm * 6 * 11, _fm * 8 + 3, 1, 110
    ); // Faint Omarchy border

    int div = versions.size();
    if (div == 0) div = 1;

    TouchFooter(ALCOLOR);

    int bar = int(tftWidth / div);
    if (bar < 5) bar = 5;
    tft->fillRect((tftWidth * versionIndex) / div, tftHeight - 5, bar, 5, ALCOLOR);

    tft->display(false);
}

/***************************************************************************************
** Function name: displayRedStripe
** Description:   Display Red Stripe with information
***************************************************************************************/
void displayRedStripe(const String &text, uint16_t fgcolor, uint16_t bgcolor, bool keepAwake) {
    // save tft settings before showing the stripe
    int _size = tft->getTextSize();
    int _x = tft->getCursorX();
    int _y = tft->getCursorY();
    uint16_t _color = tft->getTextColor();
    uint16_t _bgcolor = tft->getTextBgColor();
    Serial.println(String("Display Red Stripe: ") + text);
    // A stripe is put up to be read, so it restarts the idle clock. Long stages —
    // an HTTP connect, an erase, a retry backoff — otherwise pass without a single
    // wake, and the user ends up watching an unlit screen for the whole of one.
    if (keepAwake) wakeUpScreen();

#if E_PAPER_DISPLAY
    bgcolor = BLACK;
    fgcolor = WHITE;
#endif
#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
#endif

    // stripe drawing
    int size = text.length() * LW * _fm < (tft->width() - 2 * _fm * LW) ? _fm : _fp;
    int paddingX = 8;
    int paddingY = 5;
    int rectX = 10;
    int rectW = tftWidth - 20;
    int maxLineChars = (rectW - 2 * paddingX) / (LW * size);
    if (maxLineChars < 1) maxLineChars = 1;

    std::vector<String> lines;
    String line;
    String word;
    auto appendWord = [&]() {
        if (word.isEmpty()) return;

        while (static_cast<int>(word.length()) > maxLineChars) {
            if (!line.isEmpty()) {
                lines.push_back(line);
                line = "";
            }
            lines.push_back(word.substring(0, maxLineChars));
            word = word.substring(maxLineChars);
        }

        int extraSpace = line.isEmpty() ? 0 : 1;
        if (!line.isEmpty() && static_cast<int>(line.length() + extraSpace + word.length()) > maxLineChars) {
            lines.push_back(line);
            line = "";
        }
        if (!line.isEmpty()) line += " ";
        line += word;
        word = "";
    };

    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        if (c == '\n') {
            appendWord();
            lines.push_back(line);
            line = "";
        } else if (c == ' ') {
            appendWord();
        } else {
            word += c;
        }
    }
    appendWord();
    if (!line.isEmpty() || lines.empty()) lines.push_back(line);

    int lineHeight = size * LH;
    int rectH = static_cast<int>(lines.size()) * lineHeight + 2 * paddingY;
    int maxRectH = tftHeight > 20 ? tftHeight - 20 : tftHeight;
    if (rectH > maxRectH) rectH = maxRectH;
    int rectY = tftHeight / 2 - rectH / 2;
    if (rectY < 0) rectY = 0;

    // Omarchy notification: background colour inside the active-border gradient.
    tft->fillRect(rectX, rectY, rectW, rectH, BGCOLOR);
    omarchy::frame(rectX, rectY, rectW, rectH);
    tft->setTextColor(ALCOLOR, BGCOLOR);
    tft->setTextSize(size);

    int visibleLines = (rectH - 2 * paddingY) / lineHeight;
    int textY = rectY + (rectH - visibleLines * lineHeight) / 2;
    for (int i = 0; i < visibleLines && i < static_cast<int>(lines.size()); ++i) {
        tft->drawCentreString(lines[i], tftWidth / 2, textY + i * lineHeight, 1);
    }

    tft->display(false);
#if E_PAPER_DISPLAY
#if defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
#endif
#endif
    // return previous tft settings
    tft->setTextSize(_size);
    tft->setTextColor(_color, _bgcolor);
    tft->setCursor(_x, _y);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/***************************************************************************************
** Function name: displayError
** Description:   Show an error via the red stripe, then either wait for a keypress
**               or block for a fixed delay, depending on waitKeyPress.
***************************************************************************************/
void displayError(String txt, bool waitKeyPress) {
    displayRedStripe(txt);
    Serial.println("ERR: " + txt);
    if (!waitKeyPress) {
        const uint32_t tmp = launcherMillis() + 2000;
        while (launcherMillis() < tmp) {
            vTaskDelay(pdMS_TO_TICKS(10));
            if (check(AnyKeyPress)) break;
        }
    }
    while (waitKeyPress && !check(AnyKeyPress)) vTaskDelay(pdMS_TO_TICKS(10));
}

void displayMsg(String txt, bool waitKeyPress) { displayError(txt, waitKeyPress); }

/***************************************************************************************
** Function name: progressHandler
** Description:   Função para manipular o progresso da atualização
** Dependencia: prog_handler =>>    0 - Flash, 1 - SPIFFS
***************************************************************************************/
void progressHandler(size_t progress, size_t total) {
    vTaskDelay(pdMS_TO_TICKS(2));
#if defined(E_PAPER_DISPLAY)
    static unsigned long lastUpdate = 0;
#endif
    static unsigned long lastProgressDraw = 0;
    static size_t lastProgressBarWidth = 0;
    double fraction = (double)progress / (double)total;
    double barWidthFloat = (tftWidth - 40) * fraction;
    size_t barWidth = static_cast<size_t>(barWidthFloat);
    // Serial.printf("Total: %d, Progress: %d, Progress bar width: %d \n", total, progress, barWidth);
    if (progress == 0) {
        lastProgressDraw = launcherMillis();
        lastProgressBarWidth = 0;
        tft->drawPixel(0, 0, 0);
        // Omarchy: a full-screen window, the stage as a notification, and a progress bar
        // with a tinted track and an accent fill.
        tft->fillRect(2, 2, tftWidth - 4, tftHeight - 4, BGCOLOR);
        omarchy::frame(0, 0, tftWidth, tftHeight);
        tft->setTextSize(_fp);
        tft->setTextColor(omarchy::mutedText(), BGCOLOR);
        tft->drawCentreString("launcher", tftWidth / 2, panelHeight() > 200 ? 20 : 8, 1);
        const int barY = prog_handler == 1 ? tftHeight - 28 : tftHeight - 47;
        omarchy::frame(18, barY, tftWidth - 36, 17, 1, 110);
        tft->fillRect(20, barY + 2, tftWidth - 40, 13, omarchy::selectionFill());

        String txt;
        switch (prog_handler) {
            case 0: txt = "Installing FW"; break;
            case 1: txt = "Copying Data"; break;
            case 2: txt = "Downloading"; break;
        }
        displayRedStripe(txt);
    }
    if (progress > 0 && progress < total) {
        unsigned long now = launcherMillis();
        if (barWidth == lastProgressBarWidth || now - lastProgressDraw < 80) {
            wakeUpScreen();
            return;
        }
        lastProgressDraw = now;
        lastProgressBarWidth = barWidth;
    }
    tft->drawPixel(0, 0, 0);
    tft->fillRect(20, (prog_handler == 1 ? tftHeight - 28 : tftHeight - 47) + 2, barWidth, 13, FGCOLOR);

#if defined(E_PAPER_DISPLAY)
    if (launcherMillis() - lastUpdate > 2000) {
        tft->display();
        lastUpdate = launcherMillis();
    }
#else
    tft->display();
#endif
#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
#endif
    wakeUpScreen();
    vTaskDelay(pdMS_TO_TICKS(2));
}

/***************************************************************************************
** Function name: drawOptions
** Description:   Função para desenhar e mostrar as opçoes de contexto
***************************************************************************************/
Opt_Coord drawOptions(
    int idx, std::vector<Option> &opt, std::vector<MenuOptions> &t_menu, uint16_t fgcolor, uint16_t bgcolor,
    bool border, bool forceFullRedraw
) {
    // Cached layout of the previous call, used to tell whether this redraw only moved the
    // selection within the same page (draw just the two changed rows) or whether the page,
    // menu or styling changed (repaint everything). loopOptions is the sole caller and always
    // passes forceFullRedraw=true on the first draw of a session, so stale statics from an
    // unrelated menu can never leak into a partial redraw.
    static int lastIndex = -1;
    static int lastStart = -1;
    static int lastOptionCount = -1;
    static int lastArraySize = -1;
    static bool lastShowPageUp = false;
    static bool lastShowPageDown = false;
    static bool lastBorder = false;
    static uint16_t lastFgcolor = 0;
    static uint16_t lastBgcolor = 0;
    static int lastBoxX = -1;
    static int lastBoxY = -1;

    int index = idx;
    uint16_t alcolor = ALCOLOR;
#ifdef E_PAPER_DISPLAY
    bgcolor = WHITE; // 0xffff
    alcolor = BLACK; // 0x0000
    fgcolor = BLACK;
#endif

    Opt_Coord coord;
    coord.bgcolor = bgcolor;
    coord.fgcolor = fgcolor;

    t_menu.clear();
    int arraySize = opt.size();
    if (arraySize == 0) { return coord; }

    if (index < 0) index = 0;
    if (index >= arraySize) index = arraySize - 1;

    int lineHeight = _fm * LH;
    const int rowSpacing = 4;
    const int paddingTop = 4;
    const int paddingBottom = 4;
    const int paddingSide = 4;

    int contentWidth = border ? static_cast<int>(tftWidth * 0.8f) : tftWidth - 4;
    if (contentWidth < 1) contentWidth = tftWidth;
    int boxX = border ? (tftWidth - contentWidth) / 2 : 2;

    int availableHeight = border ? (tftHeight - 20) : (tftHeight - 4);
    int minHeight = lineHeight + paddingTop + paddingBottom;
    if (availableHeight < minHeight) availableHeight = minHeight;

    int totalRows = (availableHeight - paddingTop - paddingBottom + rowSpacing) / (lineHeight + rowSpacing);
    if (totalRows < 1) totalRows = 1;

    int start = 0;
    int optionCount = 0;
    bool showPageUp = false;
    bool showPageDown = false;

#ifdef HAS_TOUCH
    struct PageInfo {
        int start;
        int count;
        bool pageUp;
        bool pageDown;
    };

    std::vector<PageInfo> pages;
    int remaining = arraySize;
    int pageStart = 0;
    while (remaining > 0) {
        bool hasUp = !pages.empty();
        int maxOptions = totalRows - (hasUp ? 1 : 0);
        if (maxOptions < 1) maxOptions = 1;
        int count = remaining < maxOptions ? remaining : maxOptions;
        bool hasDown = (remaining > count);
        if (hasDown) {
            int maxWithDown = totalRows - (hasUp ? 1 : 0) - 1;
            if (maxWithDown < 1) maxWithDown = 1;
            count = count < maxWithDown ? count : maxWithDown;
            if (count >= remaining) hasDown = false;
        }
        if (count < 1) { count = remaining < 1 ? remaining : 1; }

        pages.push_back({pageStart, count, hasUp, hasDown});
        pageStart += count;
        remaining -= count;
    }

    if (pages.empty()) { pages.push_back({0, 0, false, false}); }

    int currentPage = 0;
#ifdef HAS_TOUCH
    int maxRowsAcrossPages = 0;
#endif
    for (size_t p = 0; p < pages.size(); ++p) {
#ifdef HAS_TOUCH
        int rowsForPage = pages[p].count + (pages[p].pageUp ? 1 : 0) + (pages[p].pageDown ? 1 : 0);
        if (rowsForPage > maxRowsAcrossPages) maxRowsAcrossPages = rowsForPage;
#endif

        int pageStartIndex = pages[p].start;
        int pageEndIndex = pageStartIndex + pages[p].count;
        if (pages[p].count == 0 && index == 0) { currentPage = p; }
        if (index >= pageStartIndex && index < pageEndIndex) {
            currentPage = p;
            break;
        }
        if (p == pages.size() - 1) { currentPage = p; }
    }

    start = pages[currentPage].start;
    optionCount = pages[currentPage].count;
    showPageUp = pages[currentPage].pageUp;
    showPageDown = pages[currentPage].pageDown;

    if (optionCount == 0) { optionCount = arraySize < totalRows ? arraySize : totalRows; }
#else
    start = (index / totalRows) * totalRows;
    optionCount = arraySize < totalRows ? arraySize : totalRows;
#endif

    int rowsThisPage = optionCount + (showPageUp ? 1 : 0) + (showPageDown ? 1 : 0);
    if (rowsThisPage < 1) rowsThisPage = 1;

    int rowsForHeight = rowsThisPage;
#ifdef HAS_TOUCH
    rowsForHeight = rowsForHeight > maxRowsAcrossPages ? rowsForHeight : maxRowsAcrossPages;
#else
    rowsForHeight = rowsForHeight > optionCount ? rowsForHeight : optionCount;
#endif
    int contentHeight =
        paddingTop + paddingBottom + rowsForHeight * lineHeight + (rowsForHeight - 1) * rowSpacing;
    int boxY;
    if (border) {
        boxY = (tftHeight - contentHeight) / 2;
        if (boxY < 10) boxY = 10;
        if (boxY + contentHeight > tftHeight - 10) boxY = tftHeight - 10 - contentHeight;
        if (boxY < 10) boxY = 10;
        if (boxY < 0) boxY = 0;
    } else {
        boxY = 2;
        contentHeight = tftHeight - 4;
    }
    coord.boxX = boxX;
    coord.boxY = boxY;
    coord.boxW = contentWidth;
    coord.boxH = contentHeight;

    // Same page, same menu, same styling, only the selected row moved: repaint just the
    // previously- and newly-selected rows instead of the whole box. Anything structural
    // (paging, menu identity, colors, first draw) falls back to a full repaint.
    bool partialRedraw = !forceFullRedraw && index != lastIndex && start == lastStart &&
                         optionCount == lastOptionCount && arraySize == lastArraySize &&
                         showPageUp == lastShowPageUp && showPageDown == lastShowPageDown &&
                         border == lastBorder && fgcolor == lastFgcolor && bgcolor == lastBgcolor &&
                         boxX == lastBoxX && boxY == lastBoxY;
    int prevIndex = lastIndex;

    lastIndex = index;
    lastStart = start;
    lastOptionCount = optionCount;
    lastArraySize = arraySize;
    lastShowPageUp = showPageUp;
    lastShowPageDown = showPageDown;
    lastBorder = border;
    lastFgcolor = fgcolor;
    lastBgcolor = bgcolor;
    lastBoxX = boxX;
    lastBoxY = boxY;

    bool firstItemSelected = (optionCount > 0 && index == start);
    tft->setTextSize(_fm);

    if (!partialRedraw) {
        // Omarchy menu card, or a full-screen window, inside the active-border gradient.
        if (border) {
            if (firstItemSelected) tft->fillRect(boxX, boxY, contentWidth, contentHeight, bgcolor);
            omarchy::frame(boxX, boxY, contentWidth, contentHeight);
        } else {
            if (firstItemSelected) tft->fillRect(2, 2, tftWidth - 4, tftHeight - 4, bgcolor);
            omarchy::frame(0, 0, tftWidth, tftHeight);
        }
    }

    int lineWidth = contentWidth - paddingSide * 2;
    if (lineWidth < 0) lineWidth = contentWidth;
    int charWidth = LW * tft->getTextSize();
    if (charWidth <= 0) charWidth = 1;
    int indicatorWidth = charWidth;
    if (indicatorWidth > lineWidth) indicatorWidth = lineWidth;
    int textStartY = boxY + paddingTop;
    int rowIndex = 0;

    auto addNavLine = [&](const char *text, bool isUp, bool paint) {
        int rowTop = textStartY + rowIndex * (lineHeight + rowSpacing);
        if (paint) {
            int textWidth = strlen(text) * charWidth;
            int navX =
                boxX + paddingSide + 0 > ((lineWidth - textWidth) / 2) ? 0 : ((lineWidth - textWidth) / 2);
            tft->fillRect(boxX + paddingSide, rowTop, lineWidth, lineHeight, bgcolor);
            tft->setCursor(navX, rowTop);
            tft->setTextColor(alcolor, bgcolor);
            tft->drawCentreString(text, tftWidth / 2, rowTop, 1);
        }

        MenuOptions navItem("", isUp ? "-" : "+", nullptr, true, false);
        navItem.setCoords(boxX + paddingSide, rowTop, lineWidth, lineHeight + rowSpacing);
        t_menu.push_back(navItem);

        rowIndex++;
    };
#ifdef HAS_TOUCH
    if (showPageUp) { addNavLine("-- Page Up --", true, !partialRedraw); }
#endif
    for (int i = 0; i < optionCount && (start + i) < arraySize; ++i) {
        ALIVIATE_TASK;
        int optionIndex = start + i;
        int rowTop = textStartY + rowIndex * (lineHeight + rowSpacing);
        int rowLeft = boxX + paddingSide;
        bool rowNeedsPaint = !partialRedraw || optionIndex == index || optionIndex == prevIndex;
        if (rowNeedsPaint && i > 0)
            tft->fillRect(rowLeft, rowTop - rowSpacing, lineWidth, rowSpacing, bgcolor);
        int prefixWidth = 0;
        int cursorX = rowLeft;
#ifdef HAS_TOUCH
        int escWidth = 0;
        bool showEscLabel = (!border && i == 0);
#endif
#ifdef HAS_TOUCH
        if (RES && !border) {
            if (i < (RES / (LH * _fm) + 1)) cursorX += RES - i * LW * _fm;
        }
#endif

        // Omarchy menu row: no caret; the selected row is tinted, outlined and in accent.
        const bool selected = optionIndex == index;
        const uint16_t rowBg = selected ? omarchy::selectionFill() : bgcolor;
        if (rowNeedsPaint) {
            tft->fillRect(rowLeft - 2, rowTop - 2, lineWidth + 4, lineHeight + 4, bgcolor);
            if (selected) omarchy::selection(rowLeft - 2, rowTop - 2, lineWidth + 4, lineHeight + 4);
        }
        prefixWidth += indicatorWidth;
        cursorX += indicatorWidth;

        uint16_t color = opt[optionIndex].color;
        if (color == NO_COLOR) color = fgcolor;
#ifdef E_PAPER_DISPLAY
        color = BLACK;
#endif

        int labelX = cursorX;
        int labelWidth = lineWidth - prefixWidth;
#ifdef HAS_TOUCH
        if (showEscLabel) {
            const char *escText = "[ESC] ";
            escWidth = strlen(escText) * charWidth;
            int escX = boxX + paddingSide + lineWidth - escWidth;
            if (escX < labelX) escX = labelX;
            if (rowNeedsPaint) {
                tft->setCursor(escX, rowTop);
                tft->setTextColor(alcolor, bgcolor);
                tft->print(escText);
            }

            MenuOptions escItem("", "ESC", nullptr, true, false);
            escItem.setCoords(
                escX > 4 ? escX - 4 : 0, rowTop > 2 ? rowTop - 2 : 0, escWidth + 8, lineHeight + rowSpacing
            );
            t_menu.push_back(escItem);

            labelWidth -= escWidth + charWidth;
        }
#endif
        if (RES && !border) {
            if (i < (RES / (LH * _fm) + 1)) { labelWidth -= RES / (i + 1); }
            if (i >= (optionCount - (RES / (LH * _fm) + 1))) { labelWidth -= RES / (optionCount - i); }
        }
        if (labelWidth < 0) labelWidth = 0;
        int labelCharLimit = labelWidth / charWidth;
        if (labelCharLimit < 1) labelCharLimit = 1;

        char txt[labelCharLimit];
        snprintf(txt, sizeof(txt), "%-*s", labelCharLimit, opt[optionIndex].label.c_str());

        if (rowNeedsPaint) {
            tft->setCursor(labelX, rowTop);
            tft->setTextColor(selected ? FGCOLOR : color, rowBg);
            tft->print(txt);
        }

        MenuOptions optItem(String(optionIndex), "", nullptr, true, optionIndex == index);
        optItem.setCoords(labelX, rowTop, 0 > labelWidth ? 0 : labelWidth, lineHeight + rowSpacing);
        t_menu.push_back(optItem);

        if (optionIndex == index) {
            coord.x = labelX;
            coord.y = rowTop;
            coord.size = labelCharLimit;
            coord.fgcolor = FGCOLOR;
            coord.bgcolor = rowBg;
        }

        rowIndex++;
    }
#ifdef HAS_TOUCH
    if (showPageDown) { addNavLine("-- Page Down --", false, !partialRedraw); }
#endif
    if (partialRedraw) {
        tft->display(false);
        return coord;
    }
    if (rowIndex < rowsForHeight) {
        int rowLeft = boxX + paddingSide;
        while (rowIndex < rowsForHeight) {
            int rowTop = textStartY + rowIndex * (lineHeight + rowSpacing);
            tft->fillRect(rowLeft, rowTop, lineWidth, lineHeight, bgcolor);
            tft->setCursor(rowLeft, rowTop);
            tft->setTextColor(fgcolor, bgcolor);
            tft->print(' ');
            rowIndex++;
        }
    }
    TouchFooter(FGCOLOR);
    tft->display(false);

    return coord;
}
/***************************************************************************************
** Function name: drawMainMenu
** Description:   Função para desenhar e mostrar o menu principal
***************************************************************************************/
// One main menu row in the Omarchy menu style: icon and label, the selected row tinted.
static void drawMainMenuRow(MenuOptions &o, int x, int y, int w, int h, bool selected) {
    tft->fillRect(x, y, w, h, BGCOLOR);
    if (selected) omarchy::selection(x, y, w, h);
    const uint16_t bg = selected ? omarchy::selectionFill() : BGCOLOR;
    const uint16_t fg = !o.active ? omarchy::inactiveText() : selected ? FGCOLOR : ALCOLOR;
    const int scale = h >= 18 ? 2 : 1;
    const int iconSize = 8 * scale;
    omarchy::menuIcon(o.name, x + 5, y + (h - iconSize) / 2, fg, scale);
    String label = omarchy::menuLabel(o.name);
    if (label.isEmpty()) label = o.text; // Installed apps show their name.
    const int textX = x + 5 + iconSize + 7;
    const int maxChars = (x + w - 4 - textX) / (LW * _fm);
    if (static_cast<int>(label.length()) > maxChars) label = label.substring(0, maxChars);
    tft->setTextSize(_fm);
    tft->setTextColor(fg, bg);
    tft->drawString(label, textX, y + (h - LH * _fm) / 2);
}

void drawMainMenu(std::vector<MenuOptions> &opt, int index, bool forceFullRedraw) {
    // Omarchy restyle: a one-column list in a window under the top bar, like the Omarchy
    // menu, with the selected item's description in the bar. The last selection and
    // scroll position are kept so a move only repaints the two rows that changed.
    static int lastIndex = -1;
    static int lastFirst = -1;

    const int size = opt.size();
    if (size < 1) {
        displayRedStripe("No options available");
        return;
    }
    const int winY = omarchy::kBarHeight + 1;
    const int rowH = 18;
    const int left = 5;
    const int innerTop = winY + 3;
    const int innerBottom = tftHeight - 4;
    int visible = (innerBottom - innerTop) / rowH;
    if (visible < 1) visible = 1;
    if (visible > size) visible = size;

    int first = (lastFirst < 0 || lastFirst >= size) ? 0 : lastFirst;
    if (index < first) first = index;
    if (index >= first + visible) first = index - visible + 1;
    if (first > size - visible) first = size - visible;
    const bool scrollbar = size > visible;
    const int rowW = tftWidth - 2 * left - (scrollbar ? 4 : 0);
    const int top = innerTop + (innerBottom - innerTop - visible * rowH) / 2;

    const bool fullRedraw = forceFullRedraw || first != lastFirst || lastIndex < 0 || lastIndex >= size;
    const int prevIndex = lastIndex;
    lastIndex = index;
    lastFirst = first;

    if (fullRedraw) {
        tft->fillScreen(BGCOLOR);
        drawDeviceBorder();
    }
    for (int i = 0; i < size; ++i) opt[i].resetCoords();
    for (int slot = 0; slot < visible; ++slot) {
        ALIVIATE_TASK;
        const int i = first + slot;
        const int y = top + slot * rowH;
        opt[i].setCoords(left, y, rowW, rowH);
        if (!fullRedraw && i != index && i != prevIndex) continue;
        drawMainMenuRow(opt[i], left, y, rowW, rowH, i == index);
    }
    if (scrollbar) {
        const int trackX = tftWidth - left - 2;
        const int trackH = visible * rowH;
        const int thumbH = trackH * visible / size;
        tft->fillRect(trackX, top, 2, trackH, omarchy::selectionFill());
        tft->fillRect(
            trackX, top + (trackH - thumbH) * first / (size - visible), 2, thumbH, omarchy::mutedText()
        );
    }

    // The selected item's description, in the bar between the logo and the status icons.
    const int textW = tftWidth - omarchy::kBarTextX - 62;
    tft->fillRect(omarchy::kBarTextX, 0, textW, omarchy::kBarHeight, BGCOLOR);
    String desc = opt[index].text;
    const int maxChars = textW / (LW * _fp);
    if (static_cast<int>(desc.length()) > maxChars) desc = desc.substring(0, maxChars);
    tft->setTextSize(_fp);
    tft->setTextColor(ALCOLOR, BGCOLOR);
    tft->drawString(desc, omarchy::kBarTextX, (omarchy::kBarHeight - LH * _fp) / 2 + 1);

    drawStatusIcons(fullRedraw);
    tft->display(false);
}

/***************************************************************************************
** Function name: drawStatusIcons
** Description:   Repaints the battery/wifi icons of the main menu header when their state
**                 changed (or always, after a full redraw). Returns true if anything was drawn.
***************************************************************************************/
bool drawStatusIcons(bool reset) {
    static int bat = 0;
    static bool chg = false;
    static bool wifi = false;
    if (reset) {
        bat = 0;
        wifi = false;
    }
    bool changed = false;
    int _bat = getBattery();
    bool _chg = isCharging();
    if ((bat != _bat || chg != _chg) && _bat > 0) {
        drawBatteryStatus(_bat, _chg);
        bat = _bat;
        chg = _chg;
        changed = true;
    }
    bool _wifi = launcherWifiIsConnected();
    if (_wifi && wifi != _wifi) {
        drawWifiStatus(bat > 0);
        changed = true;
    }
    wifi = _wifi;
    return changed;
}
/***************************************************************************************
** Function name: launcherBootAppShortcuts
** Description:   Lazily builds and caches the boot shortcut cards (one per installed
**                 app). Shared by initDisplay (drawing) and the bootscreen input loop
**                 (touch hit-testing), so both sides agree on labels/coordinates.
***************************************************************************************/
std::vector<MenuOptions> &launcherBootAppShortcuts() {
    static std::vector<MenuOptions> shortcuts;
    static bool built = false;
    if (!built) {
        for (const LauncherAppMetadata &app : launcherListInstalledApps()) {
            String label = app.label;
            String icon = (app.name.isEmpty() ? app.label : app.name).substring(0, 5);
            icon.toUpperCase();
            shortcuts.push_back({icon, "", [label]() { launcherBootAppByLabel(label.c_str()); }});
        }
        built = true;
    }
    return shortcuts;
}

/***************************************************************************************
** Function name: drawBootAppShortcuts
** Description:   Draws a row of cards with each installed app's initials at the top of
**                 the bootscreen, with its keyboard digit shortcut in the top-left
**                 corner, so a tap/keypress boots that app directly. Returns the total
**                 height, in pixels, occupied by the cards.
***************************************************************************************/
int drawBootAppShortcuts(std::vector<MenuOptions> &opt) {
    uint8_t size = opt.size();

    if (size < 1) return 0;

#if defined(USE_CARDKB2) && !defined(HAS_TOUCH)
    if (!CardKB2Installed) return 0;
#endif

    int boxH = LH * _fm + 8;
    int minBoxW = boxH * 2; // keep the card proportional to its (now taller) height
    int maxCols = (tftWidth - 8) / minBoxW;
    if (maxCols < 1) maxCols = 1;
    if (maxCols > size) maxCols = size;
    int boxW = (tftWidth - 8) / maxCols;
    int y = 4;

    // Only as many characters as actually fit the card, so names never overlap
    // when several apps are installed and the cards get narrow.
    int maxNameChars = (boxW - 6) / (LW * _fm);
    if (maxNameChars < 1) maxNameChars = 1;

    for (int i = 0; i < size; ++i) {
        int col = i % maxCols;
        int row = i / maxCols;
        int x = 4 + col * boxW;
        int by = y + row * (boxH + 2);
        opt[i].setCoords(x, by, boxW - 2, boxH);

        omarchy::frame(x, by, boxW - 2, boxH, 1, 110); // Faint Omarchy border

        String name = opt[i].name;
        if (static_cast<int>(name.length()) > maxNameChars) name = name.substring(0, maxNameChars);
        tft->setTextSize(_fm);
        tft->setTextColor(ALCOLOR, BGCOLOR);
        tft->drawCentreString(name, x + (boxW - 2) / 2, by + boxH / 2 - (LH * _fm) / 2, 1);

        if (i < 10) { // Only the first 10 apps have a keyboard digit shortcut (1..9,0)
            String shortcutLabel = (i == 9) ? "0" : String(i + 1);
            tft->setTextSize(_fp);
            tft->setTextColor(FGCOLOR, BGCOLOR);
            tft->drawString(shortcutLabel, x + 2, by + 1);
        }
    }

    int rows = (size + maxCols - 1) / maxCols;
    return y + rows * (boxH + 2);
}
void drawDeviceBorder() {
    // Omarchy: the top bar, and the screen below it as a focused Hyprland window.
    omarchy::bar();
    const int winY = omarchy::kBarHeight + 1;
    omarchy::frame(1, winY, tftWidth - 2, tftHeight - winY - 1);
}

// Left edge of the status icons at the right end of the top bar, as in Omarchy's bar.
static int barStatusLeft(bool hasBattery) { return tftWidth - 4 - (hasBattery ? 4 * LW * _fp + 3 + 16 : 0); }

void drawWifiStatus(bool hasBattery) {
    const int x = barStatusLeft(hasBattery) - 4 - 8;
    tft->fillRect(x, 0, 8, omarchy::kBarHeight, BGCOLOR);
    omarchy::wifiIcon(x, (omarchy::kBarHeight - 8) / 2 + 1, ALCOLOR);
}

void drawBatteryStatus(uint8_t bat, bool charging) {
    const int gw = 14;
    const int gh = 7;
    const int gx = tftWidth - 4 - 2 - gw;
    const int gy = (omarchy::kBarHeight - gh) / 2 + 1;
    const int left = barStatusLeft(true);
    const uint16_t color = charging ? FGCOLOR : ALCOLOR;
    tft->fillRect(left, 0, tftWidth - left, omarchy::kBarHeight, BGCOLOR);
    tft->drawRect(gx, gy, gw, gh, ALCOLOR);
    tft->fillRect(gx + gw, gy + 2, 2, gh - 4, ALCOLOR); // Battery tip
    tft->fillRect(gx + 2, gy + 2, (gw - 4) * bat / 100, gh - 4, color);
    tft->setTextSize(_fp);
    tft->setTextColor(color, BGCOLOR);
    tft->drawRightString(String(bat) + "%", gx - 3, gy, 1);
}

/*********************************************************************
**  Function: loopOptions
**  Where you choose among the options in menu
**********************************************************************/
int loopOptions(std::vector<Option> &options, bool bright, uint16_t al, uint16_t bg, bool border, int index) {
    bool redraw = true;
    bool exit = false;
    bool firstDraw = true;
#if defined(HAS_TOUCH)
    bool escRequested = false; // set only by the explicit [ESC] label (touch, border==false)
#endif
    log_i("Number of options: %d", options.size());
    int numOpt = options.size() - 1;
    Opt_Coord coord;
    std::vector<MenuOptions> list;
    int max_idx = 0;
    int min_idx = 255;
    LongPressTmp = launcherMillis();
    while (1) {
        if (omarchyScreensaver()) {
            firstDraw = true; // The screensaver cleared the screen: redraw all of it.
            redraw = true;
        }
        if (redraw) {
            list = {};
            bool wasFirstDraw = firstDraw;
            coord = drawOptions(index, options, list, al, bg, border, firstDraw);
            firstDraw = false;
#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
            M5.Display.setEpdMode(epd_mode_t::epd_text);
#endif
            max_idx = 0;
            min_idx = MAXFILES;
            int tmp = 0;
            for (auto item : list) {
                if (item.name != "") {
                    tmp = item.name.toInt();
                    // Serial.print(tmp); //Serial.print(" ");
                    if (tmp > max_idx) max_idx = tmp;
                    if (tmp < min_idx) min_idx = tmp;
                }
            }
            if (bright) { setBrightness(100 * (numOpt - index) / numOpt, false); }

            redraw = false;
        }
        if (index >= 0 && index < static_cast<int>(options.size())) {
            String txt = options[index].label;
            displayScrollingText(txt, coord);
        }

#if defined(HAS_ENCODER) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
#if defined(HAS_TOUCH)
        if (border == false) EscPress = false;
        if (touchPoint.pressed) {
            for (auto item : list) {
                if (item.contain(touchPoint.x, touchPoint.y)) {
                    resetGlobals();
                    if (item.name == "") {
                        if (item.text == "ESC") {
                            escRequested = true;
                        } else {
                            if (item.text == "+") index = max_idx + 1;
                            if (item.text == "-") index = min_idx - 1;
                            if (index < 0) index = 0;
                            // Serial.printf("\nPressed [%s], next index: %d\n",item.text,index);
                            redraw = true;
                        }
                        break;
                    } else {
                        if (index == item.name.toInt()) SelPress = true;
                        else redraw = true;
                        index = item.name.toInt();
                        break;
                    }
                }
            }
            touchPoint.pressed = false;
        }
#endif
        if (check(PrevPress) || check(UpPress)) {
            if (index == 0) index = options.size() - 1;
            else if (index > 0) index--;
            redraw = true;
        }
#else
        if (LongPress || PrevPress) {
            if (!LongPress) {
                LongPress = true;
                LongPressTmp = launcherMillis();
            }
            if (LongPress && launcherMillis() - LongPressTmp < 700) {
                if (!PrevPress) {
                    AnyKeyPress = false;
                    if (index == 0) index = options.size() - 1;
                    else if (index > 0) index--;
                    LongPress = false;
                    redraw = true;
                }
                if (launcherMillis() - LongPressTmp > 200)
                    tft->drawArc(
                        tftWidth / 2,
                        tftHeight / 2,
                        25,
                        15,
                        0,
                        360 * (launcherMillis() - (LongPressTmp + 200)) / 500,
                        FGCOLOR - 0x1111,
                        BGCOLOR
                    );
                if (launcherMillis() - LongPressTmp > 700) { // longpress detected to exit
                    LongPress = false;
                    check(PrevPress);
                    exit = true;
                    break;
                } else goto WAITING;
            }
        }
#if defined(HAS_5_BUTTONS) || defined(USE_CARDKB2)
        if (check(UpPress)) {
            if (index == 0) index = options.size() - 1;
            else if (index > 0) index--;
            redraw = true;
        }
#endif
#endif
    WAITING:
        /* DW Btn to next item */
        if (check(NextPress) || check(DownPress)) {
            index++;
            if ((index + 1) > options.size()) index = 0;
            redraw = true;
        }

        /* Select and run function */
        if (check(SelPress)) {
            drawOptionsErase(coord);
            options[index].operation();
            break;
        }

#if defined(HAS_TOUCH)
        if (border == false) {
            if (escRequested || returnToMenu || exit) return -1;
            EscPress = false; // swallow any stray heat-map ESC over the list rows
        } else {
            if (check(EscPress) || returnToMenu || exit) return -1;
        }
#else
        if (check(EscPress) || returnToMenu || exit) return -1;
#endif
    }

#if defined(E_PAPER_DISPLAY) && defined(USE_M5GFX)
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
#endif
    return index;
}

/*********************************************************************
**  Function: loopVersions
**  Where you choose which version to install/download **
**********************************************************************/
void loopVersions(const String &_fid) {
    JsonDocument item = getVersionInfo(_fid);
    if (item.isNull()) { return; }
    int versionIndex = 0;
    const char *name = item["name"];
    const char *author = item["author"];
    const char *fid = item["fid"];
    const bool star = item["star"].as<bool>();
    JsonArray versions = item["versions"];
    bool redraw = true;

    LongPressTmp = launcherMillis();
    resetGlobals();
    while (1) {
        if (returnToMenu) break; // Stops the loop to get back to Main menu

        JsonObject Version = versions[versionIndex];
        const char *version = Version["version"];
        const char *published_at = Version["published_at"];
        const char *file = Version["file"];
        if (redraw) {
            displayCurrentVersion(
                String(name), String(author), String(version), String(published_at), versionIndex, versions
            );
            redraw = false;
            tft->display(false);
            resetGlobals();
        }
        /* DW Btn to next item */
        if (check(NextPress)) {
            versionIndex++;
            if (versionIndex > versions.size() - 1) versionIndex = 0;
            redraw = true;
        }

        /* UP Btn go back to FW menu and ´<´ go to previous version item */

#if defined(HAS_ENCODER) || defined(HAS_TOUCH) || defined(HAS_KEYBOARD)
        /* UP Btn go to previous item */
        if (check(PrevPress)) {
            versionIndex--;
            if (versionIndex < 0) versionIndex = versions.size() - 1;
            redraw = true;
        }

#else // Esc logic is holding previous btn fot 1 second +-
        if (LongPress || PrevPress) {
            if (!LongPress) {
                LongPress = true;
                LongPressTmp = launcherMillis();
            }
            if (LongPress && launcherMillis() - LongPressTmp < 800) {
            WAITING:
                vTaskDelay(10 / portTICK_PERIOD_MS);
                if (!PrevPress && launcherMillis() - LongPressTmp < 200) {
                    AnyKeyPress = false;
                    if (versionIndex == 0) versionIndex = versions.size() - 1;
                    else if (versionIndex > 0) versionIndex--;
                    LongPress = false;
                    redraw = true;
                }
                if (!PrevPress && launcherMillis() - LongPressTmp > 200) {
                    check(PrevPress);
                    redraw = true;
                    LongPress = false;
                    goto EXIT_CHECK;
                }
                if (launcherMillis() - LongPressTmp > 200)
                    tft->drawArc(
                        tftWidth / 2,
                        tftHeight / 2,
                        25,
                        15,
                        0,
                        360 * (launcherMillis() - (LongPressTmp + 200)) / 500,
                        FGCOLOR - 0x1111,
                        BGCOLOR
                    );
                if (launcherMillis() - LongPressTmp > 700) { // longpress detected to exit
                    returnToMenu = true;
                    check(PrevPress);
                    goto SAIR;
                } else goto WAITING;
            }
        EXIT_CHECK:
            yield();
        }

#endif
        if (check(EscPress)) { goto SAIR; }

        /* Select to install */
        if (check(SelPress)) {

            // Definição da matriz "Options"
            options = {
                {"OTA Install", [=]() {
                     installFirmwareFromManifest(
                         String(fid), String(version), String(name) + " - " + String(version)
                     );
                 }}
            };
            if (sdcardMounted) {
                options.push_back({"Download->SD", [=]() {
                                       downloadFirmware(
                                           String(fid),
                                           String(file),
                                           String(name) + "." + String(version).substring(0, 10),
                                           dwn_path,
                                           String(version)
                                       );
                                   }});
                options.push_back({"Add to Favorite", [=] {
                                       JsonObject fav = favorite.add<JsonObject>();
                                       fav["name"] =
                                           String(name) + " - " + String(author) + " (" + ota_tag + ")";
                                       fav["fid"] = _fid;
                                       fav["link"] = "";
                                       saveConfigs();
                                   }});
            }
            options.push_back({"Back to List", [=]() { returnToMenu = true; }});

            loopOptions(options);
            // On fail installing will run the following line
            redraw = true;
        }
    }

SAIR:
#ifndef E_PAPER_DISPLAY
    tft->fillScreen(BGCOLOR);
#endif
    returnToMenu = false;
}

/*********************************************************************
**  Function: loopFirmware
**  Where you choose which Firmware to see more data
**********************************************************************/
void loopFirmware(bool isUpdate) {
    int _page = current_page;
    String order_by = "downloads";
    String query = "";
    bool star = false;
    bool refine = false;
    bool refined = false;
    int index = 0;

RESTART:
    currentIndex = -1;
    if (isUpdate && index > 0) {
        checkForUpdates();
    } else if (_page != current_page || refined) {
        GetJsonFromLauncherHub(current_page, order_by, star, query);
        index = 1;
    }
    options = {};
    int items = doc["page_size"].as<int>();
    int page = doc["page"].as<int>();
    if (total_firmware < (page * items)) {
        if (page == 1) items = total_firmware;
        else items = total_firmware - items * (page - 1);
    }
    options.push_back({"[Refine Search]", [&]() { refine = true; }, ALCOLOR});

    if (sdcardMounted && !doc["items"][0]["file"].as<String>().isEmpty() && isUpdate) {
        options.push_back(
            {"[Update all]",
             [=]() {
                 int count = (int)doc["items"].size();
                 for (int i = 0; i < count; i++) {
                     String fid = doc["items"][i]["fid"].as<String>();
                     String file = doc["items"][i]["file"].as<String>();
                     String name = doc["items"][i]["name"].as<String>();
                     String ver = doc["items"][i]["version"].as<String>();
                     downloadFirmware(fid, file, name + "." + ver.substring(0, 10), dwn_path, ver, true);
                     if (returnToMenu) break;
                 }
             },
             ALCOLOR}
        );
    }

    if (current_page > 1) {
        // Volta uma página
        options.push_back({"[Previous Page]", [=]() { current_page -= 1; }, ALCOLOR});
    }
    for (int i = 0; i < items; i++) {
        bool stared = doc["items"][i]["star"].as<bool>();
        String txt =
            doc["items"][i]["name"].as<String>() + " (" + doc["items"][i]["author"].as<String>() + ")";
        options.push_back({txt, [=]() { currentIndex = i; }, stared ? FGCOLOR - 0x1111 : FGCOLOR});
    };
    if (total_firmware > doc["page_size"].as<int>() * current_page) {
        // Avança uma pagina
        options.push_back({"[Next Page]", [=]() { current_page += 1; }, ALCOLOR});
    }
    options.push_back({"[Main Menu]", [=]() { returnToMenu = true; }, ALCOLOR});

    tft->fillScreen(BGCOLOR);
    resetGlobals();
    index = loopOptions(options, false, FGCOLOR, BGCOLOR, false, index);
    if (currentIndex >= 0) loopVersions(doc["items"][currentIndex]["fid"].as<String>());
    if (refine) {
        refine = false;
        std::vector<Option> opt = {
            {"Order by downloads",
             [&]() {
                 order_by = "downloads";
                 refined = true;
             }, order_by == "downloads" ? FGCOLOR : NO_COLOR},
            {"Order by name",
             [&]() {
                 order_by = "name";
                 refined = true;
             }, order_by == "name" ? FGCOLOR : NO_COLOR},
            {"Order by latest",
             [&]() {
                 order_by = "date";
                 refined = true;
             }, order_by == "date" ? FGCOLOR : NO_COLOR},
            {star == true ? "[x] Starred Only" : "[ ] Starred Only",
             [&]() {
                 star = !star;
                 refined = true;
             }},
            {"Text Search",
             [&]() {
                 String _q = keyboard(query, 76, "Search Firmware");
                 if (_q != String(KEY_ESCAPE)) {
                     query = _q;
                     refined = true;
                 }
             }},
            {"Back to list", [&]() { yield(); }}
        };
        resetGlobals();
        loopOptions(opt);
    }
    if (!returnToMenu && index >= 0) goto RESTART;
    doc.clear();
    RAM_LOG("firmwareList-doc-cleared");
}

/*********************************************************************
**  Function: tftprintln
**  similar to tft->println(), but allows to include margin
**********************************************************************/
void tftprintln(const String &txt, int margin, int numlines) {
    String rem = txt; // working copy: consumed line by line below
    int size = rem.length();
    if (numlines == 0) numlines = (tftHeight - 2 * margin) / (tft->getTextSize() * 8);
    int nchars = (tftWidth - 2 * margin) / (6 * tft->getTextSize()); // 6 pixels of width fot a letter size 1
    int x = tft->getCursorX();
    int start = 0;
    while (size > 0 && numlines > 0) {
        if (tft->getCursorX() < margin) tft->setCursor(margin, tft->getCursorY());
        nchars = (tftWidth - tft->getCursorX() - margin) /
                 (6 * tft->getTextSize()); // 6 pixels of width fot a letter size 1
        tft->println(rem.substring(0, nchars));
        rem = rem.substring(nchars);
        size -= nchars;
        numlines--;
    }
}
/*********************************************************************
**  Function: tftprintln
**  similar to tft->println(), but allows to include margin
**********************************************************************/
void tftprint(const String &txt, int margin, int numlines) {
    String rem = txt; // working copy: consumed line by line below
    int size = rem.length();
    if (numlines == 0) numlines = (tftHeight - 2 * margin) / (tft->getTextSize() * 8);
    int nchars = (tftWidth - 2 * margin) / (6 * tft->getTextSize()); // 6 pixels of width fot a letter size 1
    int x = tft->getCursorX();
    int start = 0;
    bool prim = true;
    while (size > 0 && numlines > 0) {
        if (!prim) { tft->println(); }
        if (tft->getCursorX() < margin) tft->setCursor(margin, tft->getCursorY());
        nchars = (tftWidth - tft->getCursorX() - margin) /
                 (6 * tft->getTextSize()); // 6 pixels of width fot a letter size 1
        tft->print(rem.substring(0, nchars));
        rem = rem.substring(nchars);
        size -= nchars;
        numlines--;
        prim = false;
    }
}

/***************************************************************************************
** Function name: getComplementaryColor
** Description:   Get simple complementary color in RGB565 format
***************************************************************************************/
uint16_t getComplementaryColor(uint16_t color) {
    int r = 31 - ((color >> 11) & 0x1F);
    int g = 63 - ((color >> 5) & 0x3F);
    int b = 31 - (color & 0x1F);
    return (r << 11) | (g << 5) | b;
}
