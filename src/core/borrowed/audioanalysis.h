#pragma once
// Adapted from SampleAudioAmplitude, SpectrumAnalyzer and PushWaveformSample in
// the user-supplied dynamic-island-for-windows 1.4.0 (lines 5800–6050).
// Copyright (c) 2026 devcode90, MIT. See third_party/dynamic-island/LICENSE.
// The Hann window, radix-2 FFT, logarithmic bands, AGC and attack/release shaping
// are retained; sample decoding, stream resets and time-based decay are extended.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>

namespace IslandAudio {

enum class SampleKind { Unsigned8, Signed16, Signed24, Signed32, Float32, Float64 };

struct Format {
    SampleKind kind = SampleKind::Float32;
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t blockAlign = 0;

    std::size_t sampleBytes() const {
        switch (kind) {
        case SampleKind::Unsigned8: return 1;
        case SampleKind::Signed16: return 2;
        case SampleKind::Signed24: return 3;
        case SampleKind::Signed32: case SampleKind::Float32: return 4;
        case SampleKind::Float64: return 8;
        }
        return 0;
    }
    bool valid() const {
        return sampleBytes() > 0 && channels > 0 && channels <= 32 && sampleRate >= 8000 && sampleRate <= 384000
            && blockAlign >= channels * sampleBytes() && blockAlign <= 4096;
    }
};

inline float decodeSample(const std::byte *sample, SampleKind kind) {
    if (!sample) return 0;
    float value = 0;
    switch (kind) {
    case SampleKind::Unsigned8:
        value = (static_cast<int>(std::to_integer<std::uint8_t>(*sample)) - 128) / 128.0f;
        break;
    case SampleKind::Signed16: {
        std::int16_t raw; std::memcpy(&raw, sample, sizeof(raw)); value = raw / 32768.0f;
        break;
    }
    case SampleKind::Signed24: {
        const std::uint32_t bits = std::to_integer<std::uint8_t>(sample[0])
            | (std::uint32_t(std::to_integer<std::uint8_t>(sample[1])) << 8)
            | (std::uint32_t(std::to_integer<std::uint8_t>(sample[2])) << 16);
        // The subtraction sign-extends without depending on signed right-shift.
        const auto raw = static_cast<std::int32_t>(bits ^ 0x800000u) - 0x800000;
        value = raw / 8388608.0f;
        break;
    }
    case SampleKind::Signed32: {
        std::int32_t raw; std::memcpy(&raw, sample, sizeof(raw));
        value = static_cast<float>(raw / 2147483648.0);
        break;
    }
    case SampleKind::Float32: std::memcpy(&value, sample, sizeof(value)); break;
    case SampleKind::Float64: {
        double raw; std::memcpy(&raw, sample, sizeof(raw));
        value = static_cast<float>(raw);
        break;
    }
    }
    return std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0;
}

class Analyzer {
public:
    static constexpr std::size_t bandCount = 8;
    static constexpr std::size_t windowSize = 1024;
    static constexpr std::size_t hopSize = 512;

    Analyzer() {
        for (std::size_t i = 0; i < windowSize; ++i) {
            window_[i] = 0.5f - 0.5f * std::cos(2 * std::numbers::pi_v<float> * static_cast<float>(i) / (windowSize - 1));
            std::size_t reverse = 0;
            for (int bit = 0; bit < 10; ++bit)
                if (i & (std::size_t(1) << bit)) reverse |= std::size_t(1) << (9 - bit);
            bitReverse_[i] = static_cast<std::uint16_t>(reverse);
        }
        for (std::size_t i = 0; i < windowSize / 2; ++i) {
            const float angle = -2 * std::numbers::pi_v<float> * static_cast<float>(i) / windowSize;
            twiddleCos_[i] = std::cos(angle); twiddleSin_[i] = std::sin(angle);
        }
    }

    bool configure(Format format) {
        if (!format.valid()) { configured_ = false; reset(); return false; }
        format_ = format; configured_ = true; reset();
        const float maximum = std::min(14000.0f, format.sampleRate * 0.45f);
        const float ratio = std::pow(maximum / 50.0f, 1.0f / (bandCount - 1));
        const float halfStep = std::sqrt(ratio);
        const float binHz = static_cast<float>(format.sampleRate) / windowSize;
        constexpr int lastBin = static_cast<int>(windowSize / 2) - 1;
        for (std::size_t band = 0; band < bandCount; ++band) {
            const float centreHz = 50.0f * std::pow(ratio, static_cast<float>(band));
            const int centre = static_cast<int>(std::lround(centreHz / binHz));
            const int first = std::min(static_cast<int>(std::floor(centreHz / halfStep / binHz)), centre);
            const int last = std::max(static_cast<int>(std::ceil(centreHz * halfStep / binHz)), centre + 1);
            lowBin_[band] = std::clamp(first, 1, lastBin);
            highBin_[band] = std::clamp(last, lowBin_[band] + 1, lastBin + 1);
        }
        return true;
    }

    void reset() {
        ring_.fill(0); bands_.fill(0); write_ = sinceHop_ = 0;
        gain_ = 0.04f; level_ = rawRms_ = 0;
    }

    bool feed(const void *data, std::uint32_t frames, bool silent = false) {
        if (!configured_ || frames == 0) return false;
        if (!data && !silent) return false;
        const auto *bytes = static_cast<const std::byte *>(data);
        double squared = 0;
        bool produced = false;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            float mono = 0;
            for (std::uint16_t channel = 0; channel < format_.channels; ++channel) {
                const float sample = silent ? 0 : decodeSample(bytes + static_cast<std::size_t>(frame) * format_.blockAlign
                    + static_cast<std::size_t>(channel) * format_.sampleBytes(), format_.kind);
                mono += sample; squared += static_cast<double>(sample) * sample;
            }
            ring_[write_] = mono / format_.channels;
            write_ = (write_ + 1) % windowSize;
            if (++sinceHop_ >= hopSize) { sinceHop_ = 0; analyze(); produced = true; }
        }
        rawRms_ = static_cast<float>(std::sqrt(squared / (static_cast<double>(frames) * format_.channels)));
        const float amplitude = std::clamp(rawRms_ * 4.0f, 0.0f, 1.0f);
        const float seconds = static_cast<float>(frames) / format_.sampleRate;
        // Fast attack and a ~56 ms release keep separate beats readable.
        const float residual = amplitude > level_ ? 0.3f : 0.75f;
        level_ = amplitude + (level_ - amplitude) * std::pow(residual, seconds / 0.016f);
        if (level_ < 0.0001f) level_ = 0;
        return produced;
    }

    void decay(double seconds) {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        rawRms_ = 0;
        level_ *= static_cast<float>(std::pow(0.75, seconds / 0.016));
        const auto fade = static_cast<float>(std::pow(0.75, seconds * format_.sampleRate / hopSize));
        for (auto &band : bands_) { band *= fade; if (band < 0.0001f) band = 0; }
        if (level_ < 0.0001f) level_ = 0;
    }

    float level() const { return level_; }
    float rawRms() const { return rawRms_; }
    const std::array<float, bandCount> &bands() const { return bands_; }

private:
    void fft(std::array<float, windowSize> &real, std::array<float, windowSize> &imaginary) const {
        for (std::size_t i = 0; i < windowSize; ++i) {
            const auto reversed = bitReverse_[i];
            if (reversed > i) { std::swap(real[i], real[reversed]); std::swap(imaginary[i], imaginary[reversed]); }
        }
        for (std::size_t length = 2; length <= windowSize; length <<= 1) {
            const auto half = length / 2, step = windowSize / length;
            for (std::size_t i = 0; i < windowSize; i += length) {
                for (std::size_t k = 0; k < half; ++k) {
                    const auto a = i + k, b = a + half;
                    const float wr = twiddleCos_[k * step], wi = twiddleSin_[k * step];
                    const float tr = real[b] * wr - imaginary[b] * wi;
                    const float ti = real[b] * wi + imaginary[b] * wr;
                    real[b] = real[a] - tr; imaginary[b] = imaginary[a] - ti;
                    real[a] += tr; imaginary[a] += ti;
                }
            }
        }
    }

    void analyze() {
        std::array<float, windowSize> real{}, imaginary{};
        for (std::size_t i = 0; i < windowSize; ++i) real[i] = ring_[(write_ + i) % windowSize] * window_[i];
        fft(real, imaginary);
        std::array<float, bandCount> raw{};
        float peak = 0;
        for (std::size_t band = 0; band < bandCount; ++band) {
            float sum = 0, maximum = 0;
            for (int bin = lowBin_[band]; bin < highBin_[band]; ++bin) {
                const float power = real[bin] * real[bin] + imaginary[bin] * imaginary[bin];
                sum += power; maximum = std::max(maximum, power);
            }
            const float power = 0.5f * sum / (highBin_[band] - lowBin_[band]) + 0.5f * maximum;
            const float tilt = 1 + 2.2f * static_cast<float>(band) / (bandCount - 1);
            raw[band] = std::sqrt(power) * (4.0f / windowSize) * tilt;
            peak = std::max(peak, raw[band]);
        }
        gain_ = std::max(peak, gain_ * 0.9985f);
        for (std::size_t band = 0; band < bandCount; ++band) {
            float value = std::sqrt(std::clamp(raw[band] / std::max(gain_, 0.02f), 0.0f, 1.0f));
            value *= std::clamp(peak * 40.0f, 0.0f, 1.0f);
            bands_[band] += (value > bands_[band] ? 0.65f : 0.25f) * (value - bands_[band]);
            if (bands_[band] < 0.0001f) bands_[band] = 0;
        }
    }

    Format format_;
    bool configured_ = false;
    std::array<float, windowSize> ring_{}, window_{};
    std::array<float, windowSize / 2> twiddleCos_{}, twiddleSin_{};
    std::array<std::uint16_t, windowSize> bitReverse_{};
    std::array<int, bandCount> lowBin_{}, highBin_{};
    std::array<float, bandCount> bands_{};
    std::size_t write_ = 0, sinceHop_ = 0;
    float gain_ = 0.04f, level_ = 0, rawRms_ = 0;
};

} // namespace IslandAudio
