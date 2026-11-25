#ifndef GROK_AUDIO_H
#define GROK_AUDIO_H

#if defined(OMARCHY_GROK)

#include <cstddef>
#include <cstdint>

// Microphone and speaker for the Grok voice chat on the Cardputer and Cardputer-Adv, through
// the ESP-IDF I2S driver, never waiting. They take turns: on the original Cardputer they
// share a pin, and keeping the microphone off while Grok talks stops Grok hearing itself.
// For the Grok task only, except loudness() and set_volume().
namespace grokaudio {

constexpr uint32_t kSampleRate = 16000;

bool listen(); // Microphone on, speaker off.
bool speak();  // Speaker on, microphone off.
void stop();   // Both off.

// Copies up to max samples of microphone audio that have arrived into out.
size_t readMic(int16_t *out, size_t max);

// Queues as many samples for the speaker as fit right now; returns how many it took.
size_t play(const int16_t *samples, size_t count);

// True once everything queued has been heard.
bool playedOut();

// Speaker volume in percent: 100 plays Grok's voice as it comes, above that boosts it,
// with a soft limit instead of harsh clipping. Up to kMaxVolume.
constexpr int kMaxVolume = 400;
void setVolume(int percent);

// How loud the sound playing or being heard is, 0..1.
float loudness();

// How sure xAI's speech detection should be before it counts sound as speech, for this
// Cardputer's microphone; 0 keeps xAI's default.
float speechThreshold();

} // namespace grokaudio

#endif
#endif
