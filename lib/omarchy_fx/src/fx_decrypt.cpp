// decrypt: characters are typed in as flickering blocks, scrambled through random cipher
// symbols, then each resolves from white to gold. Port of ttfx's decrypt
// (src/fx/effects/decrypt.rs; defaults in src/effects/decrypt.rs).

#include <array>

#include "canvas_effect.h"

namespace ofx {
namespace {

constexpr int kTypingSpeed = 2;  // Characters per keystroke.
constexpr uint16_t kTypingBlocks[] = {0x2589, 0x2593, 0x2592, 0x2591};  // ▉ ▓ ▒ ░
constexpr int kFastFrames = 80;
constexpr int kDiscoveredFrames = 11;

// The 523 cipher symbols: printable ASCII, block elements, box drawing, Latin-1 and Latin
// Extended-A/B.
struct SymbolRange {
    uint16_t first;
    uint16_t count;
};
constexpr SymbolRange kCipherSymbols[] = {{33, 94}, {0x2588, 24}, {0x2500, 127}, {0x00AE, 278}};
constexpr uint32_t kCipherSymbolCount = 94 + 24 + 127 + 278;

// Palette: the three cipher greens, then the 11 "discovered" colors from white to gold.
constexpr uint8_t kCipherColors = 3;

enum class Scene : uint8_t { None, Typing, Fast, Slow, Discovered };

class Decrypt final : public CanvasEffect {
public:
    const char* name() const override { return "decrypt"; }

    bool step() override {
        const size_t n = chars_.size();
        if (!decrypting_) {
            if (typed_ < n || any_active(chars_)) {
                if (typed_ < n && rng_.randint(0, 100) <= 75) {
                    for (int k = 0; k < kTypingSpeed && typed_ < n; ++k, ++typed_) {
                        chars_[typed_].visible = true;
                        chars_[typed_].active = true;
                        activate(typed_, Scene::Typing);
                    }
                }
                update();
                return true;
            }
            for (size_t i = 0; i < n; ++i) {
                chars_[i].active = true;
                activate(i, Scene::Fast);
            }
            decrypting_ = true;  // ...and decrypting starts in this same frame.
        }
        if (!any_active(chars_)) return false;
        update();
        return true;
    }

protected:
    void build() override {
        palette_[0] = hex(0x008000);
        palette_[1] = hex(0x00cb00);
        palette_[2] = hex(0x00ff00);
        const std::vector<Rgb> discovered = spectrum({hex(0xffffff), hex(0xeda000)}, {10});
        for (int i = 0; i < kDiscoveredFrames; ++i) palette_[kCipherColors + i] = discovered[static_cast<size_t>(i)];

        chars_.assign(stage_.chars.size(), Char{});
        for (Char& c : chars_) c.cipher = static_cast<uint8_t>(rng_.below(kCipherColors));
        typed_ = 0;
        decrypting_ = false;
    }

    void paint(CellCanvas& canvas) const override {
        for (size_t i = 0; i < chars_.size(); ++i) {
            const Char& c = chars_[i];
            if (c.visible) canvas.put(stage_.chars[i].col, stage_.chars[i].row, c.symbol, palette_[c.color]);
        }
    }

private:
    struct Char {
        Scene scene = Scene::None;
        uint8_t frame = 0;     // Head frame of the running scene.
        uint8_t frames = 0;    // Frames in the running scene.
        uint8_t ticks = 0;     // Ticks already spent on the head frame.
        uint8_t duration = 1;  // Ticks the head frame lasts.
        uint8_t cipher = 0;    // Cipher color of the fast and slow scenes.
        uint8_t head_color = 0;
        uint8_t color = 0;  // Shown color, as a palette index.
        uint16_t head_symbol = ' ';
        uint16_t symbol = ' ';  // Shown symbol.
        bool visible = false;
        bool active = false;
    };

    uint16_t random_symbol() {
        uint32_t r = rng_.below(kCipherSymbolCount);
        for (const SymbolRange& range : kCipherSymbols) {
            if (r < range.count) return static_cast<uint16_t>(range.first + r);
            r -= range.count;
        }
        return '?';
    }

    // Draws the random parts of the head frame as it comes up. ttfx draws them all up
    // front, but every draw is independent, so the animation has the same distribution.
    void load_head(size_t i) {
        Char& c = chars_[i];
        switch (c.scene) {
            case Scene::Typing:
                c.head_symbol = c.frame < 4 ? kTypingBlocks[c.frame] : random_symbol();
                c.head_color = static_cast<uint8_t>(rng_.below(kCipherColors));
                c.duration = c.frame < 4 ? 2 : 1;
                break;
            case Scene::Fast:
                c.head_symbol = random_symbol();
                c.head_color = c.cipher;
                c.duration = 2;
                break;
            case Scene::Slow:
                c.head_symbol = random_symbol();
                c.head_color = c.cipher;
                c.duration = static_cast<uint8_t>(rng_.randint(0, 100) <= 30 ? rng_.randrange(35, 60)
                                                                             : rng_.randrange(3, 6));
                break;
            case Scene::Discovered:
                c.head_symbol = static_cast<uint16_t>(stage_.chars[i].glyph);
                c.head_color = static_cast<uint8_t>(kCipherColors + c.frame);
                c.duration = 5;
                break;
            case Scene::None:
                break;
        }
    }

    // Starts a scene and shows its first frame at once, without ticking it.
    void activate(size_t i, Scene scene) {
        Char& c = chars_[i];
        c.scene = scene;
        c.frame = 0;
        c.ticks = 0;
        switch (scene) {
            case Scene::Typing: c.frames = 5; break;
            case Scene::Fast: c.frames = kFastFrames; break;
            case Scene::Slow: c.frames = static_cast<uint8_t>(rng_.randint(1, 15)); break;
            case Scene::Discovered: c.frames = kDiscoveredFrames; break;
            case Scene::None: c.frames = 0; break;
        }
        load_head(i);
        c.symbol = c.head_symbol;
        c.color = c.head_color;
    }

    // One tick of the character's scene. As with ttfx's events, finishing the fast scene
    // starts the slow one, and finishing that starts the discovered fade, in the same tick.
    void tick(size_t i) {
        Char& c = chars_[i];
        if (c.scene == Scene::None) return;
        c.symbol = c.head_symbol;
        c.color = c.head_color;
        if (++c.ticks < c.duration) return;
        c.ticks = 0;
        if (++c.frame < c.frames) {
            load_head(i);
            return;
        }
        const Scene finished = c.scene;
        c.scene = Scene::None;
        if (finished == Scene::Fast) {
            activate(i, Scene::Slow);
        } else if (finished == Scene::Slow) {
            activate(i, Scene::Discovered);
        }
    }

    void update() {
        for (size_t i = 0; i < chars_.size(); ++i) {
            if (chars_[i].active) tick(i);
        }
        for (Char& c : chars_) {
            if (c.scene == Scene::None) c.active = false;
        }
    }

    std::vector<Char> chars_;
    std::array<Rgb, kCipherColors + kDiscoveredFrames> palette_{};
    size_t typed_ = 0;
    bool decrypting_ = false;
};

}  // namespace

std::unique_ptr<Effect> make_decrypt() { return std::make_unique<Decrypt>(); }

}  // namespace ofx
