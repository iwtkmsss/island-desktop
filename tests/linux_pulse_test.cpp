#include "audiocontroller.h"
#include "volumecontroller.h"
#include <QtTest>
#include <pulse/simple.h>
#include <thread>
#include <numbers>
#include <cmath>
class LinuxPulseTest final : public QObject {
    Q_OBJECT
private slots:
    void loopbackResponseTime() {
        QVERIFY(qEnvironmentVariable("PULSE_SERVER").contains("island-pulse-test."));
        VolumeController volume;
        QTRY_VERIFY_WITH_TIMEOUT(volume.available(), 6000);
        volume.setVolume(100);
        QTRY_COMPARE_WITH_TIMEOUT(volume.volume(), 100, 4000);
        AudioController audio;
        audio.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(audio.available(), 6000);
        QElapsedTimer clock;
        clock.start();
        std::atomic<qint64> onset{-1}, offset{-1};
        std::jthread playback([&](std::stop_token stop) {
            const pa_sample_spec format{PA_SAMPLE_FLOAT32NE, 48000, 2};
            const pa_buffer_attr buffer{uint32_t(-1), 1920 * 8, 0, 480 * 8, uint32_t(-1)};
            int error = 0;
            auto *stream = pa_simple_new(nullptr, "island-latency-test", PA_STREAM_PLAYBACK,
                nullptr, "pulses", &format, nullptr, &buffer, &error);
            if (!stream) return;
            std::array<float, 960> samples{};
            // The tone starts after 8 s of silence: a capture loop that reads
            // slower than real time lets the server queue grow ~50 ms per second,
            // which a tone right after connecting would not reveal.
            for (int chunk = 0; chunk < 920 && !stop.stop_requested(); ++chunk) {
                const bool tone = chunk >= 800 && chunk < 850;
                for (size_t i = 0; i < samples.size()/2; ++i)
                    samples[2*i] = samples[2*i+1] = tone
                        ? float(0.6 * std::sin(2 * std::numbers::pi * 110 * (chunk * 480 + i) / 48000)) : 0;
                if (chunk == 800 || chunk == 850) {
                    const auto latency = pa_simple_get_latency(stream, &error);
                    if (latency == pa_usec_t(-1)) break;
                    // Reference the scheduled output, including queued playback.
                    (chunk == 800 ? onset : offset).store(clock.elapsed() + qint64(latency / 1000));
                }
                if (pa_simple_write(stream, samples.data(), sizeof(samples), &error) < 0) break;
            }
            pa_simple_free(stream);
        });
        auto waitUntil = [&](auto predicate, int timeout) {
            QElapsedTimer deadline; deadline.start();
            while (!predicate() && deadline.elapsed() < timeout) QTest::qWait(5);
            return predicate();
        };
        QVERIFY(waitUntil([&] { return onset.load() >= 0 && audio.level() > 0.2; }, 14000));
        const auto attack = clock.elapsed() - onset.load();
        QVERIFY(waitUntil([&] { return offset.load() >= 0 && clock.elapsed() >= offset.load() && audio.level() < 0.05; }, 6000));
        const auto release = clock.elapsed() - offset.load();
        qInfo() << "Loopback response (ms), attack:" << attack << "release:" << release;
        QVERIFY2(attack >= -40 && attack < 150, qPrintable(QString::number(attack)));
        QVERIFY2(release >= 0 && release < 350, qPrintable(QString::number(release)));
        playback.request_stop();
    }
    void privateServerVolumeAndLoopback() {
        // This suite may only write to the disposable server started by the wrapper.
        QVERIFY(qEnvironmentVariable("PULSE_SERVER").contains("island-pulse-test."));
        VolumeController volume;
        AudioController audio;
        QSignalSpy events(&volume, &VolumeController::volumeEvent);
        QTRY_VERIFY_WITH_TIMEOUT(volume.available(), 6000);
        QCOMPARE(events.count(), 0);
        QVERIFY(volume.deviceName().contains("island", Qt::CaseInsensitive));
        volume.setVolume(35); QTRY_COMPARE_WITH_TIMEOUT(volume.volume(), 35, 4000);
        QVERIFY(events.count() >= 1);
        volume.toggleMute(); QTRY_VERIFY(volume.muted());
        volume.toggleMute(); QTRY_VERIFY(!volume.muted());
        audio.setEnabled(true); QTRY_VERIFY_WITH_TIMEOUT(audio.available(), 6000);
        QVERIFY(!audio.active()); QCOMPARE(audio.level(), 0);
        std::atomic_bool started = false;
        std::jthread playback([&](std::stop_token stop) {
            const pa_sample_spec format{PA_SAMPLE_FLOAT32NE, 48000, 2}; int error = 0;
            auto *stream = pa_simple_new(nullptr, "island-test-tone", PA_STREAM_PLAYBACK, nullptr, "test", &format, nullptr, nullptr, &error);
            if (!stream) return;
            started = true;
            std::array<float, 960> samples{}; qint64 frame = 0;
            while (!stop.stop_requested()) {
                for (size_t i = 0; i < samples.size() / 2; ++i, ++frame) {
                    samples[2*i] = samples[2*i+1] = float(0.6 * std::sin(2 * std::numbers::pi * 110 * frame / 48000));
                }
                if (pa_simple_write(stream, samples.data(), samples.size() * sizeof(float), &error) < 0) break;
            }
            pa_simple_free(stream);
        });
        QTRY_VERIFY_WITH_TIMEOUT(started.load(), 4000);
        QTRY_VERIFY_WITH_TIMEOUT(audio.active(), 6000);
        QVERIFY(audio.level() > 0);
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            for (const auto &band : audio.bands()) if (band.toDouble() > 0) return true;
            return false;
        })(), 3000);
        playback.request_stop(); playback.join();
        QTRY_VERIFY_WITH_TIMEOUT(!audio.active(), 6000);
        audio.setEnabled(false); QVERIFY(!audio.available()); QCOMPARE(audio.level(), 0);
        for (const auto &band : audio.bands()) QCOMPARE(band.toDouble(), 0);
        // A virtual sink may renegotiate its graph after the first capture is
        // destroyed. Allow its idle transition, while keeping shutdown bounded.
        audio.setEnabled(true); QTRY_VERIFY_WITH_TIMEOUT(audio.available(), 15000);
        QVERIFY(!audio.active()); audio.setEnabled(false);
    }
};
QTEST_GUILESS_MAIN(LinuxPulseTest)
#include "linux_pulse_test.moc"
