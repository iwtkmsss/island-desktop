#pragma once
#include "volumecontroller.h"
#include "volumechangetracker.h"
#include "borrowed/audioanalysis.h"
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <pulse/pulseaudio.h>
#include <atomic>
#include <functional>

// The Pulse protocol works with both PulseAudio and PipeWire's pulse server.
// All native objects belong to this thread. Commands are coalesced, not queued.
struct LinuxAudioFrame {
    quint64 generation = 0;
    bool available = false, active = false;
    float level = 0;
    std::array<float, IslandAudio::Analyzer::bandCount> bands{};
    QString error;
};
class PulseWorker : public QThread {
public:
    std::function<void(VolumeSnapshot, bool)> volumeReady;
    ~PulseWorker() override { requestInterruption(); wait(); }
    void volume(int value) { QMutexLocker lock(&mutex_); pendingVolume_ = std::clamp(value, 0, 100); }
    void mute() { QMutexLocker lock(&mutex_); pendingMute_ = !pendingMute_; }
    quint64 enableCapture(bool enabled) {
        QMutexLocker lock(&mutex_); captureEnabled_ = enabled; return ++generation_;
    }
    LinuxAudioFrame latest() const { QMutexLocker lock(&mutex_); return frame_; }
protected:
    void run() override;
private:
    static void serverInfo(pa_context *, const pa_server_info *, void *);
    static void sinkInfo(pa_context *, const pa_sink_info *, int, void *);
    static void readStream(pa_stream *, size_t, void *);
    void publishVolume(VolumeSnapshot snapshot, bool event = false) { if (volumeReady) volumeReady(std::move(snapshot), event); }
    void publishAudio(LinuxAudioFrame frame) { QMutexLocker lock(&mutex_); frame_ = std::move(frame); }
    void stopStream();
    void updateStream(bool enabled, quint64 generation);
    void operation(pa_operation *op) { if (op) pa_operation_unref(op); }
    mutable QMutex mutex_;
    int pendingVolume_ = -1;
    bool pendingMute_ = false, captureEnabled_ = false;
    quint64 generation_ = 0, streamGeneration_ = 0;
    LinuxAudioFrame frame_;
    pa_mainloop *loop_ = nullptr;
    pa_context *context_ = nullptr;
    pa_stream *stream_ = nullptr;
    QString sink_, monitor_, streamMonitor_;
    pa_cvolume channels_{};
    bool muted_ = false, dirty_ = true;
    VolumeChangeTracker tracker_;
    IslandAudio::Analyzer analyzer_;
    QElapsedTimer clock_;
    qint64 lastSignal_ = -1000, lastRead_ = 0, lastUpdate_ = 0;
    // ISLAND_AUDIO_TRACE=1: once per second, worst backlog/gaps to stderr.
    bool trace_ = qEnvironmentVariableIsSet("ISLAND_AUDIO_TRACE");
    qint64 traceAt_ = 0, traceLoop_ = 0, traceMaxLoop_ = 0, traceFrames_ = 0, traceMaxBacklog_ = 0, traceMaxLatency_ = 0;
};
