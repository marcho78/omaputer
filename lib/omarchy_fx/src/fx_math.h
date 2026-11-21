// Numeric helpers matching ttfx (which mirrors Python): round-half-even, floor division,
// and the easing curves the ported effects use (ttfx src/utils/pycompat.rs, easing.rs).
#pragma once

#include <cmath>

namespace ofx {

// Rounds halves to even, like Python's round(). Relies on the default rounding mode;
// std::round and (int)(x + 0.5) round halves up and must not be used instead.
inline int rhe(double x) { return static_cast<int>(std::nearbyint(x)); }
inline int rhe(float x) { return static_cast<int>(std::nearbyint(x)); }

// Integer division rounded toward negative infinity, like Python's //. C++ truncates.
inline int floor_div(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

enum class Easing { InQuart, InOutQuad, InOutCirc, InOutSine };

// Maps progress p in [0, 1] to eased progress.
template <typename T>
T ease(Easing easing, T p) {
    constexpr T kOne = 1, kTwo = 2, kHalf = static_cast<T>(0.5);
    constexpr T kPi = static_cast<T>(3.14159265358979323846);
    switch (easing) {
        case Easing::InQuart:
            return p * p * p * p;
        case Easing::InOutQuad: {
            if (p < kHalf) return kTwo * p * p;
            const T q = -kTwo * p + kTwo;
            return kOne - q * q / kTwo;
        }
        case Easing::InOutCirc: {
            if (p < kHalf) return (kOne - std::sqrt(kOne - (kTwo * p) * (kTwo * p))) / kTwo;
            const T q = -kTwo * p + kTwo;
            return (std::sqrt(kOne - q * q) + kOne) / kTwo;
        }
        case Easing::InOutSine:
            return -(std::cos(kPi * p) - kOne) / kTwo;
    }
    return p;
}

}  // namespace ofx
