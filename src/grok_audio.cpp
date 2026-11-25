#include "grok_audio.h"

#if defined(OMARCHY_GROK)

#include "idf/launcher_platform.h"
#include <Wire.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <driver/i2s_pdm.h>
#include <driver/i2s_std.h>

extern bool UseTCA8418; // boards/m5stack-cardputer/interface.cpp: true on the Cardputer-Adv.

namespace grokaudio {
namespace {

// Both models use bit clock 41, word select 43, speaker data 42 and microphone data 46.
// The original Cardputer has a PDM microphone (clock on 43) and an NS4168 amplifier; the
// Adv has an ES8311 codec at I2C address 0x18 on the keyboard's bus (Wire, pins 8 and 9).
constexpr gpio_num_t kBitClock = GPIO_NUM_41;
constexpr gpio_num_t kWordSelect = GPIO_NUM_43;
constexpr gpio_num_t kSpeakerData = GPIO_NUM_42;
constexpr gpio_num_t kMicData = GPIO_NUM_46;
constexpr uint8_t kCodecAddress = 0x18;
constexpr int kDmaFrames = 240;
constexpr int kDmaBuffers = 6;
constexpr uint32_t kQueuedMs = kDmaFrames * kDmaBuffers * 1000 / kSampleRate; // 90 ms.
// M5Unified records these microphones on the right channel with an overall gain of about 8
// (magnification 16 over 2x oversampling). Twice that here: at 8 the Cardputers barely heard
// each other's speakers across a table.
constexpr int32_t kMicGain = 16;

// ES8311 register settings from M5Unified's Cardputer-Adv callbacks (M5Unified.inl).
constexpr uint8_t kCodecSpeakerOn[][2] = {
    {0x00, 0x80}, {0x01, 0xB5}, {0x02, 0x18}, {0x0D, 0x01},
    {0x12, 0x00}, {0x13, 0x10}, {0x32, 0xBF}, {0x37, 0x08},
};
constexpr uint8_t kCodecMicOn[][2] = {
    {0x00, 0x80}, {0x01, 0xBA}, {0x02, 0x18}, {0x0D, 0x01},
    {0x0E, 0x02}, {0x14, 0x10}, {0x17, 0xBF}, {0x1C, 0x6A},
};
constexpr uint8_t kCodecOff[][2] = {{0x0D, 0xFC}, {0x0E, 0x6A}, {0x00, 0x00}};

enum class Mode { Off, Mic, Speaker };
Mode mode = Mode::Off;
i2s_chan_handle_t channel = nullptr;
std::atomic<int> volume{200};
std::atomic<float> level{0};
std::atomic<uint32_t> levelMs{0};
uint32_t lastPlayMs = 0;

void setLevel(float value) {
    level.store(value);
    levelMs.store(launcherMillis());
}

template <size_t N> void writeCodec(const uint8_t (&regs)[N][2]) {
    for (const auto &reg : regs) {
        Wire.beginTransmission(kCodecAddress);
        Wire.write(reg[0]);
        Wire.write(reg[1]);
        Wire.endTransmission();
    }
}

void closeChannel() {
    if (!channel) return;
    i2s_channel_disable(channel);
    i2s_del_channel(channel);
    channel = nullptr;
}

bool openChannel(bool speaker) {
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cfg.dma_desc_num = kDmaBuffers;
    cfg.dma_frame_num = kDmaFrames;
    cfg.auto_clear = true; // Silence, not the last sound, when nothing is queued.
    if (i2s_new_channel(&cfg, speaker ? &channel : nullptr, speaker ? nullptr : &channel) != ESP_OK) {
        channel = nullptr;
        return false;
    }
    esp_err_t err;
    if (!speaker && !UseTCA8418) {
        i2s_pdm_rx_config_t pdm = {
            .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kSampleRate),
            .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {.clk = kWordSelect, .din = kMicData, .invert_flags = {.clk_inv = false}},
        };
        pdm.slot_cfg.slot_mask = I2S_PDM_SLOT_RIGHT;
        err = i2s_channel_init_pdm_rx_mode(channel, &pdm);
    } else {
        i2s_std_config_t std = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT, speaker ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO
            ),
            .gpio_cfg =
                {.mclk = I2S_GPIO_UNUSED,
                 .bclk = kBitClock,
                 .ws = kWordSelect,
                 .dout = speaker ? kSpeakerData : I2S_GPIO_UNUSED,
                 .din = speaker ? I2S_GPIO_UNUSED : kMicData,
                 .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}},
        };
        if (!speaker) std.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
        err = i2s_channel_init_std_mode(channel, &std);
    }
    if (err != ESP_OK || i2s_channel_enable(channel) != ESP_OK) {
        launcherConsolePrintf("Grok audio: I2S %s failed (%d)\n", speaker ? "speaker" : "mic", err);
        closeChannel();
        return false;
    }
    return true;
}

// Boosted sample with a soft knee: unchanged up to 70% of full scale, then eased towards
// full scale instead of being cut off, so a loud setting sounds louder, not broken.
int16_t boost(int16_t sample, float gain) {
    constexpr float kKnee = 0.7f;
    const float x = sample * gain / 32768.0f;
    const float a = std::fabs(x);
    const float y = a <= kKnee ? a : kKnee + (1 - kKnee) * std::tanh((a - kKnee) / (1 - kKnee));
    return static_cast<int16_t>(std::copysign(y, x) * 32767.0f);
}

float rmsLevel(const int16_t *s, size_t n) {
    if (n == 0) return 0;
    float sum = 0;
    for (size_t i = 0; i < n; ++i) sum += static_cast<float>(s[i]) * s[i];
    return std::min(1.0f, std::sqrt(sum / n) / 6000.0f); // Speech sits around 1000-6000.
}

} // namespace

bool listen() {
    if (mode == Mode::Mic) return true;
    stop();
    if (!openChannel(false)) return false;
    if (UseTCA8418) writeCodec(kCodecMicOn);
    mode = Mode::Mic;
    return true;
}

bool speak() {
    if (mode == Mode::Speaker) return true;
    stop();
    if (!openChannel(true)) return false;
    if (UseTCA8418) writeCodec(kCodecSpeakerOn);
    mode = Mode::Speaker;
    lastPlayMs = 0;
    return true;
}

void stop() {
    if (mode == Mode::Off) return;
    if (UseTCA8418) writeCodec(kCodecOff);
    closeChannel();
    mode = Mode::Off;
    setLevel(0);
}

size_t readMic(int16_t *out, size_t max) {
    if (mode != Mode::Mic || max == 0) return 0;
    size_t bytes = 0;
    i2s_channel_read(channel, out, max * sizeof(int16_t), &bytes, 0);
    const size_t n = bytes / sizeof(int16_t);
    for (size_t i = 0; i < n; ++i) {
        out[i] = static_cast<int16_t>(std::clamp<int32_t>(out[i] * kMicGain, INT16_MIN, INT16_MAX));
    }
    if (n) setLevel(rmsLevel(out, n));
    return n;
}

size_t play(const int16_t *samples, size_t count) {
    if (mode != Mode::Speaker || count == 0) return 0;
    constexpr size_t kBlock = 160; // 10 ms.
    int16_t frames[kBlock * 2];
    const float gain = volume.load() / 100.0f;
    size_t taken = 0;
    while (taken < count) {
        const size_t n = std::min(count - taken, kBlock);
        for (size_t i = 0; i < n; ++i) {
            const int16_t v = boost(samples[taken + i], gain);
            frames[2 * i] = frames[2 * i + 1] = v; // The same sound on both channels.
        }
        size_t written = 0;
        i2s_channel_write(channel, frames, n * 2 * sizeof(int16_t), &written, 0);
        const size_t done = written / (2 * sizeof(int16_t));
        if (done) setLevel(rmsLevel(samples + taken, done));
        taken += done;
        if (done < n) break; // The driver is full: try again later.
    }
    if (taken) lastPlayMs = launcherMillis();
    return taken;
}

bool playedOut() { return mode != Mode::Speaker || launcherMillis() - lastPlayMs > kQueuedMs + 30; }

void setVolume(int percent) { volume.store(std::clamp(percent, 0, kMaxVolume)); }

// xAI's default is 0.85. A Cardputer's small microphone hears another Cardputer's speaker
// faintly, so xAI is asked to count speech it is only 50% sure of (accepted when tested).
float speechThreshold() { return 0.5f; }

float loudness() { return launcherMillis() - levelMs.load() < 150 ? level.load() : 0.0f; }

} // namespace grokaudio

#endif
