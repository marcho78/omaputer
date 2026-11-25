#include "grok_session.h"

#if defined(OMARCHY_GROK)

#include "byte_ring.h"
#include "grok_audio.h"
#include "grok_ws.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "omarchy_clock.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/base64.h>
#include <memory>
#include <new>

namespace groksession {
namespace {

constexpr const char *kHost = "api.x.ai";
constexpr const char *kPath = "/v1/realtime?model=grok-voice-latest";
constexpr uint32_t kSessionTimeoutMs = 10000;
constexpr uint32_t kIdleTimeoutMs = 5 * 60 * 1000;
constexpr size_t kChunkSamples = grokaudio::kSampleRate / 10; // 100 ms, the size xAI recommends.
constexpr size_t kChunkBytes = kChunkSamples * sizeof(int16_t);
constexpr size_t kRingBytes = 24000;   // 0.75 s of Grok's voice between network and speaker.
constexpr size_t kPrerollBytes = 8000; // Start playing once 0.25 s is waiting.
constexpr uint32_t kTailMs = 150;      // After the last sound, before the microphone opens.
constexpr size_t kMaxReplyText = 1500; // Keep the end of long replies.
constexpr size_t kPlayBlock = 160;     // 10 ms.

// Microphone audio goes as base64 JSON (the API ignored raw binary microphone frames when
// tested); Grok's voice comes back as raw 16 kHz PCM in binary frames.
const char kDevice[] =
    ", talking out loud through a pocket-size handheld computer with a small speaker. Keep replies "
    "short and conversational unless asked for more. Use web search for news, current events, weather, "
    "sports, prices and anything else that may have changed recently.";
const char kDuo[] =
    " You share the room with another AI on another handheld device: you hear what it says through your "
    "microphone and it hears you. Keep the conversation going with it: answer what it just said in one "
    "or two sentences, add something of your own, and often ask it a question. When the person in the "
    "room talks or types to you, follow their lead.";
constexpr uint32_t kClockWaitMs = 2000; // Right after WiFi connects the clock may still be setting.
const char kCancel[] = R"({"type":"response.cancel"})";
const char kClear[] = R"({"type":"input_audio_buffer.clear"})";
const char kRespond[] = R"({"type":"response.create"})";
const char kAppendPrefix[] = R"({"type":"input_audio_buffer.append","audio":")";
const char kAppendSuffix[] = R"("})";
constexpr size_t kSendBytes = sizeof(kAppendPrefix) + (kChunkBytes + 2) / 3 * 4 + sizeof(kAppendSuffix) + 1;

// Shared with the screen.
std::atomic<Phase> phaseNow{Phase::Connecting};
std::atomic<bool> interruptWanted{false};
std::atomic<bool> wakeWanted{false};
std::atomic<bool> stopWanted{false};
std::atomic<bool> mutedNow{false};
std::atomic<bool> clearWanted{false}; // Drop audio already sent, after muting mid-sentence.
SemaphoreHandle_t textLock = nullptr; // Guards the strings below.
SemaphoreHandle_t finished = nullptr;
String failureText, youText, replyText;
String typedText; // A typed message waiting to be sent.
uint32_t textVersion = 0;

// Owned by the task while it runs; allocated by start() and freed by the task on exit.
struct State {
    Options options;
    GrokWs ws;
    grokcore::ByteRing ring{kRingBytes};
    char send[kSendBytes];
    int16_t mic[kChunkSamples];
    size_t micCount = 0;
    int16_t pending[kPlayBlock];
    size_t pendingCount = 0, pendingDone = 0;
    bool sessionReady = false, responseActive = false, responseAudioDone = false;
    bool discarding = false; // Dropping the rest of a reply that was stopped.
    bool speaking = false;
    uint32_t quietSinceMs = 0, lastActivityMs = 0;
};
State *st = nullptr;

void setPhase(Phase p) { phaseNow.store(p); }

void editText(void (*edit)(void *), void *arg) {
    xSemaphoreTake(textLock, portMAX_DELAY);
    edit(arg);
    ++textVersion;
    xSemaphoreGive(textLock);
}

void fail(const String &why) {
    launcherConsolePrintf("Grok: %s\n", why.c_str());
    st->ws.close();
    grokaudio::stop();
    st->speaking = false;
    String copy = why;
    editText([](void *p) { failureText = *static_cast<String *>(p); }, &copy);
    setPhase(Phase::Failed);
}

void backToListening() {
    if (!grokaudio::listen()) {
        fail("Can't start the microphone");
        return;
    }
    st->speaking = false;
    st->micCount = 0;
    st->quietSinceMs = 0;
    st->lastActivityMs = launcherMillis();
    setPhase(Phase::Listening);
}

bool sendJson(const char *json, size_t len) {
    if (len >= sizeof st->send) return false;
    memcpy(st->send, json, len); // sendText scrambles its buffer.
    return st->ws.sendText(st->send, len);
}

void sendMicChunk() {
    size_t used = sizeof(kAppendPrefix) - 1;
    memcpy(st->send, kAppendPrefix, used);
    size_t encoded = 0;
    mbedtls_base64_encode(
        reinterpret_cast<unsigned char *>(st->send + used), sizeof st->send - used, &encoded,
        reinterpret_cast<const unsigned char *>(st->mic), kChunkBytes
    );
    used += encoded;
    memcpy(st->send + used, kAppendSuffix, sizeof(kAppendSuffix) - 1);
    used += sizeof(kAppendSuffix) - 1;
    st->ws.sendText(st->send, used);
}

void handleEvent(const char *text, size_t len) {
    JsonDocument filter;
    filter["type"] = true;
    filter["delta"] = true;
    filter["transcript"] = true;
    filter["error"]["message"] = true;
    filter["response"]["status"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, text, len, DeserializationOption::Filter(filter))) return;
    const char *type = doc["type"] | "";

    if (!strcmp(type, "session.updated")) {
        st->sessionReady = true;
    } else if (!strcmp(type, "input_audio_buffer.speech_started")) {
        st->lastActivityMs = launcherMillis();
        if (!st->speaking) {
            // You spoke over a reply that hadn't started playing: drop what has arrived.
            st->ring.clear();
            st->pendingCount = st->pendingDone = 0;
            if (st->responseActive) st->discarding = true;
        }
        editText(
            [](void *) {
                youText = "";
                replyText = "";
            },
            nullptr
        );
        setPhase(Phase::Hearing);
    } else if (!strcmp(type, "input_audio_buffer.speech_stopped")) {
        st->lastActivityMs = launcherMillis();
        setPhase(Phase::Thinking);
    } else if (!strcmp(type, "conversation.item.input_audio_transcription.completed")) {
        String said = doc["transcript"] | "";
        editText([](void *p) { youText = *static_cast<String *>(p); }, &said);
    } else if (!strcmp(type, "response.created")) {
        st->responseActive = true;
        st->responseAudioDone = false;
        editText([](void *) { replyText = ""; }, nullptr);
        if (phaseNow.load() == Phase::Listening) setPhase(Phase::Thinking);
    } else if (!strcmp(type, "response.output_audio_transcript.delta")) {
        if (st->discarding) return;
        String delta = doc["delta"] | "";
        editText(
            [](void *p) {
                replyText += *static_cast<String *>(p);
                if (replyText.length() > kMaxReplyText) replyText.remove(0, replyText.length() - kMaxReplyText);
            },
            &delta
        );
    } else if (!strcmp(type, "response.output_audio.done")) {
        st->responseAudioDone = true;
    } else if (!strcmp(type, "response.done")) {
        launcherConsolePrintf("Grok: reply %s\n", doc["response"]["status"] | "?");
        st->responseActive = false;
        st->responseAudioDone = true;
        st->lastActivityMs = launcherMillis();
        if (st->discarding) {
            st->discarding = false;
            st->ring.clear();
        }
    } else if (!strcmp(type, "error")) {
        const char *message = doc["error"]["message"] | "unknown error";
        launcherConsolePrintf("Grok error: %s\n", message);
        if (!st->sessionReady) fail(String("Grok: ") + message);
    }
}

class Events : public GrokWs::Handler {
public:
    void onText(const char *text, size_t len) override { handleEvent(text, len); }
    size_t binaryRoom() override { return st->discarding ? 1024 : st->ring.room(); }
    void onBinary(const uint8_t *data, size_t len) override {
        if (!st->discarding) st->ring.write(data, len);
    }
};
Events events;

void openSession() {
    setPhase(Phase::Connecting);
    st->sessionReady = st->responseActive = st->responseAudioDone = st->discarding = st->speaking = false;
    st->ring.clear();
    st->pendingCount = st->pendingDone = 0;
    String error;
    if (!st->ws.connect(kHost, kPath, "Bearer " + st->options.apiKey, error)) return fail(error);

    // Who it is, how to talk on this device, and today's date when the clock is set (Grok
    // doesn't know it otherwise). Built with ArduinoJson so typed names are escaped.
    const Options &o = st->options;
    String instructions = o.name.isEmpty() ? String("You are Grok") : "You are " + o.name + ", a Grok voice assistant";
    instructions += kDevice;
    if (!o.personality.isEmpty()) instructions += " Your personality: " + o.personality + ".";
    if (o.duo) instructions += kDuo;
#if defined(OMARCHY_CLOCK)
    String now = omarchyClockDateTime();
    for (uint32_t waited = 0; now.isEmpty() && waited < kClockWaitMs; waited += 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
        now = omarchyClockDateTime();
    }
    if (!now.isEmpty()) instructions += " It is now " + now + " Pacific time.";
#endif
    JsonDocument doc;
    doc["type"] = "session.update";
    JsonObject s = doc["session"].to<JsonObject>();
    s["instructions"] = instructions;
    if (!o.voice.isEmpty()) s["voice"] = o.voice;
    s["tools"][0]["type"] = "web_search"; // Grok searches the web itself, on xAI's side.
    s["turn_detection"]["type"] = "server_vad";
    if (grokaudio::speechThreshold() > 0) s["turn_detection"]["threshold"] = grokaudio::speechThreshold();
    JsonObject in = s["audio"]["input"].to<JsonObject>();
    in["format"]["type"] = "audio/pcm";
    in["format"]["rate"] = grokaudio::kSampleRate;
    in["transport"] = "json";
    JsonObject out = s["audio"]["output"].to<JsonObject>();
    out["format"]["type"] = "audio/pcm";
    out["format"]["rate"] = grokaudio::kSampleRate;
    out["transport"] = "binary";
    String update;
    serializeJson(doc, update);
    if (!sendJson(update.c_str(), update.length())) return fail("Lost the connection to Grok");
    const uint32_t start = launcherMillis();
    while (!st->sessionReady && phaseNow.load() != Phase::Failed && !stopWanted.load()) {
        if (!st->ws.poll(events)) {
            // An error event may already have failed with Grok's own reason.
            if (phaseNow.load() != Phase::Failed) fail("Grok closed the connection");
            return;
        }
        if (launcherMillis() - start > kSessionTimeoutMs) return fail("Grok didn't answer");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (phaseNow.load() == Phase::Failed || stopWanted.load()) return;
    launcherConsolePrintf("Grok: session ready, free heap %u\n", (unsigned)ESP.getFreeHeap());
    backToListening();
}

void stopReply() {
    if (!st->responseActive && !st->speaking) return;
    if (st->responseActive) {
        sendJson(kCancel, sizeof(kCancel) - 1);
        st->discarding = true; // Until response.done.
    }
    st->ring.clear();
    st->pendingCount = st->pendingDone = 0;
    backToListening(); // Turning the speaker off also drops what it had queued.
}

void pumpMicrophone() {
    const Phase p = phaseNow.load();
    if (p != Phase::Listening && p != Phase::Hearing && p != Phase::Thinking) return;
    for (;;) {
        st->micCount += grokaudio::readMic(st->mic + st->micCount, kChunkSamples - st->micCount);
        if (st->micCount < kChunkSamples) return;
        if (!mutedNow.load()) sendMicChunk(); // Muted: read and dropped, never sent.
        st->micCount = 0;
    }
}

// Sends a typed message waiting from the screen as the user's turn, then asks for a reply.
void sendTyped() {
    xSemaphoreTake(textLock, portMAX_DELAY);
    String text = typedText;
    typedText = "";
    xSemaphoreGive(textLock);
    if (text.isEmpty()) return;
    stopReply(); // Typing over a reply replaces it.
    JsonDocument doc;
    doc["type"] = "conversation.item.create";
    JsonObject item = doc["item"].to<JsonObject>();
    item["type"] = "message";
    item["role"] = "user";
    JsonObject part = item["content"].add<JsonObject>();
    part["type"] = "input_text";
    part["text"] = text;
    String json;
    serializeJson(doc, json);
    if (!sendJson(json.c_str(), json.length()) || !sendJson(kRespond, sizeof(kRespond) - 1)) return;
    st->lastActivityMs = launcherMillis();
    editText(
        [](void *p) {
            youText = *static_cast<String *>(p);
            replyText = "";
        },
        &text
    );
    setPhase(Phase::Thinking);
}

// Moves Grok's voice from the ring to the speaker, and back to listening when it ends.
void playReply() {
    if (!st->speaking) {
        if (st->discarding || st->ring.size() == 0) {
            // A reply that ended without sound (stopped, or text only): listen again.
            if (phaseNow.load() == Phase::Thinking && !st->responseActive && st->responseAudioDone) backToListening();
            return;
        }
        if (st->ring.size() < kPrerollBytes && !st->responseAudioDone) return;
        if (!grokaudio::speak()) return fail("Can't start the speaker");
        st->speaking = true;
        setPhase(Phase::Speaking);
    }
    for (;;) {
        if (st->pendingDone == st->pendingCount) {
            const size_t whole = st->ring.size() & ~static_cast<size_t>(1);
            const size_t bytes = std::min(whole, sizeof st->pending);
            if (bytes == 0 || (bytes < sizeof st->pending && !st->responseAudioDone)) break;
            st->ring.read(reinterpret_cast<uint8_t *>(st->pending), bytes);
            st->pendingCount = bytes / sizeof(int16_t);
            st->pendingDone = 0;
        }
        const size_t taken = grokaudio::play(st->pending + st->pendingDone, st->pendingCount - st->pendingDone);
        st->pendingDone += taken;
        if (st->pendingDone < st->pendingCount) break; // The speaker is full for now.
    }
    const bool done = st->responseAudioDone && !st->responseActive && st->ring.size() < 2 &&
                      st->pendingDone == st->pendingCount && grokaudio::playedOut();
    if (!done) {
        st->quietSinceMs = 0;
    } else if (st->quietSinceMs == 0) {
        st->quietSinceMs = launcherMillis();
    } else if (launcherMillis() - st->quietSinceMs >= kTailMs) {
        st->ring.clear();
        backToListening();
    }
}

void goToSleep() {
    launcherConsolePrintln("Grok: nobody talked for 5 minutes, hanging up");
    st->ws.close();
    grokaudio::stop();
    setPhase(Phase::Asleep);
}

void task(void *) {
    while (!stopWanted.load()) {
        const Phase p = phaseNow.load();
        if (p == Phase::Asleep || p == Phase::Failed) {
            if (wakeWanted.exchange(false)) openSession();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!st->ws.connected()) {
            openSession();
            continue;
        }
        if (!launcherWifiIsConnected()) {
            fail("WiFi disconnected");
            continue;
        }
        if (interruptWanted.exchange(false)) stopReply();
        if (clearWanted.exchange(false)) sendJson(kClear, sizeof(kClear) - 1);
        sendTyped();
        pumpMicrophone();
        if (!st->ws.poll(events)) {
            launcherConsolePrintln("Grok: connection lost, reconnecting");
            grokaudio::stop();
            st->speaking = false;
            openSession();
            continue;
        }
        if (phaseNow.load() != Phase::Failed) playReply();
        if (phaseNow.load() == Phase::Listening && launcherMillis() - st->lastActivityMs > kIdleTimeoutMs) goToSleep();
        vTaskDelay(1);
    }
    st->ws.close();
    grokaudio::stop();
    delete st;
    st = nullptr;
    xSemaphoreGive(finished);
    vTaskDelete(nullptr);
}

} // namespace

bool start(const Options &options) {
    if (st) return true;
    if (!textLock) textLock = xSemaphoreCreateMutex();
    if (!finished) finished = xSemaphoreCreateBinary();
    st = new (std::nothrow) State();
    if (!st || !st->ring.ok()) {
        delete st;
        st = nullptr;
        return false;
    }
    st->options = options;
    failureText = youText = replyText = typedText = "";
    mutedNow.store(options.muted);
    clearWanted.store(false);
    stopWanted.store(false);
    interruptWanted.store(false);
    wakeWanted.store(false);
    setPhase(Phase::Connecting);
    // TLS needs a deep stack. Core 0, beside WiFi; the screen runs on core 1.
    if (xTaskCreatePinnedToCore(task, "grok", 12288, nullptr, 2, nullptr, 0) != pdPASS) {
        delete st;
        st = nullptr;
        return false;
    }
    return true;
}

void stop() {
    if (!st) return;
    stopWanted.store(true);
    xSemaphoreTake(finished, portMAX_DELAY);
}

Phase phase() { return phaseNow.load(); }

String failure() {
    xSemaphoreTake(textLock, portMAX_DELAY);
    String copy = failureText;
    xSemaphoreGive(textLock);
    return copy;
}

void transcript(String &you, String &reply, uint32_t &version) {
    xSemaphoreTake(textLock, portMAX_DELAY);
    you = youText;
    reply = replyText;
    version = textVersion;
    xSemaphoreGive(textLock);
}

void interrupt() { interruptWanted.store(true); }
void wake() { wakeWanted.store(true); }

void setMuted(bool on) {
    if (on && !mutedNow.load()) clearWanted.store(true); // Drop half a sentence already sent.
    mutedNow.store(on);
}

bool muted() { return mutedNow.load(); }

void say(const String &text) {
    xSemaphoreTake(textLock, portMAX_DELAY);
    typedText = text;
    xSemaphoreGive(textLock);
}

} // namespace groksession

#endif
