// Shared base for effects that animate the logo's characters on a cell canvas the way ttfx
// does: begin() lays out the canvas and runs the effect's build step; draw() paints the
// visible characters in ascending id order, so the highest id wins a shared cell.
#pragma once

#include <vector>

#include "cell_canvas.h"
#include "color.h"
#include "effect.h"
#include "rng.h"
#include "stage.h"

namespace ofx {

class CanvasEffect : public Effect {
public:
    void begin(const Logo& logo, int cols, int rows, uint32_t seed) final {
        stage_ = Stage(logo, cols, rows);
        rng_.reseed(seed);
        canvas_.reset(cols, rows);
        build();
    }

    void draw(Framebuffer& fb, const CellGrid& grid) final {
        canvas_.clear();
        paint(canvas_);
        canvas_.render(fb, grid);
    }

protected:
    virtual void build() = 0;                          // ttfx build()
    virtual void paint(CellCanvas& canvas) const = 0;  // Visible characters, ascending id.

    // Every character's final color: a 12-step gradient over the text box.
    std::vector<Rgb> final_colors(const std::vector<Rgb>& stops, Direction direction) const {
        const std::vector<Rgb> colors = spectrum(stops, {12});
        std::vector<Rgb> out;
        out.reserve(stage_.chars.size());
        for (const StageChar& c : stage_.chars) {
            out.push_back(color_at(colors, gradient_fraction(direction, stage_.text, c.col, c.row)));
        }
        return out;
    }

    // ttfx's diagonal groups, top-left first: keys col - row ascending, each group from
    // bottom-left to top-right. Groups are flattened into `members` with start offsets.
    struct DiagonalGroups {
        std::vector<uint16_t> members;  // Character indices.
        std::vector<uint16_t> starts;   // Group g is members[starts[g] .. starts[g + 1]).
        size_t size() const { return starts.empty() ? 0 : starts.size() - 1; }
    };

    DiagonalGroups diagonal_groups() const {
        DiagonalGroups groups;
        const int w = stage_.width;
        std::vector<int> at(static_cast<size_t>(w * stage_.height), -1);  // Character index per cell.
        for (size_t i = 0; i < stage_.chars.size(); ++i) {
            const StageChar& c = stage_.chars[i];
            at[static_cast<size_t>((c.row - 1) * w + (c.col - 1))] = static_cast<int>(i);
        }
        const TextBox& t = stage_.text;
        for (int key = t.left - t.top; key <= t.right - t.bottom; ++key) {
            const size_t before = groups.members.size();
            for (int row = t.bottom; row <= t.top; ++row) {
                const int col = key + row;
                if (col < 1 || col > w) continue;
                const int i = at[static_cast<size_t>((row - 1) * w + (col - 1))];
                if (i >= 0) groups.members.push_back(static_cast<uint16_t>(i));
            }
            if (groups.members.size() == before) continue;  // ttfx drops empty groups.
            groups.starts.push_back(static_cast<uint16_t>(before));
        }
        groups.starts.push_back(static_cast<uint16_t>(groups.members.size()));
        return groups;
    }

    // True while any character is in ttfx's active set (moving, or mid-scene).
    template <typename Char>
    static bool any_active(const std::vector<Char>& chars) {
        for (const Char& c : chars) {
            if (c.active) return true;
        }
        return false;
    }

    Stage stage_;
    Rng rng_;

private:
    CellCanvas canvas_;
};

}  // namespace ofx
