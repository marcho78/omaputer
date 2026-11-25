#ifndef GROK_SESSION_H
#define GROK_SESSION_H

#if defined(OMARCHY_GROK)

#include <Arduino.h>

// The conversation with Grok, run by its own task while the Grok screen is open: opens a
// realtime voice session with xAI, streams the microphone to Grok and plays its replies.
// The screen asks it what is happening and passes on key presses. WiFi must already be up.
namespace groksession {

enum class Phase : uint8_t {
    Connecting, // Opening the session with Grok.
    Listening,  // Waiting for you to speak.
    Hearing,    // You are speaking.
    Thinking,   // You stopped; Grok's reply hasn't started playing yet.
    Speaking,   // Grok's reply is playing.
    Asleep,     // Hung up after 5 minutes with nobody talking.
    Failed,     // Something went wrong; failure() says what.
};

struct Options {
    String apiKey;
    String name;        // What this Grok calls itself; empty for plain "Grok".
    String personality; // Extra character, in the user's words; empty for none.
    String voice;       // An xAI voice id like "eve"; empty for xAI's default (Ara).
    bool duo = false;   // It shares the room with another Grok and talks with it.
    bool muted = false; // Start with the microphone muted.
};

bool start(const Options &options); // False if there isn't enough memory.
void stop();                        // Ends the session and frees everything it used.

void setMuted(bool muted); // While muted, nothing the microphone hears reaches Grok.
bool muted();

// A typed message for this Grok only (the other box doesn't hear it); Grok answers out loud.
void say(const String &text);

Phase phase();
String failure();

// What you said and what Grok is saying; version changes whenever either does.
void transcript(String &you, String &reply, uint32_t &version);

void interrupt(); // Stop Grok's reply.
void wake();      // Connect again after Asleep or Failed.

} // namespace groksession

#endif
#endif
