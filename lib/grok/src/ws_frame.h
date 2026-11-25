// WebSocket frame headers (RFC 6455): writing the client's masked frames and reading the
// server's. The device's client (src/device/ws_client.cpp) does the I/O.
#pragma once

#include <cstddef>
#include <cstdint>

namespace grokcore {

enum WsOpcode : uint8_t {
    kWsContinuation = 0x0,
    kWsText = 0x1,
    kWsBinary = 0x2,
    kWsClose = 0x8,
    kWsPing = 0x9,
    kWsPong = 0xA,
};

constexpr size_t kWsMaxClientHeader = 14;

// Writes the header of a single, final, masked client frame and returns its size (6..14).
size_t ws_client_header(uint8_t out[kWsMaxClientHeader], uint8_t opcode, uint64_t payload_len,
                        const uint8_t mask[4]);

// XORs data with the mask. offset is the position of data[0] within the frame's payload,
// so a payload can be masked in pieces.
void ws_mask(uint8_t* data, size_t len, const uint8_t mask[4], uint64_t offset = 0);

struct WsHeader {
    bool fin = false;
    uint8_t opcode = 0;
    uint64_t length = 0;
};

// Reads a server frame's header from the first `available` bytes. Returns the header's
// size once all of it is there, 0 if more bytes are needed, or -1 if it is not a valid
// frame from a server (masked, reserved bits set, a fragmented or oversized control frame,
// or a length using the top bit).
int ws_parse_server_header(const uint8_t* bytes, size_t available, WsHeader& out);

}  // namespace grokcore
