#include "effect.h"

namespace ofx {
namespace {

using Factory = std::unique_ptr<Effect> (*)();
constexpr Factory kEffects[] = {make_decrypt, make_beams, make_rain, make_slide, make_wipe, make_middleout};

}  // namespace

int effect_count() { return static_cast<int>(sizeof(kEffects) / sizeof(kEffects[0])); }

std::unique_ptr<Effect> make_effect(int index) {
    if (index < 0 || index >= effect_count()) return nullptr;
    return kEffects[index]();
}

}  // namespace ofx
