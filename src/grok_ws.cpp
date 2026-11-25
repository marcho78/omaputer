#include "grok_ws.h"

#if defined(OMARCHY_GROK)

#include "idf/ca_certs.h"
#include "idf/launcher_platform.h"
#include <algorithm>
#include <cstring>
#include <esp_random.h>
#include <esp_tls.h>
#include <lwip/sockets.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <new>

using namespace grokcore;

namespace {

constexpr int kConnectTimeoutMs = 15000;
constexpr uint32_t kHandshakeTimeoutMs = 10000;

// Sec-WebSocket-Accept for a key: base64(SHA-1(key + RFC 6455's GUID)).
String acceptFor(const char *key) {
    String text = String(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char digest[20];
    mbedtls_sha1(reinterpret_cast<const unsigned char *>(text.c_str()), text.length(), digest);
    unsigned char out[32];
    size_t len = 0;
    mbedtls_base64_encode(out, sizeof out, &len, digest, sizeof digest);
    return String(reinterpret_cast<const char *>(out), len);
}

String describeRefusal(int code) {
    switch (code) {
        case 401:
        case 403: return "Grok rejected the xAI key (HTTP " + String(code) + ")";
        case 429: return "Grok says too many requests, or no credit left (HTTP 429)";
        case 0: return "Grok sent an answer this can't read";
        default: return "Grok refused to connect (HTTP " + String(code) + ")";
    }
}

} // namespace

// Bytes that can be read without waiting: those esp-tls holds decrypted, else a full
// scratch buffer's worth once the socket has data (a read then returns what one TLS record
// holds), else 0. -1 on a socket error.
int GrokWs::readable(uint32_t waitMs) {
    const ssize_t buffered = esp_tls_get_bytes_avail(tls_);
    if (buffered > 0) return static_cast<int>(buffered);
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd_, &set);
    timeval tv = {static_cast<time_t>(waitMs / 1000), static_cast<suseconds_t>((waitMs % 1000) * 1000)};
    const int ready = select(fd_ + 1, &set, nullptr, nullptr, &tv);
    if (ready < 0) return -1;
    return ready > 0 ? static_cast<int>(sizeof scratch_) : 0;
}

bool GrokWs::writeAll(const uint8_t *data, size_t len) {
    while (len > 0) {
        const ssize_t n = esp_tls_conn_write(tls_, data, len);
        if (n <= 0) return false;
        data += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool GrokWs::connect(const char *host, const char *path, const String &authorization, String &error) {
    close();
    if (!text_) text_.reset(new (std::nothrow) char[kMaxTextMessage]);
    tls_ = text_ ? esp_tls_init() : nullptr;
    if (!tls_) {
        error = "Out of memory";
        return false;
    }
    esp_tls_cfg_t cfg = {};
    cfg.cacert_buf = reinterpret_cast<const unsigned char *>(kRootCAs); // Grok chains to GlobalSign Root CA.
    cfg.cacert_bytes = sizeof(kRootCAs);
    cfg.timeout_ms = kConnectTimeoutMs;
    if (esp_tls_conn_new_sync(host, strlen(host), 443, &cfg, tls_) != 1) {
        launcherConsolePrintf("Grok: TLS connect to %s failed\n", host);
        esp_tls_conn_destroy(tls_);
        tls_ = nullptr;
        error = String("Can't reach ") + host;
        return false;
    }
    esp_tls_get_conn_sockfd(tls_, &fd_);

    uint8_t nonce[16];
    esp_fill_random(nonce, sizeof nonce);
    char key[32];
    size_t keyLen = 0;
    mbedtls_base64_encode(reinterpret_cast<unsigned char *>(key), sizeof key, &keyLen, nonce, sizeof nonce);
    key[keyLen] = '\0';

    String request;
    request.reserve(256 + authorization.length());
    request += "GET ";
    request += path;
    request += " HTTP/1.1\r\nHost: ";
    request += host;
    request += "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ";
    request += key;
    request += "\r\nSec-WebSocket-Version: 13\r\nAuthorization: ";
    request += authorization;
    request += "\r\nUser-Agent: grok-cardputer-launcher\r\n\r\n";
    const bool sent = writeAll(reinterpret_cast<const uint8_t *>(request.c_str()), request.length());
    request = String(); // Holds the key; let it go now.
    if (!sent || !readHandshake(key, error)) {
        if (!sent) error = "Lost the connection while opening it";
        esp_tls_conn_destroy(tls_);
        tls_ = nullptr;
        return false;
    }
    open_ = true;
    stage_ = Stage::Header;
    headerLen_ = 0;
    messageOpcode_ = 0;
    return true;
}

bool GrokWs::readHandshake(const char *key, String &error) {
    const uint32_t deadline = launcherMillis() + kHandshakeTimeoutMs;
    // One byte at a time, so nothing after the headers is consumed.
    auto readLine = [&](String &line) {
        line = "";
        while (static_cast<int32_t>(deadline - launcherMillis()) > 0) {
            const int ready = readable(50);
            if (ready < 0) return false;
            if (ready == 0) continue;
            uint8_t c;
            if (esp_tls_conn_read(tls_, &c, 1) != 1) return false;
            if (c == '\n') {
                if (line.endsWith("\r")) line.remove(line.length() - 1);
                return true;
            }
            if (line.length() < 512) line += static_cast<char>(c);
        }
        return false;
    };

    String status;
    if (!readLine(status)) {
        error = "Grok didn't answer";
        return false;
    }
    const int code = status.startsWith("HTTP/1.1 ") ? status.substring(9, 12).toInt() : 0;
    String accept;
    for (;;) {
        String line;
        if (!readLine(line)) {
            error = "Grok's answer was cut off";
            return false;
        }
        if (line.isEmpty()) break;
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        String name = line.substring(0, colon);
        name.trim();
        name.toLowerCase();
        if (name == "sec-websocket-accept") {
            accept = line.substring(colon + 1);
            accept.trim();
        }
    }
    if (code != 101) {
        launcherConsolePrintf("Grok: handshake refused: %s\n", status.c_str());
        error = describeRefusal(code);
        return false;
    }
    if (accept != acceptFor(key)) {
        error = "Grok's answer didn't check out";
        return false;
    }
    return true;
}

bool GrokWs::sendText(char *text, size_t len) {
    return sendFrame(kWsText, reinterpret_cast<uint8_t *>(text), len);
}

bool GrokWs::sendFrame(uint8_t opcode, uint8_t *payload, size_t len) {
    if (!open_) return false;
    uint8_t mask[4];
    esp_fill_random(mask, sizeof mask);
    uint8_t header[kWsMaxClientHeader];
    const size_t headerLen = ws_client_header(header, opcode, len, mask);
    ws_mask(payload, len, mask);
    if (!writeAll(header, headerLen) || (len && !writeAll(payload, len))) {
        drop("write failed");
        return false;
    }
    return true;
}

bool GrokWs::poll(Handler &handler) {
    for (int step = 0; step < 48 && open_; ++step) {
        const int available = readable();
        if (available < 0) {
            drop("socket error");
            break;
        }
        if (available == 0) break;

        if (stage_ == Stage::Header) {
            uint8_t c;
            const ssize_t got = esp_tls_conn_read(tls_, &c, 1);
            if (got <= 0) {
                drop(got == 0 ? "closed by server" : "read failed");
                break;
            }
            header_[headerLen_++] = c;
            const int size = ws_parse_server_header(header_, headerLen_, frame_);
            if (size < 0) {
                drop("bad frame from server");
                break;
            }
            if (size == 0) continue;
            headerLen_ = 0;
            remaining_ = frame_.length;
            if (!beginFrame()) break;
            if (remaining_ == 0) finishFrame(handler);
            else stage_ = Stage::Payload;
            continue;
        }

        const uint8_t opcode = frame_.opcode == kWsContinuation ? messageOpcode_ : frame_.opcode;
        size_t want = static_cast<size_t>(std::min<uint64_t>(remaining_, static_cast<uint64_t>(available)));
        uint8_t *into = scratch_;
        if (opcode == kWsBinary) {
            want = std::min({want, handler.binaryRoom(), sizeof scratch_});
            if (want == 0) break; // No room: leave the rest in TCP until the audio plays.
        } else if (opcode == kWsText) {
            want = std::min(want, sizeof scratch_);
        } else {
            into = control_ + controlLen_;
        }
        const ssize_t got = esp_tls_conn_read(tls_, into, want);
        if (got <= 0) {
            drop(got == 0 ? "closed by server" : "read failed");
            break;
        }
        if (opcode == kWsBinary) {
            handler.onBinary(scratch_, static_cast<size_t>(got));
        } else if (opcode == kWsText) {
            if (!textOverflow_ && textLen_ + got <= kMaxTextMessage) {
                memcpy(text_.get() + textLen_, scratch_, static_cast<size_t>(got));
                textLen_ += static_cast<size_t>(got);
            } else {
                textOverflow_ = true;
            }
        } else {
            controlLen_ += static_cast<size_t>(got);
        }
        remaining_ -= static_cast<uint64_t>(got);
        if (remaining_ == 0) {
            finishFrame(handler);
            stage_ = Stage::Header;
        }
    }
    return open_;
}

bool GrokWs::beginFrame() {
    switch (frame_.opcode) {
        case kWsText:
        case kWsBinary:
            if (messageOpcode_ != 0) {
                drop("new message inside another");
                return false;
            }
            messageOpcode_ = frame_.opcode;
            textLen_ = 0;
            textOverflow_ = false;
            return true;
        case kWsContinuation:
            if (messageOpcode_ == 0) {
                drop("continuation without a message");
                return false;
            }
            return true;
        case kWsClose:
        case kWsPing:
        case kWsPong: controlLen_ = 0; return true;
        default: drop("unknown frame type"); return false;
    }
}

void GrokWs::finishFrame(Handler &handler) {
    switch (frame_.opcode) {
        case kWsPing: sendFrame(kWsPong, control_, controlLen_); return;
        case kWsPong: return;
        case kWsClose: {
            launcherConsolePrintf(
                "Grok: server closed the connection (code %d)\n",
                controlLen_ >= 2 ? (control_[0] << 8) | control_[1] : 0
            );
            uint8_t reply[2] = {control_[0], control_[1]};
            sendFrame(kWsClose, reply, controlLen_ >= 2 ? 2 : 0);
            drop("closed by server");
            return;
        }
        default: break;
    }
    if (!frame_.fin) return;
    if (messageOpcode_ == kWsText) {
        if (textOverflow_) launcherConsolePrintf("Grok: dropped a message over %u bytes\n", (unsigned)kMaxTextMessage);
        else handler.onText(text_.get(), textLen_);
    }
    messageOpcode_ = 0;
}

void GrokWs::drop(const char *why) {
    launcherConsolePrintf("Grok: connection %s\n", why);
    open_ = false;
    if (tls_) {
        esp_tls_conn_destroy(tls_);
        tls_ = nullptr;
    }
    fd_ = -1;
}

void GrokWs::close() {
    if (open_) {
        uint8_t normal[2] = {0x03, 0xE8}; // 1000: normal closure.
        sendFrame(kWsClose, normal, sizeof normal);
    }
    open_ = false;
    if (tls_) {
        esp_tls_conn_destroy(tls_);
        tls_ = nullptr;
    }
    fd_ = -1;
}

#endif
