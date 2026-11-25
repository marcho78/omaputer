// The Launcher's Grok voice chat: Grok's face (or the words of the conversation) on screen
// while groksession talks to xAI's realtime voice API.
#if defined(OMARCHY_GROK)

#include "omarchy_grok.h"

#include "display.h"
#include "face_layout.h"
#include "grok_audio.h"
#include "grok_face.h"
#include "grok_session.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "mykeyboard.h"
#include "nvs_helpers.h"
#include "onlineLauncher.h"
#include "partitioner.h"
#include <Keyboard_def.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <globals.h>
#include <memory>
#include <new>
#include <vector>

extern bool UseTCA8418; // boards/m5stack-cardputer/interface.cpp: true on the Cardputer-Adv.

namespace {

using groksession::Phase;

constexpr const char *kKeyNamespace = "launcher"; // Erased by Settings > Reset Configs/Wifi.
constexpr const char *kKeyName = "xai_key";
constexpr const char *kDuoKey = "grok_duo";
constexpr const char *kNameKey = "grok_name";
constexpr const char *kPersonaKey = "grok_persona";
constexpr const char *kMutedKey = "grok_muted";
constexpr const char *kVoiceKey = "grok_voice";
constexpr const char *kVolumeKey = "grok_volume";
// Voices the realtime API accepted when tested; Ara is xAI's default.
const char *const kVoices[] = {"ara",   "eve",   "leo",    "rex",     "sal",   "altair", "atlas",
                               "carina", "castor", "celeste", "cosmo",  "helios", "helix", "iris",
                               "kepler", "lumen", "luna",   "lux",     "naksh", "orion", "perseus",
                               "rigel", "sirius", "ursa",   "zagan",   "zenith"};

String voiceName(const String &id) {
    if (id.isEmpty()) return "Ara";
    String name = id;
    name.setCharAt(0, static_cast<char>(toupper(name[0])));
    return name;
}
constexpr uint32_t kFrameMs = 33;
constexpr int kBandRows = 24; // The face is drawn in strips to save RAM.
// The session takes about 100 KB while it runs (TLS, 24 KB audio ring, buffers, task stack).
constexpr unsigned kMinFreeHeap = 120000;
constexpr uint16_t kWhite = 0xFFFF, kBlack = 0x0000, kGrey = 0x8410;
constexpr int kCharW = 6, kCharH = 8; // The built-in font, at text size 1.
constexpr int kWordsSize = 2;
constexpr int kWordsCols = grokface::kScreenWidth / (kCharW * kWordsSize);
constexpr int kWordsLineH = kCharH * kWordsSize + 1;
constexpr int kWordsRows = (grokface::kStatusTop - 2) / kWordsLineH;
constexpr int kVolumeStep = 25; // Percent.

int volume = 200; // Percent: Grok's voice twice as loud as it arrives, for the small speaker.

enum class View { Face, Words };
enum class Shown { Nothing, Face, Words, Message };

struct Screen {
    View view = View::Face;
    Shown shown = Shown::Nothing;
    String status;
    String message;
    uint32_t wordsVersion = UINT32_MAX;
    grokface::Pose pose;
    float level = 0;
    std::unique_ptr<uint16_t[]> band;
};

// Splits text into lines of at most cols characters, at spaces where possible.
void wrap(const String &text, int cols, std::vector<String> &lines) {
    String line;
    int start = 0;
    const int n = text.length();
    while (start < n) {
        int end = start;
        while (end < n && text[end] != ' ' && text[end] != '\n') ++end;
        String word = text.substring(start, end);
        while (static_cast<int>(word.length()) > cols) { // A word longer than a line.
            if (!line.isEmpty()) lines.push_back(line);
            line = "";
            lines.push_back(word.substring(0, cols));
            word = word.substring(cols);
        }
        if (!word.isEmpty()) {
            if (line.isEmpty()) line = word;
            else if (static_cast<int>(line.length() + 1 + word.length()) <= cols) line += " " + word;
            else {
                lines.push_back(line);
                line = word;
            }
        }
        if (end < n && text[end] == '\n') {
            lines.push_back(line);
            line = "";
        }
        start = end + 1;
    }
    if (!line.isEmpty()) lines.push_back(line);
}

void drawText(const String &text, int x, int y, int size, uint16_t colour) {
    tft->setTextDatum(TL_DATUM);
    tft->setTextSize(size);
    tft->setTextColor(colour, kWhite, true);
    tft->drawString(text, x, y);
}

void drawCentred(const String &text, int y, int size, uint16_t colour) {
    const int width = static_cast<int>(text.length()) * kCharW * size;
    drawText(text, std::max(0, (grokface::kScreenWidth - width) / 2), y, size, colour);
}

void drawStatus(Screen &s, const String &status, bool force) {
    if (!force && status == s.status) return;
    s.status = status;
    tft->fillRect(
        0, grokface::kStatusTop, grokface::kScreenWidth, grokface::kScreenHeight - grokface::kStatusTop, kWhite
    );
    drawCentred(status, grokface::kStatusTop + 4, 1, kGrey);
}

void drawFace(Screen &s, grokface::Mood mood, const String &status) {
    const grokface::Style style = grokface::screen_style();
    const bool fresh = s.shown != Shown::Face;
    if (fresh) {
        tft->fillScreen(kWhite);
        s.shown = Shown::Face;
    }
    // Loudness jumps up at once and falls away gently, like a level meter.
    const float loud = grokaudio::loudness();
    s.level = loud > s.level ? loud : s.level * 0.85f;
    const grokface::Pose target = grokface::animate(mood, launcherMillis(), s.level);
    s.pose = fresh ? target : grokface::ease(s.pose, target, 0.35f);
    for (int y0 = 0; y0 < style.region_h; y0 += kBandRows) {
        grokface::Style band = style;
        band.region_y += y0;
        band.region_h = std::min(kBandRows, style.region_h - y0);
        grokface::render(s.band.get(), band, s.pose);
        tft->native()->draw16bitBeRGBBitmap(band.region_x, band.region_y, s.band.get(), band.region_w, band.region_h);
    }
    drawStatus(s, status, fresh);
}

void drawWords(Screen &s, const String &status) {
    String you, reply;
    uint32_t version = 0;
    groksession::transcript(you, reply, version);
    const bool fresh = s.shown != Shown::Words;
    if (fresh) {
        tft->fillScreen(kWhite);
        s.shown = Shown::Words;
    }
    if (fresh || version != s.wordsVersion) {
        s.wordsVersion = version;
        // Your words in grey, Grok's in black; the newest lines are kept when they overflow.
        std::vector<String> youLines, replyLines;
        if (!you.isEmpty()) wrap("You: " + you, kWordsCols, youLines);
        if (!reply.isEmpty()) wrap(reply, kWordsCols, replyLines);
        std::vector<std::pair<String, uint16_t>> lines;
        for (const String &l : youLines) lines.push_back({l, kGrey});
        for (const String &l : replyLines) lines.push_back({l, kBlack});
        if (lines.empty()) lines.push_back({"Say something", kGrey});
        const size_t first = lines.size() > static_cast<size_t>(kWordsRows) ? lines.size() - kWordsRows : 0;
        for (int row = 0; row < kWordsRows; ++row) {
            const size_t i = first + row;
            String text = i < lines.size() ? lines[i].first : String();
            while (static_cast<int>(text.length()) < kWordsCols) text += ' '; // Paints over the old line.
            drawText(text, 0, 2 + row * kWordsLineH, kWordsSize, i < lines.size() ? lines[i].second : kBlack);
        }
    }
    drawStatus(s, status, fresh);
}

void drawMessage(Screen &s, const String &title, const String &body, const String &hint) {
    const String key = title + "\n" + body + "\n" + hint;
    if (s.shown == Shown::Message && key == s.message) return;
    s.shown = Shown::Message;
    s.message = key;
    tft->fillScreen(kWhite);
    drawCentred(title, 6, 2, kBlack);
    std::vector<String> lines;
    wrap(body, grokface::kScreenWidth / kCharW, lines);
    int y = 34;
    for (const String &line : lines) {
        if (y > grokface::kStatusTop - 12) break;
        drawText(line, 0, y, 1, kBlack);
        y += kCharH + 2;
    }
    drawCentred(hint, grokface::kStatusTop + 4, 1, kGrey);
    s.status = "";
}

grokface::Mood moodFor(Phase p) {
    switch (p) {
        case Phase::Connecting: return grokface::Mood::Connecting;
        case Phase::Listening: return grokface::Mood::Idle;
        case Phase::Hearing: return grokface::Mood::Hearing;
        case Phase::Thinking: return grokface::Mood::Thinking;
        case Phase::Speaking: return grokface::Mood::Talking;
        default: return grokface::Mood::Asleep;
    }
}

const char *statusFor(Phase p) {
    switch (p) {
        case Phase::Connecting: return "Connecting to Grok...";
        case Phase::Listening: return "Listening";
        case Phase::Hearing: return "Hearing you";
        case Phase::Thinking: return "Thinking...";
        case Phase::Speaking: return "Talking - any key stops it";
        case Phase::Asleep: return "Asleep - press any key";
        default: return "";
    }
}

struct Keys {
    bool exit = false, toggle = false, up = false, down = false, mute = false, type = false, other = false;
};

bool loadBool(const char *key) {
    lnvs::Handle handle(kKeyNamespace, false);
    bool value = false;
    return handle && lnvs::getBool(handle.raw(), key, value) && value;
}

void saveBool(const char *key, bool value) {
    lnvs::Handle handle(kKeyNamespace, true);
    if (handle && lnvs::setBool(handle.raw(), key, value)) handle.commit();
}

// The last volume set with ; and ., kept across restarts.
int loadVolume() {
    lnvs::Handle handle(kKeyNamespace, false);
    int value = 0;
    if (!handle || !lnvs::getInt(handle.raw(), kVolumeKey, value)) return volume;
    return std::clamp(value, 0, grokaudio::kMaxVolume);
}

void saveVolume(int value) {
    lnvs::Handle handle(kKeyNamespace, true);
    if (handle && lnvs::setInt(handle.raw(), kVolumeKey, value)) handle.commit();
}

// What the user set under Settings > Grok.
groksession::Options loadOptions() {
    groksession::Options o;
    o.apiKey = lnvs::getString(kKeyNamespace, kKeyName);
    o.name = lnvs::getString(kKeyNamespace, kNameKey);
    o.personality = lnvs::getString(kKeyNamespace, kPersonaKey);
    o.voice = lnvs::getString(kKeyNamespace, kVoiceKey);
    o.duo = loadBool(kDuoKey);
    o.muted = loadBool(kMutedKey);
    return o;
}

Keys readKeys() {
    Keys k;
    const keyStroke key = _getKeyPress();
    const bool select = check(SelPress); // Enter, or the G0 button on the side.
    if (!key.pressed) {
        k.exit = select; // Select with no key pressed is the G0 button: back to the menu.
        return k;
    }
    const auto has = [](const auto &list, int value) {
        return std::find(list.begin(), list.end(), value) != list.end();
    };
    // Esc (the ` key, with or without Fn) and Backspace go back to the menu.
    if (key.exit_key || key.del || has(key.word, '`')) {
        k.exit = true;
        return k;
    }
    // The original Cardputer reports Tab among its HID keys; the Adv's reader types it.
    k.toggle = has(key.hid_keys, KEY_TAB) || (UseTCA8418 && has(key.word, KEY_TAB));
    k.up = has(key.word, ';');
    k.down = has(key.word, '.');
    k.mute = has(key.word, 'm') || has(key.word, 'M');
    k.type = has(key.word, 't') || has(key.word, 'T');
    k.other |= !k.toggle && !k.up && !k.down && !k.mute && !k.type;
    return k;
}

} // namespace

void omarchyGrok() {
    const groksession::Options options = loadOptions();
    if (options.apiKey.isEmpty()) {
        displayError("No xAI key: add it in Settings", true);
        return;
    }
    if (!ensureWifiConnected()) return;
    const unsigned freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    launcherConsolePrintf(
        "Grok: start, free heap %u, largest block %u\n", freeHeap,
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    );
    Screen s;
    if (freeHeap >= kMinFreeHeap) {
        s.band.reset(new (std::nothrow) uint16_t[grokface::screen_style().region_w * kBandRows]);
    }
    if (!s.band || !groksession::start(options)) {
        displayError("Not enough memory for Grok", true);
        return;
    }
    volume = loadVolume();
    grokaudio::setVolume(volume);
    launcherWaitForQuietInput(); // The Enter that opened Grok.

    uint32_t volumeNoteUntil = 0;
    for (;;) {
        const uint32_t frameStart = launcherMillis();
        const Keys k = readKeys();
        if (k.exit) break;
        if (k.toggle) {
            s.view = s.view == View::Face ? View::Words : View::Face;
            s.shown = Shown::Nothing;
        }
        if (k.mute) groksession::setMuted(!groksession::muted());
        if (k.type) {
            // A message for this Grok only; Grok keeps talking while the keyboard is up.
            String typed = keyboard("", 200, "Message to Grok:");
            typed.trim();
            if (typed != String(KEY_ESCAPE) && !typed.isEmpty()) groksession::say(typed);
            s.shown = Shown::Nothing;
        }
        if (k.up || k.down) {
            volume = std::clamp(volume + (k.up ? kVolumeStep : -kVolumeStep), 0, grokaudio::kMaxVolume);
            grokaudio::setVolume(volume);
            saveVolume(volume);
            volumeNoteUntil = launcherMillis() + 1500;
        }
        const Phase p = groksession::phase();
        if (k.other) {
            if (p == Phase::Thinking || p == Phase::Speaking) {
                groksession::interrupt();
            } else if (p == Phase::Asleep || p == Phase::Failed) {
                if (!launcherWifiIsConnected()) {
                    s.shown = Shown::Nothing; // The WiFi menus draw over this screen.
                    ensureWifiConnected();
                }
                groksession::wake();
            }
        }

        String status = statusFor(p);
        if (groksession::muted() && p != Phase::Speaking) status = "Muted - press M";
        if (static_cast<int32_t>(volumeNoteUntil - launcherMillis()) > 0) {
            status = "Volume " + String(volume) + "%";
        }
        if (p == Phase::Failed) {
            drawMessage(s, "Can't talk to Grok", groksession::failure(), "Any key: try again   Esc: back");
        } else if (s.view == View::Face) {
            drawFace(s, moodFor(p), status);
        } else {
            drawWords(s, status);
        }

        const uint32_t spent = launcherMillis() - frameStart;
        if (spent < kFrameMs) launcherDelayMs(kFrameMs - spent);
    }
    groksession::stop();
    launcherWaitForQuietInput(); // The ` that closed Grok, so the menu doesn't act on it.
    tft->fillScreen(BGCOLOR);
}

void omarchyGrokSettingsMenu() {
    // Shown again after each change, so the labels show the new values, until Back or Esc.
    for (bool done = false; !done;) {
        const groksession::Options o = loadOptions();
        const auto shortened = [](const String &s, unsigned max) {
            return s.length() <= max ? s : s.substring(0, max - 3) + "...";
        };
        // Typed text for a setting; an empty answer clears it.
        const auto edit = [](const char *key, const String &current, const char *prompt, int max) {
            String typed = keyboard(current, max, prompt);
            if (typed == String(KEY_ESCAPE)) return;
            typed.trim();
            lnvs::setString(kKeyNamespace, key, typed.c_str());
        };
        std::vector<Option> options = {
            {String("Duo mode: ") + (o.duo ? "On" : "Off"), [&]() { saveBool(kDuoKey, !o.duo); }},
            {"Name: " + shortened(o.name.isEmpty() ? String("Grok") : o.name, 16),
             [&]() { edit(kNameKey, o.name, "Grok's name (empty: Grok):", 24); }},
            {"Personality: " + shortened(o.personality.isEmpty() ? String("none") : o.personality, 12),
             [&]() { edit(kPersonaKey, o.personality, "Personality (empty: none):", 120); }},
            {"Voice: " + voiceName(o.voice),
             [&]() {
                 std::vector<Option> voices;
                 int current = 0;
                 for (const char *id : kVoices) {
                     if (o.voice == id || (o.voice.isEmpty() && String(id) == "ara")) current = voices.size();
                     voices.push_back({voiceName(id), [id]() { lnvs::setString(kKeyNamespace, kVoiceKey, id); }});
                 }
                 loopOptions(voices, false, ALCOLOR, BGCOLOR, true, current);
             }},
            {String("Start muted: ") + (o.muted ? "On" : "Off"), [&]() { saveBool(kMutedKey, !o.muted); }},
            {o.apiKey.isEmpty() ? "Add xAI key" : "Change xAI key",
             []() {
                 String typed = keyboard("", 128, "xAI API key:");
                 if (typed == String(KEY_ESCAPE)) return;
                 typed.trim();
                 if (typed.isEmpty()) return;
                 displayMsg(omarchyGrokSetKey(typed) ? "xAI key saved" : "Couldn't save the key", true);
             }},
        };
        if (!o.apiKey.isEmpty()) {
            options.push_back({"Remove xAI key", []() {
                                   if (!confirmAction("Remove the xAI key?")) return;
                                   displayMsg(omarchyGrokClearKey() ? "xAI key removed" : "Couldn't remove it", true);
                               }});
        }
        options.push_back({"Back", [&]() { done = true; }});
        if (loopOptions(options) < 0) done = true;
    }
}

bool omarchyGrokSetKey(const String &key) { return lnvs::setString(kKeyNamespace, kKeyName, key.c_str()); }

bool omarchyGrokClearKey() {
    lnvs::Handle handle(kKeyNamespace, true);
    if (!handle) return false;
    return lnvs::eraseKey(handle.raw(), kKeyName) && handle.commit();
}

void omarchyGrokSettingsJson(JsonObject out) {
    const groksession::Options o = loadOptions();
    out["duo"] = o.duo;
    out["name"] = o.name;
    out["persona"] = o.personality;
    out["voice"] = o.voice.isEmpty() ? String("ara") : o.voice;
    out["muted"] = o.muted;
    out["hasKey"] = !o.apiKey.isEmpty();
    JsonArray voices = out["voices"].to<JsonArray>();
    for (const char *id : kVoices) voices.add(id);
}

String omarchyGrokApplySettings(JsonObjectConst in) {
    if (in["duo"].is<bool>()) saveBool(kDuoKey, in["duo"].as<bool>());
    if (in["muted"].is<bool>()) saveBool(kMutedKey, in["muted"].as<bool>());
    // Typed text, trimmed and cut to what the menu's keyboard takes; empty clears it.
    const auto text = [&](const char *field, const char *key, unsigned max) {
        if (!in[field].is<const char *>()) return;
        String value = in[field].as<const char *>();
        value.trim();
        lnvs::setString(kKeyNamespace, key, value.substring(0, max).c_str());
    };
    text("name", kNameKey, 24);
    text("persona", kPersonaKey, 120);
    if (in["voice"].is<const char *>()) {
        const String voice = in["voice"].as<const char *>();
        const auto known = [&](const char *id) { return voice == id; };
        if (std::none_of(std::begin(kVoices), std::end(kVoices), known)) return "Unknown voice";
        lnvs::setString(kKeyNamespace, kVoiceKey, voice.c_str());
    }
    if (in["clearKey"].as<bool>() && !omarchyGrokClearKey()) return "Couldn't remove the xAI key";
    if (in["key"].is<const char *>()) {
        String key = in["key"].as<const char *>();
        key.trim();
        if (!key.isEmpty() && !omarchyGrokSetKey(key.substring(0, 128))) return "Couldn't save the xAI key";
    }
    return "";
}

#endif
