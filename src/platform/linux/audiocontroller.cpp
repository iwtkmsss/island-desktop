#include "audiocontroller.h"
#include "pulseworker.h"
#include <cmath>
#include <cstdio>
#include <QElapsedTimer>
class AudioCaptureThread final : public PulseWorker {
public:
    quint64 setCaptureEnabled(bool enabled) { return enableCapture(enabled); }
};
AudioController::AudioController(QObject *parent) : QObject(parent), worker_(std::make_unique<AudioCaptureThread>()) {
    bands_.reserve(IslandAudio::Analyzer::bandCount);
    for (std::size_t i = 0; i < IslandAudio::Analyzer::bandCount; ++i) bands_.append(0.0);
    ticker_.setTimerType(Qt::PreciseTimer);
    ticker_.setInterval(16);
    connect(&ticker_, &QTimer::timeout, this, &AudioController::poll);
    worker_->start();
}
AudioController::~AudioController() {
    ticker_.stop();
    worker_->requestInterruption();

    worker_->wait();
}
void AudioController::clear() {
    available_ = active_ = false; level_ = 0; error_.clear();
    for (auto &value : bands_) value = 0.0;
}
void AudioController::setEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    generation_ = worker_->setCaptureEnabled(enabled);
    clear();
    if (enabled) ticker_.start(); else ticker_.stop();
    emit enabledChanged(); emit changed();
}
void AudioController::poll() {
    if (!enabled_) return;
    static const bool trace = qEnvironmentVariableIsSet("ISLAND_AUDIO_TRACE");
    if (trace) {
        static QElapsedTimer clock; static qint64 last = 0, worst = 0, reported = 0;
        if (!clock.isValid()) clock.start();
        const auto now = clock.elapsed();
        if (last) worst = std::max(worst, now - last);
        last = now;
        if (now - reported >= 1000) { std::fprintf(stderr, "audio-trace ui-poll-gap-max=%lldms\n", (long long)worst); worst = 0; reported = now; }
    }
    const auto frame = worker_->latest();
    if (frame.generation != generation_) return;
    bool updated = frame.available != available_ || frame.active != active_ || frame.error != error_
        || std::abs(static_cast<qreal>(frame.level) - level_) > 0.001 || (frame.level == 0 && level_ != 0);
    for (std::size_t i = 0; i < frame.bands.size(); ++i)
        updated = updated || std::abs(frame.bands[i] - bands_[static_cast<qsizetype>(i)].toDouble()) > 0.001
            || (frame.bands[i] == 0 && bands_[static_cast<qsizetype>(i)].toDouble() != 0);
    if (!updated) return;
    available_ = frame.available; active_ = frame.active; level_ = frame.level; error_ = frame.error;
    for (std::size_t i = 0; i < frame.bands.size(); ++i) bands_[static_cast<qsizetype>(i)] = frame.bands[i];
    emit changed();
}
