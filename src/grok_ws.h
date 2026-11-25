#ifndef GROK_WS_H
#define GROK_WS_H

#if defined(OMARCHY_GROK)

#include <Arduino.h>
#include <memory>

#include "ws_frame.h"

struct esp_tls;

// A WebSocket client over TLS (ESP-IDF esp-tls, already in the Launcher for HTTPS) for
// Grok's realtime voice API. Grok sends its voice in binary messages of up to 33 KB
// (measured), faster than real time; this client streams them to the caller only as fast
// as it has room, so TCP holds back the rest instead of the Cardputer's RAM.
class GrokWs {
public:
    class Handler {
    public:
        virtual ~Handler() = default;
        // A complete text message (not NUL-terminated).
        virtual void onText(const char *text, size_t len) = 0;
        // How many bytes of binary payload onBinary() can take now; 0 pauses reading.
        virtual size_t binaryRoom() = 0;
        virtual void onBinary(const uint8_t *data, size_t len) = 0;
    };

    static constexpr size_t kMaxTextMessage = 8192; // Grok's largest event seen: 718 bytes.

    ~GrokWs() { close(); }

    // Opens wss://host/path with this Authorization header value. On failure, error says why.
    bool connect(const char *host, const char *path, const String &authorization, String &error);

    // Sends a text message. Masks text in place, so its contents are scrambled afterwards.
    bool sendText(char *text, size_t len);

    // Reads what has arrived and passes it on. False once the connection is gone.
    bool poll(Handler &handler);

    void close();
    bool connected() const { return open_; }

private:
    int readable(uint32_t waitMs = 0);
    bool writeAll(const uint8_t *data, size_t len);
    bool readHandshake(const char *key, String &error);
    bool sendFrame(uint8_t opcode, uint8_t *payload, size_t len);
    bool beginFrame();
    void finishFrame(Handler &handler);
    void drop(const char *why);

    esp_tls *tls_ = nullptr;
    int fd_ = -1;
    bool open_ = false;

    enum class Stage { Header, Payload };
    Stage stage_ = Stage::Header;
    uint8_t header_[10] = {};
    size_t headerLen_ = 0;
    grokcore::WsHeader frame_;
    uint64_t remaining_ = 0;
    uint8_t messageOpcode_ = 0; // Text or binary while a message is in progress.

    std::unique_ptr<char[]> text_;
    size_t textLen_ = 0;
    bool textOverflow_ = false;
    uint8_t control_[125] = {};
    size_t controlLen_ = 0;
    uint8_t scratch_[1024] = {};
};

#endif
#endif
