// middleout: the logo starts as one white point in the middle, spreads out along the
// centre row, then every column opens to its full height while fading from white into a
// purple-to-white gradient. Port of ttfx's middleout (src/fx/effects/middleout.rs;
// defaults in src/effects/middleout.rs).

#include "canvas_effect.h"
#include "path.h"

namespace ofx {
namespace {

constexpr double kSpeed = 0.6;   // Both the centre and the full movement.
constexpr int kSceneSteps = 10;  // 11 colors from white to the final color...
constexpr int kFrameTicks = 6;
constexpr int kSceneTicks = (kSceneSteps + 1) * kFrameTicks;  // ...66 ticks in all.
constexpr Rgb kStartColor = hex(0xffffff);

class Middleout final : public CanvasEffect {
public:
    const char* name() const override { return "middleout"; }

    bool step() override {
        if (!full_ && !any_active(chars_)) {
            full_ = true;  // Every character now opens out to its own row.
            for (size_t i = 0; i < chars_.size(); ++i) {
                Char& c = chars_[i];
                const StageChar& s = stage_.chars[i];
                c.active = true;
                c.ticks = 0;
                c.path.start(c.col, c.row, s.col, s.row, kSpeed, Easing::InOutSine);
            }
        }
        if (!any_active(chars_)) return false;
        update();
        return true;
    }

protected:
    void build() override {
        const std::vector<Rgb> finals = final_colors({hex(0x8A008A), hex(0x00D1FF), hex(0xFFFFFF)}, Direction::Vertical);
        chars_.assign(stage_.chars.size(), Char{});
        for (size_t i = 0; i < chars_.size(); ++i) {
            Char& c = chars_[i];
            c.final_color = finals[i];
            c.col = static_cast<int16_t>(stage_.center_col);
            c.row = static_cast<int16_t>(stage_.center_row);
            c.visible = true;
            c.active = true;
            c.path.start(c.col, c.row, stage_.chars[i].col, c.row, kSpeed, Easing::InOutSine);
        }
        full_ = false;
    }

    void paint(CellCanvas& canvas) const override {
        for (size_t i = 0; i < chars_.size(); ++i) {
            const Char& c = chars_[i];
            if (!c.visible) continue;
            const int frame = c.ticks == 0 ? 0 : (c.ticks - 1) / kFrameTicks;
            const Rgb color = full_ ? spectrum_color(kStartColor, c.final_color, kSceneSteps, frame) : kStartColor;
            canvas.put(c.col, c.row, stage_.chars[i].glyph, color);
        }
    }

private:
    struct Char {
        Path path;
        int16_t col = 0;
        int16_t row = 0;
        Rgb final_color;
        uint8_t ticks = 0;
        bool visible = false;
        bool active = false;
    };

    void update() {
        for (Char& c : chars_) {
            if (!c.active) continue;
            if (c.path.active) {
                int col = c.col, row = c.row;
                c.path.advance(col, row);
                c.col = static_cast<int16_t>(col);
                c.row = static_cast<int16_t>(row);
            }
            if (full_ && c.ticks < kSceneTicks) ++c.ticks;
        }
        for (Char& c : chars_) {
            if (c.active && !c.path.active && (!full_ || c.ticks >= kSceneTicks)) c.active = false;
        }
    }

    std::vector<Char> chars_;
    bool full_ = false;
};

}  // namespace

std::unique_ptr<Effect> make_middleout() { return std::make_unique<Middleout>(); }

}  // namespace ofx
