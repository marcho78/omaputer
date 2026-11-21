// A straight, eased move from one cell to another, advanced once per frame like a ttfx
// path with a single waypoint (src/fx/motion.rs).
#pragma once

#include <cmath>
#include <cstdint>

#include "fx_math.h"

namespace ofx {

// ease(easing, step / steps) in double precision, memoized. Double keeps positions
// identical to ttfx: in float, a rounding tie can land a character one cell off.
double eased_progress(Easing easing, uint16_t step, uint16_t steps);

struct Path {
    double total = 0;  // Distance, counting a row as two columns.
    int16_t from_col = 0;
    int16_t from_row = 0;
    int16_t to_col = 0;
    int16_t to_row = 0;
    uint16_t step = 0;
    uint16_t steps = 0;
    Easing easing = Easing::InOutSine;
    bool active = false;

    // Starts a move at `speed` distance per frame.
    void start(int fc, int fr, int tc, int tr, double speed, Easing e) {
        from_col = static_cast<int16_t>(fc);
        from_row = static_cast<int16_t>(fr);
        to_col = static_cast<int16_t>(tc);
        to_row = static_cast<int16_t>(tr);
        total = std::hypot(static_cast<double>(tc - fc), 2.0 * (tr - fr));
        steps = static_cast<uint16_t>(total == 0 ? 0 : rhe(total / speed));
        step = 0;
        easing = e;
        active = true;
    }

    // Advances one frame and writes the new position. Returns true in the frame the move
    // completes; a zero-step move lands on its target in its first frame.
    bool advance(int& col, int& row) {
        if (steps == 0 || step >= steps) {
            col = to_col;
            row = to_row;
        } else {
            ++step;
            // ttfx turns eased progress into a distance and back: t = f * total / total.
            const double t = eased_progress(easing, step, steps) * total / total;
            col = rhe((1 - t) * from_col + t * to_col);
            row = rhe((1 - t) * from_row + t * to_row);
        }
        if (step != steps) return false;
        active = false;
        return true;
    }
};

}  // namespace ofx
