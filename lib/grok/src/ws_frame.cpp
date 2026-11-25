#include "ws_frame.h"

namespace grokcore {

size_t ws_client_header(uint8_t out[kWsMaxClientHeader], uint8_t opcode, uint64_t payload_len,
                        const uint8_t mask[4]) {
    size_t n = 0;
    out[n++] = static_cast<uint8_t>(0x80 | (opcode & 0x0F));
    if (payload_len < 126) {
        out[n++] = static_cast<uint8_t>(0x80 | payload_len);
    } else if (payload_len <= 0xFFFF) {
        out[n++] = 0x80 | 126;
        out[n++] = static_cast<uint8_t>(payload_len >> 8);
        out[n++] = static_cast<uint8_t>(payload_len);
    } else {
        out[n++] = 0x80 | 127;
        for (int shift = 56; shift >= 0; shift -= 8) out[n++] = static_cast<uint8_t>(payload_len >> shift);
    }
    for (int i = 0; i < 4; ++i) out[n++] = mask[i];
    return n;
}

void ws_mask(uint8_t* data, size_t len, const uint8_t mask[4], uint64_t offset) {
    for (size_t i = 0; i < len; ++i) data[i] ^= mask[(offset + i) & 3];
}

int ws_parse_server_header(const uint8_t* b, size_t available, WsHeader& out) {
    if (available < 2) return 0;
    if (b[0] & 0x70) return -1;  // Reserved bits: no extensions were agreed.
    if (b[1] & 0x80) return -1;  // A server must not mask its frames.
    const uint8_t opcode = b[0] & 0x0F;
    const bool fin = (b[0] & 0x80) != 0;
    const uint8_t len7 = b[1] & 0x7F;
    const bool control = (opcode & 0x08) != 0;
    if (control && (!fin || len7 > 125)) return -1;

    size_t size = 2;
    uint64_t length = len7;
    if (len7 == 126) {
        size = 4;
        if (available < size) return 0;
        length = (static_cast<uint64_t>(b[2]) << 8) | b[3];
    } else if (len7 == 127) {
        size = 10;
        if (available < size) return 0;
        if (b[2] & 0x80) return -1;
        length = 0;
        for (int i = 2; i < 10; ++i) length = (length << 8) | b[i];
    }
    out.fin = fin;
    out.opcode = opcode;
    out.length = length;
    return static_cast<int>(size);
}

}  // namespace grokcore
