#ifndef OMARCHY_GROK_H
#define OMARCHY_GROK_H

#if defined(OMARCHY_GROK)

#include <Arduino.h>
#include <ArduinoJson.h>

// Talk to Grok out loud: the Launcher menu's Grok item. Uses the Launcher's WiFi and the
// xAI API key saved below. Tab switches between Grok's face and the words, ; and . change
// the volume, M mutes the microphone, T types a message to this Grok only, Esc (`),
// Backspace or the G0 button go back to the menu, and any other key stops Grok mid-reply or
// reconnects.
void omarchyGrok();

// Settings > Grok: Duo mode (talking with another Grok), its name and personality, starting
// muted, and the xAI API key.
void omarchyGrokSettingsMenu();

// The key lives in the Launcher's own settings memory, so Settings > Reset Configs/Wifi
// erases it too. For the menu above and the serial console.
bool omarchyGrokSetKey(const String &key);
bool omarchyGrokClearKey();
// The saved key, or empty: for the web portal's chat, which asks xAI through the Cardputer so
// the key never reaches the browser.
String omarchyGrokKey();

// For the WebUI's Settings page: what Settings > Grok shows (whether an xAI key is saved,
// never the key itself) and the voices to pick from.
void omarchyGrokSettingsJson(JsonObject out);
// Changes the settings present in `in` (duo, name, persona, voice, muted, key, clearKey) the
// way Settings > Grok does. Returns what went wrong, or an empty string when all were saved.
String omarchyGrokApplySettings(JsonObjectConst in);

#endif
#endif
