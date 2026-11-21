// slide: each row slides in from the left edge, released one character at a time from its
// right end, while fading from purple into a purple-red-orange gradient. Port of ttfx's
// slide (src/fx/effects/slide.rs; defaults in src/effects/slide.rs).

#include <algorithm>

#include "canvas_effect.h"
#include "path.h"

namespace ofx {
namespace {

constexpr double kSpeed = 0.8;
constexpr int kGap = 2;          // Frames between row launches.
constexpr int kSceneSteps = 10;  // 11 colors from the first gradient stop to the final color...
constexpr int kFrameTicks = 6;
constexpr int kSceneTicks = (kSceneSteps + 1) * kFrameTicks;  // ...66 ticks in all.
constexpr Rgb kFirstStop = hex(0x833ab4);

class Slide final : public CanvasEffect {
public:
    const char* name() const override { return "slide"; }

    bool step() override {
        const bool pending = next_group_ < groups_.size();
        if (!pending && launched_.empty() && !any_active(chars_)) return false;
        if (pending) {
            if (gap_ == kGap) {
                launched_.push_back({next_group_++, 0});
                gap_ = 0;
            } else {
                ++gap_;
            }
        }
        for (Launch& l : launched_) {
            const Group& g = groups_[l.group];
            release(g.first + g.count - 1 - l.released++);  // Right to left.
        }
        launched_.erase(std::remove_if(launched_.begin(), launched_.end(),
                                       [this](const Launch& l) { return l.released == groups_[l.group].count; }),
                        launched_.end());
        update();
        return true;
    }

protected:
    void build() override {
        const std::vector<Rgb> finals = final_colors({kFirstStop, hex(0xfd1d1d), hex(0xfcb045)}, Direction::Vertical);
        chars_.assign(stage_.chars.size(), Char{});
        groups_.clear();
        for (size_t i = 0; i < chars_.size(); ++i) {
            chars_[i].final_color = finals[i];
            chars_[i].col = static_cast<int16_t>(stage_.text.left - 1);  // Just off the canvas.
            // Characters come row by row, top first, left to right: each run is one group.
            if (groups_.empty() || stage_.chars[groups_.back().first].row != stage_.chars[i].row) {
                groups_.push_back({i, 0});
            }
            ++groups_.back().count;
        }
        launched_.clear();
        next_group_ = 0;
        gap_ = 0;
    }

    void paint(CellCanvas& canvas) const override {
        for (size_t i = 0; i < chars_.size(); ++i) {
            const Char& c = chars_[i];
            if (!c.visible) continue;
            const int frame = c.ticks == 0 ? 0 : (c.ticks - 1) / kFrameTicks;
            canvas.put(c.col, stage_.chars[i].row, stage_.chars[i].glyph,
                       spectrum_color(kFirstStop, c.final_color, kSceneSteps, frame));
        }
    }

private:
    struct Char {
        Path path;
        int16_t col = 0;
        Rgb final_color;
        uint8_t ticks = 0;
        bool visible = false;
        bool active = false;
    };
    struct Group {
        size_t first;
        size_t count;
    };
    struct Launch {
        size_t group;
        size_t released;
    };

    void release(size_t i) {
        Char& c = chars_[i];
        const StageChar& s = stage_.chars[i];
        c.visible = true;
        c.active = true;
        c.path.start(c.col, s.row, s.col, s.row, kSpeed, Easing::InOutQuad);
    }

    void update() {
        for (Char& c : chars_) {
            if (!c.active) continue;
            if (c.path.active) {
                int col = c.col, row = 0;
                c.path.advance(col, row);
                c.col = static_cast<int16_t>(col);
            }
            if (c.ticks < kSceneTicks) ++c.ticks;
        }
        for (Char& c : chars_) {
            if (c.active && !c.path.active && c.ticks >= kSceneTicks) c.active = false;
        }
    }

    std::vector<Char> chars_;
    std::vector<Group> groups_;
    std::vector<Launch> launched_;
    size_t next_group_ = 0;
    int gap_ = 0;
};

}  // namespace

std::unique_ptr<Effect> make_slide() { return std::make_unique<Slide>(); }

}  // namespace ofx
