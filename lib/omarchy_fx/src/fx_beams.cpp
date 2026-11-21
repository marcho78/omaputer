// beams: light beams sweep across every row and column and leave the logo dimmed, then a
// diagonal wipe brightens it into a purple-to-white gradient. Port of ttfx's beams
// (src/fx/effects/beams.rs; defaults in src/effects/beams.rs).

#include <algorithm>

#include "canvas_effect.h"

namespace ofx {
namespace {

// The beam glyph for each of the 9 beam colors, as ttfx spreads its symbols over them.
constexpr uint16_t kRowGlyphs[] = {0x2582, 0x2582, 0x2582, 0x2581, 0x2581, 0x2581, '_', '_', '_'};
constexpr uint16_t kColumnGlyphs[] = {0x258C, 0x258C, 0x258C, 0x258D, 0x258D, 0x258E, 0x258E, 0x258F, 0x258F};
constexpr int kBeamFrames = 9;
constexpr int kBeamTicks = 2 * (kBeamFrames + 11);  // 9 beam frames then an 11-color fade, 2 ticks each.
constexpr int kBrightenTicks = 4 * 11;              // 11 colors, 4 ticks each.
constexpr int kLaunchDelay = 6;                     // Frames between group launches.
constexpr int kWipeSpeed = 3;                       // Diagonal groups brightened per frame.
constexpr double kDimBrightness = 0.3;

enum class Scene : uint8_t { None, Row, Column, Brighten };
enum class Phase : uint8_t { Beams, FinalWipe, Complete };

class Beams final : public CanvasEffect {
public:
    const char* name() const override { return "beams"; }

    bool step() override {
        if (phase_ == Phase::Complete && active_ == 0) return false;
        if (phase_ == Phase::Beams) {
            if (delay_ == 0) {
                if (next_group_ < groups_.size()) {
                    const size_t left = groups_.size() - next_group_;
                    const size_t take = std::min(static_cast<size_t>(rng_.randint(1, 5)), left);
                    for (size_t k = 0; k < take; ++k) launched_.push_back(next_group_++);
                }
                delay_ = kLaunchDelay;
            } else {
                --delay_;
            }
            for (size_t g : launched_) advance(groups_[g]);
            launched_.erase(std::remove_if(launched_.begin(), launched_.end(),
                                           [this](size_t g) { return groups_[g].released == length(groups_[g]); }),
                            launched_.end());
            if (next_group_ == groups_.size() && launched_.empty() && active_ == 0) phase_ = Phase::FinalWipe;
        } else if (phase_ == Phase::FinalWipe) {
            if (next_wipe_ < wipe_.size()) {
                const size_t end = std::min(next_wipe_ + kWipeSpeed, wipe_.size());
                for (; next_wipe_ < end; ++next_wipe_) {
                    for (size_t k = wipe_.starts[next_wipe_]; k < wipe_.starts[next_wipe_ + 1]; ++k) {
                        const StageChar& c = stage_.chars[wipe_.members[k]];
                        activate(cell_index(c.col, c.row), Scene::Brighten);
                    }
                }
            } else {
                phase_ = Phase::Complete;
            }
        }
        update();
        return true;
    }

protected:
    void build() override {
        const int w = stage_.width, h = stage_.height;
        cells_.assign(static_cast<size_t>(w * h), Cell{});
        beam_ = spectrum({hex(0xffffff), hex(0x00D1FF), hex(0x8A008A)}, {2, 6});

        // Per text row, bottom first: the dimming fade after a beam passes, and the
        // brightening back to the final color during the closing wipe.
        const std::vector<Rgb> finals = spectrum({hex(0x8A008A), hex(0x00D1FF), hex(0xffffff)}, {12});
        const TextBox& t = stage_.text;
        fade_.clear();
        bright_.clear();
        for (int row = t.bottom; row <= t.top; ++row) {
            const Rgb color = color_at(finals, gradient_fraction(Direction::Vertical, t, t.left, row));
            const Rgb dim = adjust_brightness(color, kDimBrightness);
            fade_.push_back(spectrum({color, dim}, {10}));
            bright_.push_back(spectrum({dim, color}, {10}));
        }
        for (const StageChar& c : stage_.chars) {
            Cell& cell = cells_[cell_index(c.col, c.row)];
            cell.glyph = static_cast<uint16_t>(c.glyph);
            cell.shade = static_cast<uint8_t>(1 + c.row - t.bottom);
        }

        // Every row (top first) and every column becomes a beam group; then shuffle them.
        groups_.clear();
        for (int row = h; row >= 1; --row) groups_.push_back(make_group(false, row, rng_.randint(15, 60)));
        for (int col = 1; col <= w; ++col) groups_.push_back(make_group(true, col, rng_.randint(9, 15)));
        rng_.shuffle(groups_);

        wipe_ = diagonal_groups();
        launched_.clear();
        next_group_ = next_wipe_ = 0;
        delay_ = 0;
        active_ = 0;
        phase_ = Phase::Beams;
    }

    void paint(CellCanvas& canvas) const override {
        const int w = stage_.width;
        for (size_t i = 0; i < cells_.size(); ++i) {
            const Cell& c = cells_[i];
            if (!c.visible) continue;
            const int col = static_cast<int>(i % static_cast<size_t>(w)) + 1;
            const int row = static_cast<int>(i / static_cast<size_t>(w)) + 1;
            const int k = c.ticks;
            if (c.shown == Scene::Brighten) {
                canvas.put(col, row, c.glyph, bright_[c.shade - 1][static_cast<size_t>(k == 0 ? 0 : (k - 1) / 4)]);
                continue;
            }
            const int j = k == 0 ? 0 : (k - 1) / 2;
            if (j < kBeamFrames) {
                canvas.put(col, row, c.shown == Scene::Column ? kColumnGlyphs[j] : kRowGlyphs[j],
                           beam_[static_cast<size_t>(j)]);
            } else if (c.shade != 0) {  // Cells without a character fade to black: nothing to draw.
                canvas.put(col, row, c.glyph, fade_[c.shade - 1][static_cast<size_t>(j - kBeamFrames)]);
            }
        }
    }

private:
    struct Cell {
        uint16_t glyph = ' ';
        uint8_t shade = 0;          // 0 for a cell without a character, else 1 + its text row.
        Scene scene = Scene::None;  // The running scene.
        Scene shown = Scene::None;  // The scene on screen; kept once it finishes.
        uint8_t ticks = 0;
        bool visible = false;
    };

    struct Group {
        bool column = false;
        bool reversed = false;
        uint8_t index = 0;     // Its row or column, 1-based.
        uint8_t speed = 0;     // Cells per frame, in tenths.
        uint16_t counter = 0;  // Tenths built up.
        uint8_t released = 0;  // Cells hit so far.
    };

    size_t cell_index(int col, int row) const { return static_cast<size_t>((row - 1) * stage_.width + (col - 1)); }
    int length(const Group& g) const { return g.column ? stage_.height : stage_.width; }

    Group make_group(bool column, int index, int speed) {
        Group g;
        g.column = column;
        g.index = static_cast<uint8_t>(index);
        g.speed = static_cast<uint8_t>(speed);
        g.reversed = rng_.below(2) == 0;
        return g;
    }

    void advance(Group& g) {
        g.counter = static_cast<uint16_t>(g.counter + g.speed);
        const int whole = g.counter / 10;
        if (whole <= 1) return;  // ttfx only releases once more than one whole cell has built up.
        const int n = std::min(whole, length(g) - g.released);
        for (int k = 0; k < n; ++k) {
            const int p = g.reversed ? length(g) - 1 - g.released : g.released;
            ++g.released;
            activate(g.column ? cell_index(g.index, 1 + p) : cell_index(1 + p, g.index),
                     g.column ? Scene::Column : Scene::Row);
        }
        g.counter = static_cast<uint16_t>(g.counter - n * 10);
    }

    // Starts a scene from its first frame; a beam hitting a cell mid-scene restarts it.
    void activate(size_t index, Scene scene) {
        Cell& c = cells_[index];
        if (c.scene == Scene::None) ++active_;
        c.scene = c.shown = scene;
        c.ticks = 0;
        c.visible = true;
    }

    void update() {
        for (Cell& c : cells_) {
            if (c.scene == Scene::None) continue;
            if (++c.ticks == (c.scene == Scene::Brighten ? kBrightenTicks : kBeamTicks)) {
                c.scene = Scene::None;
                --active_;
            }
        }
    }

    std::vector<Cell> cells_;
    std::vector<Rgb> beam_;
    std::vector<std::vector<Rgb>> fade_;
    std::vector<std::vector<Rgb>> bright_;
    std::vector<Group> groups_;
    std::vector<size_t> launched_;
    DiagonalGroups wipe_;
    size_t next_group_ = 0;
    size_t next_wipe_ = 0;
    int delay_ = 0;
    int active_ = 0;
    Phase phase_ = Phase::Beams;
};

}  // namespace

std::unique_ptr<Effect> make_beams() { return std::make_unique<Beams>(); }

}  // namespace ofx
