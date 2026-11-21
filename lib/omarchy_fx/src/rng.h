// Small seedable random numbers (PCG32) with the helpers the effects need. ttfx uses
// xoshiro256++; only the distributions have to match, not the exact sequence.
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace ofx {

class Rng {
public:
    explicit Rng(uint64_t seed = 0) { reseed(seed); }

    void reseed(uint64_t seed) {
        state_ = 0;
        inc_ = (seed << 1) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<uint32_t>(((old >> 18) ^ old) >> 27);
        const auto rot = static_cast<uint32_t>(old >> 59);
        return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
    }

    // Uniform in [0, n) with no modulo bias (Lemire's method). n must be at least 1.
    uint32_t below(uint32_t n) {
        uint64_t m = static_cast<uint64_t>(next()) * n;
        auto low = static_cast<uint32_t>(m);
        if (low < n) {
            const uint32_t threshold = (0u - n) % n;
            while (low < threshold) {
                m = static_cast<uint64_t>(next()) * n;
                low = static_cast<uint32_t>(m);
            }
        }
        return static_cast<uint32_t>(m >> 32);
    }

    int randint(int lo, int hi) { return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo + 1))); }  // [lo, hi]
    int randrange(int lo, int hi) { return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo))); }    // [lo, hi)
    double uniform(double lo, double hi) { return lo + (hi - lo) * (next() >> 8) * (1.0 / 16777216.0); }     // [lo, hi)

    template <typename T>
    void shuffle(std::vector<T>& v) {
        for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[below(static_cast<uint32_t>(i))]);
    }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

}  // namespace ofx
