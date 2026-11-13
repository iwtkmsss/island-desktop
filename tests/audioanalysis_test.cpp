#include "borrowed/audioanalysis.h"
#include <QtTest>
#include <limits>
#include <vector>

using namespace IslandAudio;

class AudioAnalysisTest : public QObject {
    Q_OBJECT
    static std::vector<float> tone(float hz, float amplitude = 0.2f, std::size_t frames = 4096) {
        std::vector<float> samples(frames * 2);
        for (std::size_t i = 0; i < frames; ++i) {
            const float value = amplitude * std::sin(2 * std::numbers::pi_v<float> * hz * static_cast<float>(i) / 48000);
            samples[i * 2] = samples[i * 2 + 1] = value;
        }
        return samples;
    }
    static float decoded(const void *sample, SampleKind kind) {
        return decodeSample(static_cast<const std::byte *>(sample), kind);
    }
private slots:
    void pcmConversionPreservesSignAndFullScale() {
        const std::array<std::int16_t, 3> pcm16{-32768, 0, 32767};
        QCOMPARE(decoded(&pcm16[0], SampleKind::Signed16), -1.0f);
        QCOMPARE(decoded(&pcm16[1], SampleKind::Signed16), 0.0f);
        QVERIFY(decoded(&pcm16[2], SampleKind::Signed16) > 0.999f);
        const std::array<std::uint8_t, 3> low24{0, 0, 0x80}, high24{0xff, 0xff, 0x7f};
        QCOMPARE(decoded(low24.data(), SampleKind::Signed24), -1.0f);
        QVERIFY(decoded(high24.data(), SampleKind::Signed24) > 0.999f);
        const std::int32_t low32 = std::numeric_limits<std::int32_t>::min();
        QCOMPARE(decoded(&low32, SampleKind::Signed32), -1.0f);
        const std::array<std::uint8_t, 3> pcm8{0, 128, 255};
        QCOMPARE(decoded(&pcm8[0], SampleKind::Unsigned8), -1.0f);
        QCOMPARE(decoded(&pcm8[1], SampleKind::Unsigned8), 0.0f);
        QVERIFY(decoded(&pcm8[2], SampleKind::Unsigned8) > 0.99f);
    }
    void nonFiniteFloatSamplesCannotPoisonTheEnvelope() {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        const float clipped = 100;
        QCOMPARE(decoded(&nan, SampleKind::Float32), 0.0f);
        QCOMPARE(decoded(&infinity, SampleKind::Float64), 0.0f);
        QCOMPARE(decoded(&clipped, SampleKind::Float32), 1.0f);
    }
    void silenceDoesNotBecomeAnAutoGainSpectrum() {
        Analyzer analyzer;
        QVERIFY(analyzer.configure({SampleKind::Float32, 48000, 2, 8}));
        QVERIFY(analyzer.feed(nullptr, 8192, true));
        QCOMPARE(analyzer.rawRms(), 0.0f);
        QCOMPARE(analyzer.level(), 0.0f);
        for (const auto value : analyzer.bands()) QCOMPARE(value, 0.0f);
    }
    void silentPacketsSettleExactlyAfterAudibleSignal() {
        Analyzer analyzer;
        QVERIFY(analyzer.configure({SampleKind::Float32, 48000, 2, 8}));
        const auto samples = tone(1000);
        analyzer.feed(samples.data(), static_cast<std::uint32_t>(samples.size() / 2));
        QVERIFY(analyzer.level() > 0.2f);
        for (int packet = 0; packet < 200; ++packet) analyzer.feed(nullptr, 480, true);
        QCOMPARE(analyzer.rawRms(), 0.0f);
        QCOMPARE(analyzer.level(), 0.0f);
        for (const auto value : analyzer.bands()) QCOMPARE(value, 0.0f);
    }
    void beatsReleaseBeforeTheNextBeat() {
        Analyzer analyzer;
        QVERIFY(analyzer.configure({SampleKind::Float32, 48000, 2, 8}));
        const auto samples = tone(110, 0.6f, 4800);
        analyzer.feed(samples.data(), 4800);
        QVERIFY(analyzer.level() > 0.9f);
        for (int packet = 0; packet < 16; ++packet) analyzer.feed(nullptr, 480, true);
        QVERIFY(analyzer.level() < 0.08f);
        for (const auto band : analyzer.bands()) QVERIFY(band < 0.03f);
    }
    void lowAndHighTonesAppearInDifferentFrequencyBands() {
        Analyzer low, high;
        QVERIFY(low.configure({SampleKind::Float32, 48000, 2, 8}));
        QVERIFY(high.configure({SampleKind::Float32, 48000, 2, 8}));
        const auto bass = tone(220), treble = tone(6000);
        low.feed(bass.data(), static_cast<std::uint32_t>(bass.size() / 2));
        high.feed(treble.data(), static_cast<std::uint32_t>(treble.size() / 2));
        const auto lowPeak = std::max_element(low.bands().begin(), low.bands().end());
        const auto highPeak = std::max_element(high.bands().begin(), high.bands().end());
        QVERIFY(lowPeak - low.bands().begin() < highPeak - high.bands().begin());
        QVERIFY(*lowPeak > 0.2f);
        QVERIFY(*highPeak > 0.2f);
        QVERIFY(low.level() > 0.2f && high.level() > 0.2f);
    }
    void missingPacketsDecayToZeroAndResetClearsHistory() {
        Analyzer analyzer;
        analyzer.configure({SampleKind::Float32, 48000, 2, 8});
        const auto samples = tone(1000);
        analyzer.feed(samples.data(), static_cast<std::uint32_t>(samples.size() / 2));
        QVERIFY(analyzer.level() > 0.2f);
        analyzer.decay(2);
        QCOMPARE(analyzer.level(), 0.0f);
        for (const auto value : analyzer.bands()) QCOMPARE(value, 0.0f);
        analyzer.reset();
        analyzer.feed(nullptr, 1024, true);
        for (const auto value : analyzer.bands()) QCOMPARE(value, 0.0f);
    }
    void blockPaddingAndUnsupportedFormatsAreHandled() {
        Analyzer padded;
        QVERIFY(padded.configure({SampleKind::Signed16, 48000, 1, 4}));
        std::array<std::int16_t, 2048> data{};
        for (std::size_t i = 0; i < data.size(); i += 2) { data[i] = 16384; data[i + 1] = -32768; }
        padded.feed(data.data(), 1024);
        QVERIFY(padded.rawRms() > 0.49f && padded.rawRms() < 0.51f);
        QVERIFY(!padded.configure({SampleKind::Float32, 48000, 0, 8}));
        QVERIFY(!padded.configure({SampleKind::Float32, 48000, 2, 1}));
        QVERIFY(!padded.feed(data.data(), 1024));
        QCOMPARE(padded.level(), 0.0f);
    }
};

QTEST_GUILESS_MAIN(AudioAnalysisTest)
#include "audioanalysis_test.moc"
