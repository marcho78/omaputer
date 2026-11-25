#if defined(OMARCHY_GROK)

#include "omarchy_chat.h"

#include "idf/ca_certs.h"
#include "idf/idf_wifi.h"
#include "idf/launcher_platform.h"
#include "omarchy_grok.h"
#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_tls.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <memory>
#include <new>
#include <strings.h>

namespace {

constexpr const char *kHost = "api.x.ai";
constexpr int kConnectTimeoutMs = 15000;
constexpr int64_t kReadTimeoutUs = 120 * 1000000LL; // A reasoning model can think a while before writing.
constexpr size_t kBufSize = 1024;
constexpr size_t kMaxBody = 512 * 1024; // A long chat sent again in full once xAI has forgotten it.

unsigned freeHeap() { return static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)); }

// Whether text contains word, ignoring case, as header values may come in either.
bool containsNoCase(const char *text, const char *word) {
    const size_t n = strlen(word);
    for (; *text; ++text) {
        if (strncasecmp(text, word, n) == 0) return true;
    }
    return false;
}

// An error the portal shows as it is, in the shape xAI's own errors have.
esp_err_t fail(httpd_req_t *req, const char *status, const char *message) {
    launcherConsolePrintf("Chat: %s\n", message);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char body[200];
    snprintf(body, sizeof body, "{\"error\":\"%s\"}", message);
    return httpd_resp_sendstr(req, body);
}

// The TLS connection to xAI, read through a small buffer.
class Upstream {
public:
    explicit Upstream(char *buf) : buf_(buf) {}
    ~Upstream() {
        if (tls_) esp_tls_conn_destroy(tls_);
    }

    bool connect() {
        tls_ = esp_tls_init();
        if (!tls_) return false;
        esp_tls_cfg_t cfg = {};
        cfg.cacert_buf = reinterpret_cast<const unsigned char *>(kRootCAs); // xAI chains to GlobalSign Root CA.
        cfg.cacert_bytes = sizeof(kRootCAs);
        cfg.timeout_ms = kConnectTimeoutMs;
        if (esp_tls_conn_new_sync(kHost, strlen(kHost), 443, &cfg, tls_) != 1) return false;
        return esp_tls_get_conn_sockfd(tls_, &fd_) == ESP_OK;
    }

    bool write(const char *data, size_t len) {
        while (len > 0) {
            const ssize_t n = esp_tls_conn_write(tls_, data, len);
            if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) continue;
            if (n <= 0) return false;
            data += n;
            len -= static_cast<size_t>(n);
        }
        return true;
    }

    // Up to n bytes of what has arrived, waiting for more when none is left. 0 once the
    // connection ends, fails or has been quiet for the read timeout.
    size_t take(const char *&data, size_t n) {
        if (pos_ == len_ && !fill()) return 0;
        const size_t k = std::min(n, len_ - pos_);
        data = buf_ + pos_;
        pos_ += k;
        return k;
    }

    // One line without its CR LF, cut to fit out. False if the connection ends first.
    bool line(char *out, size_t cap) {
        size_t used = 0;
        for (;;) {
            if (pos_ == len_ && !fill()) return false;
            const char c = buf_[pos_++];
            if (c == '\n') break;
            if (c != '\r' && used + 1 < cap) out[used++] = c;
        }
        out[used] = '\0';
        return true;
    }

private:
    bool fill() {
        if (!tls_ || closed_) return false;
        const int64_t deadline = esp_timer_get_time() + kReadTimeoutUs;
        for (;;) {
            if (esp_tls_get_bytes_avail(tls_) <= 0) {
                const int64_t left = deadline - esp_timer_get_time();
                if (left <= 0) return false;
                fd_set set;
                FD_ZERO(&set);
                FD_SET(fd_, &set);
                timeval tv = {static_cast<time_t>(left / 1000000), static_cast<suseconds_t>(left % 1000000)};
                if (select(fd_ + 1, &set, nullptr, nullptr, &tv) <= 0) return false;
            }
            const ssize_t n = esp_tls_conn_read(tls_, buf_, kBufSize);
            if (n == ESP_TLS_ERR_SSL_WANT_READ || n == ESP_TLS_ERR_SSL_WANT_WRITE) continue;
            if (n <= 0) {
                closed_ = true;
                return false;
            }
            pos_ = 0;
            len_ = static_cast<size_t>(n);
            return true;
        }
    }

    esp_tls_t *tls_ = nullptr;
    int fd_ = -1;
    char *buf_;
    size_t pos_ = 0;
    size_t len_ = 0;
    bool closed_ = false;
};

} // namespace

esp_err_t omarchyChatRelay(httpd_req_t *req, httpd_method_t method, const char *path) {
    String key = omarchyGrokKey();
    if (key.isEmpty()) return fail(req, "400 Bad Request", "No xAI key yet: add it in Settings, under Grok.");
    if (!launcherWifiIsConnected()) {
        return fail(req, "503 Service Unavailable", "The Cardputer isn't on the internet: start Web UI on your network, not its own.");
    }
    const size_t bodyLen = method == HTTP_POST ? req->content_len : 0;
    if (bodyLen > kMaxBody) return fail(req, "413 Payload Too Large", "This chat is too long to send. Start a new one.");

    std::unique_ptr<char[]> buf(new (std::nothrow) char[kBufSize]);
    if (!buf) return fail(req, "503 Service Unavailable", "The Cardputer is out of memory.");
    Upstream up(buf.get());
    unsigned lowest = freeHeap();
    if (!up.connect()) return fail(req, "502 Bad Gateway", "Can't reach xAI.");
    lowest = std::min(lowest, freeHeap());

    // The request, with the key added here.
    String head;
    head.reserve(256 + key.length());
    head += method == HTTP_POST ? "POST " : "GET ";
    head += path;
    head += " HTTP/1.1\r\nHost: ";
    head += kHost;
    head += "\r\nAuthorization: Bearer ";
    head += key;
    head += "\r\nUser-Agent: cardputer-launcher\r\nAccept: text/event-stream, application/json\r\nConnection: close\r\n";
    if (method == HTTP_POST) {
        head += "Content-Type: application/json\r\nContent-Length: ";
        head += String(static_cast<unsigned>(bodyLen));
        head += "\r\n";
    }
    head += "\r\n";
    const bool sentHead = up.write(head.c_str(), head.length());
    head = String(); // Holds the key; let it go now.
    key = String();
    if (!sentHead) return fail(req, "502 Bad Gateway", "Lost xAI while sending.");

    for (size_t left = bodyLen; left > 0;) {
        const int n = httpd_req_recv(req, buf.get(), std::min(left, kBufSize));
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return fail(req, "400 Bad Request", "The browser stopped sending.");
        if (!up.write(buf.get(), static_cast<size_t>(n))) return fail(req, "502 Bad Gateway", "Lost xAI while sending.");
        left -= static_cast<size_t>(n);
    }

    // xAI's status line and headers. httpd keeps pointers to the status and type until the
    // answer is sent, and serves one request at a time, so these live in static buffers.
    static char status[48];
    static char type[64];
    char line[160];
    if (!up.line(line, sizeof line) || strncmp(line, "HTTP/1.", 7) != 0 || !strchr(line, ' ')) {
        return fail(req, "502 Bad Gateway", "xAI didn't answer.");
    }
    const char *code = strchr(line, ' ') + 1;
    const int statusCode = atoi(code);
    snprintf(status, sizeof status, "%s", code);
    snprintf(type, sizeof type, "application/json");
    bool chunked = false;
    long length = -1; // -1: until xAI closes the connection.
    while (up.line(line, sizeof line) && line[0]) {
        if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 && containsNoCase(line, "chunked")) {
            chunked = true;
        } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
            length = atol(line + 15);
        } else if (strncasecmp(line, "Content-Type:", 13) == 0) {
            const char *value = line + 13;
            while (*value == ' ') ++value;
            snprintf(type, sizeof type, "%s", value);
        }
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // The body, passed on as it comes. A chunked body is unwrapped first: the browser gets
    // the web server's own chunks.
    size_t bytes = 0;
    bool browserGone = false;
    auto pass = [&](size_t n) {
        while (n > 0 && !browserGone) {
            const char *data = nullptr;
            const size_t k = up.take(data, n);
            if (k == 0) return false;
            if (httpd_resp_send_chunk(req, data, k) != ESP_OK) browserGone = true;
            bytes += k;
            n -= k;
            lowest = std::min(lowest, freeHeap());
        }
        return !browserGone;
    };
    if (chunked) {
        while (up.line(line, sizeof line)) {
            const long size = strtol(line, nullptr, 16);
            if (size <= 0 || !pass(static_cast<size_t>(size)) || !up.line(line, sizeof line)) break;
        }
    } else if (length >= 0) {
        pass(static_cast<size_t>(length));
    } else {
        while (pass(kBufSize)) {}
    }
    if (!browserGone) httpd_resp_send_chunk(req, nullptr, 0);
    launcherConsolePrintf(
        "Chat: xAI answered %d, %u bytes%s, lowest free heap %u, stack left %u\n",
        statusCode,
        static_cast<unsigned>(bytes),
        browserGone ? ", browser left" : "",
        lowest,
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr))
    );
    return ESP_OK;
}

#endif
