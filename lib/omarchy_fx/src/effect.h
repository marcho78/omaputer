// The interface every screensaver effect implements, and the list of effects.
#pragma once

#include <cstdint>
#include <memory>

#include "logo.h"
#include "raster.h"

namespace ofx {

class Effect {
public:
    virtual ~Effect() = default;

    virtual const char* name() const = 0;

    // Prepares the effect to animate `logo` centered on a canvas of cols x rows cells.
    // The same seed always gives the same animation.
    virtual void begin(const Logo& logo, int cols, int rows, uint32_t seed) = 0;

    // Advances one frame; the device runs 120 a second, like Omarchy's screensaver.
    // Returns false once the effect has finished with the logo fully drawn.
    virtual bool step() = 0;

    // Draws the current frame a row of cells at a time into strip, showing each on sink.
    // Only rows that changed since the last draw are drawn (the first draw does them all), so
    // keep showing an effect on the same screen for its whole life. Nothing is drawn outside
    // the grid's rows, so that screen should start black.
    virtual void draw(Strip& strip, const CellGrid& grid, StripSink& sink) = 0;
};

int effect_count();
std::unique_ptr<Effect> make_effect(int index);  // 0 <= index < effect_count()

// The individual effects, ported from ttfx with its default options.
std::unique_ptr<Effect> make_decrypt();
std::unique_ptr<Effect> make_beams();
std::unique_ptr<Effect> make_rain();
std::unique_ptr<Effect> make_slide();
std::unique_ptr<Effect> make_wipe();
std::unique_ptr<Effect> make_middleout();

}  // namespace ofx
