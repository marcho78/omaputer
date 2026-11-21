// rain: raindrops fall from the top in random order, filling the logo row by row from the
// bottom, and each drop fades into a diagonal blue gradient as it lands. Port of ttfx's
// rain (src/fx/effects/rain.rs; defaults in src/effects/rain.rs).

#include <algorithm>

#include "canvas_effect.h"
#include "path.h"

namespace ofx {
namespace {

constexpr Rgb kDropColors[] = {hex(0x00315C), hex(0x004C8F), hex(0x0075DB), hex(0x3F91D9),
                               hex(0x78B9F2), hex(0x9AC8F5), hex(0xB8D8F8), hex(0xE3EFFC)};
constexpr uint16_t kDropSymbols[] = {'o', '.', ',', '*', '|'};
constexpr int kFadeSteps = 7;  // 8 colors from the drop's color to the final one...
constexpr int kFadeFrameTicks = 3;
constexpr int kFadeTicks = (kFadeSteps + 1) * kFadeFrameTicks;  // ...24 ticks in all.

class Rain final : public CanvasEffect {
public:
    const char* name() const override { return "rain"; }

    bool step() override {
        if (pending_.empty()) {
            if (next_row_ < rows_.size()) {
                const int row = rows_[next_row_++];
                for (size_t i = 0; i < chars_.size(); ++i) {
                    if (stage_.chars[i].row == row) pending_.push_back(static_cast<uint16_t>(i));  // Left to right.
                }
            } else if (!any_active(chars_)) {
                return false;
            }
        }
        if (!pending_.empty()) {
            const int releases = rng_.randint(1, 2);
            for (int k = 0; k < releases && !pending_.empty(); ++k) {
                const size_t pick = rng_.below(static_cast<uint32_t>(pending_.size()));
                Char& c = chars_[pending_[pick]];
                pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(pick));
                c.visible = true;
                c.active = true;
            }
        }
        update();
        return true;
    }

protected:
    void build() override {
        const std::vector<Rgb> finals = final_colors({hex(0x488bff), hex(0xb2e7de), hex(0x57eaf7)}, Direction::Diagonal);
        chars_.assign(stage_.chars.size(), Char{});
        rows_.clear();
        for (size_t i = 0; i < chars_.size(); ++i) {
            const StageChar& s = stage_.chars[i];
            Char& c = chars_[i];
            c.drop_color = static_cast<uint8_t>(rng_.below(8));
            c.drop_symbol = static_cast<uint8_t>(rng_.below(5));
            c.final_color = finals[i];
            c.col = s.col;
            c.row = static_cast<int16_t>(stage_.height);  // Every drop starts in the top row.
            c.path.start(s.col, stage_.height, s.col, s.row, rng_.uniform(0.33, 0.57), Easing::InQuart);
            if (rows_.empty() || rows_.back() != s.row) rows_.push_back(s.row);
        }
        // Characters come top line first, so reversing gives the rows bottom first.
        std::reverse(rows_.begin(), rows_.end());
        pending_.clear();
        next_row_ = 0;
    }

    void paint(CellCanvas& canvas) const override {
        for (size_t i = 0; i < chars_.size(); ++i) {
            const Char& c = chars_[i];
            if (!c.visible) continue;
            if (c.fading) {
                const int frame = c.fade_ticks == 0 ? 0 : (c.fade_ticks - 1) / kFadeFrameTicks;
                canvas.put(c.col, c.row, stage_.chars[i].glyph,
                           spectrum_color(kDropColors[c.drop_color], c.final_color, kFadeSteps, frame));
            } else {
                canvas.put(c.col, c.row, kDropSymbols[c.drop_symbol], kDropColors[c.drop_color]);
            }
        }
    }

private:
    struct Char {
        Path path;
        int16_t col = 0;
        int16_t row = 0;
        Rgb final_color;
        uint8_t drop_color = 0;
        uint8_t drop_symbol = 0;
        uint8_t fade_ticks = 0;
        bool fading = false;  // Landed: the fade has replaced the raindrop.
        bool visible = false;
        bool active = false;
    };

    void update() {
        for (Char& c : chars_) {
            if (!c.active) continue;
            if (c.path.active) {
                int col = c.col, row = c.row;
                if (c.path.advance(col, row)) {
                    c.fading = true;  // Landing starts the fade, ticked from this same frame.
                    c.fade_ticks = 0;
                }
                c.col = static_cast<int16_t>(col);
                c.row = static_cast<int16_t>(row);
            }
            if (c.fading && c.fade_ticks < kFadeTicks) ++c.fade_ticks;
        }
        for (Char& c : chars_) {
            if (c.active && !c.path.active && c.fading && c.fade_ticks >= kFadeTicks) c.active = false;
        }
    }

    std::vector<Char> chars_;
    std::vector<int> rows_;  // Text rows, bottom first.
    std::vector<uint16_t> pending_;
    size_t next_row_ = 0;
};

}  // namespace

std::unique_ptr<Effect> make_rain() { return std::make_unique<Rain>(); }

}  // namespace ofx
