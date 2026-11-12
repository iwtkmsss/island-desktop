#include "audiocontroller.h"
#include "borrowed/audioanalysis.h"
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>
#include <array>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;
#endif

namespace {
struct AudioFrame {
    quint64 generation = 0;
    bool available = false, active = false;
    float level = 0;
    std::array<float, IslandAudio::Analyzer::bandCount> bands{};
    QString error;
};

#ifdef Q_OS_WIN
bool decodeFormat(const WAVEFORMATEX *wave, IslandAudio::Format &format) {
    if (!wave) return false;
    bool floating = wave->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool pcm = wave->wFormatTag == WAVE_FORMAT_PCM;
    if (wave->wFormatTag == WAVE_FORMAT_EXTENSIBLE
        && wave->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto &extended = *reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(wave);
        constexpr GUID pcmSubtype = {WAVE_FORMAT_PCM, 0, 0x0010, {0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
        constexpr GUID floatSubtype = {WAVE_FORMAT_IEEE_FLOAT, 0, 0x0010, {0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
        floating = IsEqualGUID(extended.SubFormat, floatSubtype);
        pcm = IsEqualGUID(extended.SubFormat, pcmSubtype);
    }
    if (floating && wave->wBitsPerSample == 32) format.kind = IslandAudio::SampleKind::Float32;
    else if (floating && wave->wBitsPerSample == 64) format.kind = IslandAudio::SampleKind::Float64;
    else if (pcm && wave->wBitsPerSample == 8) format.kind = IslandAudio::SampleKind::Unsigned8;
    else if (pcm && wave->wBitsPerSample == 16) format.kind = IslandAudio::SampleKind::Signed16;
    else if (pcm && wave->wBitsPerSample == 24) format.kind = IslandAudio::SampleKind::Signed24;
    else if (pcm && wave->wBitsPerSample == 32) format.kind = IslandAudio::SampleKind::Signed32;
    else return false;
    format.channels = wave->nChannels;
    format.sampleRate = wave->nSamplesPerSec;
    format.blockAlign = wave->nBlockAlign;
    return format.valid();
}

std::wstring deviceId(IMMDevice *device) {
    LPWSTR text = nullptr;
    if (!device || FAILED(device->GetId(&text))) return {};
    std::wstring id = text ? text : L"";
    CoTaskMemFree(text);
    return id;
}

struct CaptureSession {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX *wave = nullptr;
    UINT32 bufferFrames = 0;
    bool started = false;
    ~CaptureSession() {
        if (started && client) client->Stop();
        if (wave) CoTaskMemFree(wave);
    }
};
#endif
} // namespace

class AudioCaptureThread final : public QThread {
public:
    AudioCaptureThread() { setObjectName("SystemAudioLoopback"); }
    ~AudioCaptureThread() override {
        requestInterruption(); wake(); wait();
    }
    quint64 setCaptureEnabled(bool enabled) {
        QMutexLocker lock(&controlMutex_);
        enabled_ = enabled;
        ++generation_;
        wake_.wakeAll();
        return generation_;
    }
    void wake() { QMutexLocker lock(&controlMutex_); wake_.wakeAll(); }
    AudioFrame latest() const { QMutexLocker lock(&frameMutex_); return frame_; }

protected:
    void run() override {
#ifdef Q_OS_WIN
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool apartmentReady = SUCCEEDED(initialized);
#endif
        while (!isInterruptionRequested()) {
            quint64 generation;
            {
                QMutexLocker lock(&controlMutex_);
                while (!enabled_ && !isInterruptionRequested()) wake_.wait(&controlMutex_);
                if (isInterruptionRequested()) break;
                generation = generation_;
            }
#ifdef Q_OS_WIN
            if (!apartmentReady) {
                publishUnavailable(generation, QStringLiteral("Не вдалося підключити аналіз системного звуку"));
                waitForChange(1000);
                continue;
            }
            capture(generation);
            if (wanted(generation)) waitForChange(200);
#else
            publishUnavailable(generation, QStringLiteral("Аналіз звуку цієї платформи ще не підключено"));
            waitForChange(1000);
#endif
        }
#ifdef Q_OS_WIN
        if (apartmentReady) CoUninitialize();
#endif
    }

private:
    bool wanted(quint64 generation) const {
        QMutexLocker lock(&controlMutex_);
        return enabled_ && generation == generation_ && !isInterruptionRequested();
    }
    void waitForChange(unsigned long milliseconds) {
        QMutexLocker lock(&controlMutex_);
        if (!isInterruptionRequested()) wake_.wait(&controlMutex_, milliseconds);
    }
    void publish(AudioFrame frame) { QMutexLocker lock(&frameMutex_); frame_ = std::move(frame); }
    void publishUnavailable(quint64 generation, QString error = {}) {
        AudioFrame frame; frame.generation = generation; frame.error = std::move(error); publish(std::move(frame));
    }
#ifdef Q_OS_WIN
    void capture(quint64 generation) {
        // Shared default-render loopback setup and packet draining are adapted
        // from AudioThreadProc in the supplied mod, lines 7794–7935.
        // Copyright (c) 2026 devcode90, MIT; third_party/dynamic-island/LICENSE.
        CaptureSession session;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&session.enumerator));
        if (SUCCEEDED(result)) result = session.enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &session.device);
        if (SUCCEEDED(result)) result = session.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void **>(session.client.GetAddressOf()));
        if (SUCCEEDED(result)) result = session.client->GetMixFormat(&session.wave);
        IslandAudio::Format format;
        if (SUCCEEDED(result) && !decodeFormat(session.wave, format)) {
            publishUnavailable(generation, QStringLiteral("Формат звуку цього пристрою поки не підтримується"));
            waitForChange(1500);
            return;
        }
        if (SUCCEEDED(result)) result = session.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, session.wave, nullptr);
        if (SUCCEEDED(result)) result = session.client->GetBufferSize(&session.bufferFrames);
        if (SUCCEEDED(result)) result = session.client->GetService(IID_PPV_ARGS(&session.capture));
        if (SUCCEEDED(result)) result = session.client->Start();
        if (FAILED(result)) {
            publishUnavailable(generation, QStringLiteral("Системний звук зараз недоступний. Очікую пристрій виведення."));
            waitForChange(1000);
            return;
        }
        session.started = true;
        const auto endpointId = deviceId(session.device.Get());
        IslandAudio::Analyzer analyzer;
        analyzer.configure(format);
        QElapsedTimer clock; clock.start();
        qint64 lastTick = 0, lastSignal = -1000, lastDeviceCheck = 0;
        AudioFrame frame; frame.generation = generation; frame.available = true;
        publish(frame);
        bool failed = false;
        while (wanted(generation)) {
            const auto now = clock.elapsed();
            const double elapsed = std::clamp(static_cast<double>(now - lastTick) / 1000, 0.0, 0.25);
            lastTick = now;
            UINT32 packetFrames = 0;
            result = session.capture->GetNextPacketSize(&packetFrames);
            if (FAILED(result)) { failed = true; break; }
            bool received = false;
            // Bound work per pass so shutdown/disable does not wait for an
            // indefinitely growing packet queue on a malfunctioning endpoint.
            for (int packets = 0; packetFrames > 0 && packets < 64 && wanted(generation); ++packets) {
                BYTE *data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                result = session.capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(result)) { failed = true; break; }
                if (result == AUDCLNT_S_BUFFER_EMPTY) break;
                if (frames > session.bufferFrames || (frames > 0 && !data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))) {
                    session.capture->ReleaseBuffer(frames);
                    failed = true; break;
                }
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) analyzer.reset();
                analyzer.feed(data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
                if (analyzer.rawRms() > 0.0005f) lastSignal = now;
                received = true;
                result = session.capture->ReleaseBuffer(frames);
                if (FAILED(result)) { failed = true; break; }
                result = session.capture->GetNextPacketSize(&packetFrames);
                if (FAILED(result)) { failed = true; break; }
            }
            if (failed) break;
            if (!received) analyzer.decay(elapsed);
            frame.level = analyzer.level();
            frame.bands = analyzer.bands();
            frame.active = now - lastSignal < 250 && frame.level > 0.002f;
            publish(frame);
            if (now - lastDeviceCheck >= 1000) {
                lastDeviceCheck = now;
                ComPtr<IMMDevice> current;
                DWORD state = 0;
                if (FAILED(session.enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &current))
                    || deviceId(current.Get()) != endpointId
                    || FAILED(session.device->GetState(&state)) || state != DEVICE_STATE_ACTIVE) break;
            }
            // No capture buffers are held during a wait. In silence, 50 ms is
            // sufficient; audible playback is processed at roughly 50 Hz.
            waitForChange(now - lastSignal > 400 ? 50 : 20);
        }
        if (wanted(generation)) {
            publishUnavailable(generation, failed
                ? QStringLiteral("Перепідключення до системного звуку…") : QString());
            // Let CaptureSession release all stream resources before reconnecting.
        }
    }
#endif
    mutable QMutex controlMutex_, frameMutex_;
    QWaitCondition wake_;
    bool enabled_ = false;
    quint64 generation_ = 0;
    AudioFrame frame_;
};

AudioController::AudioController(QObject *parent) : QObject(parent), worker_(std::make_unique<AudioCaptureThread>()) {
    bands_.reserve(IslandAudio::Analyzer::bandCount);
    for (std::size_t i = 0; i < IslandAudio::Analyzer::bandCount; ++i) bands_.append(0.0);
    ticker_.setInterval(33);
    connect(&ticker_, &QTimer::timeout, this, &AudioController::poll);
    worker_->start();
}
AudioController::~AudioController() {
    ticker_.stop();
    worker_->requestInterruption();
    worker_->wake();
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
