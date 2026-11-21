#include "path.h"

namespace ofx {

double eased_progress(Easing easing, uint16_t step, uint16_t steps) {
    // The ESP32-S3 does double math in software, so cache results: characters with the
    // same path length need the same value in the same frame (ttfx caches eased
    // progress per easing and length too, src/fx/motion.rs:341-366). The cache only ever
    // returns what ease() would, so sharing it between effects is safe.
    struct Entry {
        uint32_t key = UINT32_MAX;
        double value = 0;
    };
    static Entry cache[256];

    const uint32_t key = (static_cast<uint32_t>(easing) << 30) | (static_cast<uint32_t>(steps & 0x7FFF) << 15) |
                         (step & 0x7FFF);
    Entry& slot = cache[(key * 2654435761u) >> 24];
    if (slot.key != key) {
        slot.key = key;
        slot.value = ease(easing, static_cast<double>(step) / static_cast<double>(steps));
    }
    return slot.value;
}

}  // namespace ofx
