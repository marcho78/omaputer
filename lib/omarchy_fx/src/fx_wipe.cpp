// wipe: a diagonal wipe from the top-left corner reveals the logo, easing in and out,
// each character warming from purple into its final gradient color. Port of ttfx's wipe
// (src/fx/effects/wipe.rs; defaults in src/effects/wipe.rs).

#include <algorithm>

#include "canvas_effect.h"
#include "fx_math.h"

namespace ofx {
namespace {

constexpr int kEaserSteps = 100;  // The wipe's eased progress advances one step per frame.
constexpr int kSceneSteps = 12;   // 13 colors from the first gradient stop to the final color...
constexpr int kFrameTicks = 3;
constexpr int kSceneTicks = (kSceneSteps + 1) * kFrameTicks;  // ...39 ticks in all.
constexpr Rgb kFirstStop = hex(0x833ab4);

class Wipe final : public CanvasEffect {
public:
    const char* name() const override { return "wipe"; }

    bool step() override {
        if (!any_active(chars_) && easer_step_ >= kEaserSteps) return false;
        // ttfx's default wipe delay is 0, so the wipe moves on every frame.
        const double before = eased_;
        if (easer_step_ < kEaserSteps) {
            ++easer_step_;
            eased_ = std::clamp(ease(Easing::InOutCirc, static_cast<double>(easer_step_) / kEaserSteps), 0.0, 1.0);
        }
        const double count = static_cast<double>(groups_.size());
        const auto from = static_cast<size_t>(before * count);
        const auto to = static_cast<size_t>(eased_ * count);
        for (size_t g = from; g < to; ++g) {
            for (size_t k = groups_.starts[g]; k < groups_.starts[g + 1]; ++k) {
                Char& c = chars_[groups_.members[k]];
                c.visible = true;
                c.active = true;
                c.ticks = 0;
            }
        }
        update();
        return true;
    }

protected:
    void build() override {
        const std::vector<Rgb> finals = final_colors({kFirstStop, hex(0xfd1d1d), hex(0xfcb045)}, Direction::Vertical);
        chars_.assign(stage_.chars.size(), Char{});
        for (size_t i = 0; i < chars_.size(); ++i) chars_[i].final_color = finals[i];
        groups_ = diagonal_groups();
        easer_step_ = 0;
        eased_ = 0;
    }

    void paint(CellCanvas& canvas) const override {
        for (size_t i = 0; i < chars_.size(); ++i) {
            const Char& c = chars_[i];
            if (!c.visible) continue;
            const int frame = c.ticks == 0 ? 0 : (c.ticks - 1) / kFrameTicks;
            canvas.put(stage_.chars[i].col, stage_.chars[i].row, stage_.chars[i].glyph,
                       spectrum_color(kFirstStop, c.final_color, kSceneSteps, frame));
        }
    }

private:
    struct Char {
        Rgb final_color;
        uint8_t ticks = 0;
        bool visible = false;
        bool active = false;
    };

    void update() {
        for (Char& c : chars_) {
            if (c.active && ++c.ticks >= kSceneTicks) c.active = false;
        }
    }

    std::vector<Char> chars_;
    DiagonalGroups groups_;
    int easer_step_ = 0;
    double eased_ = 0;
};

}  // namespace

std::unique_ptr<Effect> make_wipe() { return std::make_unique<Wipe>(); }

}  // namespace ofx
