// Native shim: "mounts" the fake filesystem from FS.h instead of a real card.
#pragma once

#include "FS.h"

class SDClass : public FS {
public:
    bool begin(int = -1) { return true; }
    bool begin(int, SPIClass &) { return true; }
    // Full form used by formatSdCard(): (cs, spi, frequency, mountpoint, max_files, format_if_empty).
    bool begin(int, SPIClass &, uint32_t, const char *, uint8_t, bool) { return true; }
    void end() {}
    bool rename(const char *, const char *) { return false; }
    uint64_t cardSize() { return 32ULL * 1024 * 1024 * 1024; }
    uint64_t usedBytes() { return 128ULL * 1024 * 1024; }
};
extern SDClass SD;
