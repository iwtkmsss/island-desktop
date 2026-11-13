#include "pulseworker.h"
#include <cmath>
#include <utility>
#include <cstdio>

void PulseWorker::serverInfo(pa_context *context, const pa_server_info *info, void *data) {
    auto *self = static_cast<PulseWorker *>(data);
    if (!info || !info->default_sink_name) {
        self->sink_.clear(); self->monitor_.clear(); self->tracker_.reset();
        self->publishVolume({false, 0, false, {}, QStringLiteral("Немає системного аудіовиходу")});
        return;
    }
    self->operation(pa_context_get_sink_info_by_name(context, info->default_sink_name, sinkInfo, self));
}
void PulseWorker::sinkInfo(pa_context *, const pa_sink_info *info, int end, void *data) {
    auto *self = static_cast<PulseWorker *>(data);
    if (end < 0) {
        self->sink_.clear(); self->monitor_.clear(); self->tracker_.reset();
        self->publishVolume({false, 0, false, {}, QStringLiteral("Аудіовихід недоступний")});
    }
    if (!info) return;
    self->sink_ = QString::fromUtf8(info->name);
    self->monitor_ = QString::fromUtf8(info->monitor_source_name);
    self->channels_ = info->volume;
    self->muted_ = info->mute;
    const int percent = std::clamp(qRound(100.0 * pa_cvolume_avg(&info->volume) / PA_VOLUME_NORM), 0, 100);
    self->publishVolume({true, percent, bool(info->mute), QString::fromUtf8(info->description), {}},
        self->tracker_.observe(self->sink_, percent, info->mute));
}
void PulseWorker::stopStream() {
    if (!stream_) return;
    pa_stream_set_read_callback(stream_, nullptr, nullptr);
    pa_stream_disconnect(stream_); pa_stream_unref(stream_); stream_ = nullptr;
    streamMonitor_.clear(); analyzer_.reset(); lastSignal_ = -1000;
}
void PulseWorker::readStream(pa_stream *stream, size_t, void *data) {
    auto *self = static_cast<PulseWorker *>(data);
    if (self->trace_) {
        const auto readable = pa_stream_readable_size(stream);
        if (readable != size_t(-1)) self->traceMaxBacklog_ = std::max<qint64>(self->traceMaxBacklog_, qint64(readable) / 384);
        pa_usec_t latency = 0; int negative = 0;
        if (pa_stream_get_latency(stream, &latency, &negative) == 0 && !negative)
            self->traceMaxLatency_ = std::max<qint64>(self->traceMaxLatency_, qint64(latency / 1000));
    }
    // Bounded draining also bounds shutdown latency on a busy server.
    for (int packet = 0; packet < 32; ++packet) {
        const auto readable = pa_stream_readable_size(stream);
        if (!readable || readable == size_t(-1)) break;
        const void *bytes = nullptr; size_t count = 0;
        if (pa_stream_peek(stream, &bytes, &count) < 0 || !count) break;
        self->analyzer_.feed(bytes, count / (sizeof(float) * 2), bytes == nullptr);
        self->traceFrames_ += qint64(count / (sizeof(float) * 2));
        if (self->analyzer_.rawRms() > 0.0005f) self->lastSignal_ = self->clock_.elapsed();
        self->lastRead_ = self->clock_.elapsed();
        pa_stream_drop(stream);
    }
}
void PulseWorker::updateStream(bool enabled, quint64 generation) {
    if (!enabled || generation != streamGeneration_ || streamMonitor_ != monitor_) stopStream();
    streamGeneration_ = generation;
    LinuxAudioFrame frame; frame.generation = generation;
    if (!enabled) { publishAudio(frame); return; }
    if (!stream_ && !monitor_.isEmpty()) {
        const pa_sample_spec format{PA_SAMPLE_FLOAT32NE, 48000, 2};
        stream_ = pa_stream_new(context_, "Island Desktop output spectrum", &format, nullptr);
        if (stream_) {
            analyzer_.configure({IslandAudio::SampleKind::Float32, 48000, 2, 8});
            streamMonitor_ = monitor_;
            pa_stream_set_read_callback(stream_, readStream, this);
            // 10 ms deliveries, at most 80 ms queued: a visualizer must not
            // replay seconds of stale audio after a scheduling stall.
            pa_buffer_attr buffer{3840 * 8, uint32_t(-1), uint32_t(-1), uint32_t(-1), 480 * 8};
            if (pa_stream_connect_record(stream_, monitor_.toUtf8().constData(), &buffer,
                    pa_stream_flags_t(PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE
                        | (trace_ ? PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE : 0))) < 0) stopStream();
        }
    }
    if (stream_ && !PA_STREAM_IS_GOOD(pa_stream_get_state(stream_))) stopStream();
    frame.available = stream_ && pa_stream_get_state(stream_) == PA_STREAM_READY;
    const auto now = clock_.elapsed();
    if (frame.available) {
        if (now - lastRead_ > 60) analyzer_.decay((now - lastUpdate_) / 1000.0);
        frame.level = analyzer_.level(); frame.bands = analyzer_.bands();
        frame.active = clock_.elapsed() - lastSignal_ < 250 && frame.level > 0.002f;
    } else frame.error = QStringLiteral("Очікую локальний аудіовихід PipeWire/PulseAudio");
    lastUpdate_ = now;
    publishAudio(frame);
}
void PulseWorker::run() {
    clock_.start();
    while (!isInterruptionRequested()) {
        loop_ = pa_mainloop_new();
        context_ = pa_context_new(pa_mainloop_get_api(loop_), "Island Desktop");
        pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr);
        bool subscribed = false;
        qint64 lastQuery = -2000;
        while (!isInterruptionRequested()) {
            // Dispatch everything that is ready. One iteration reads at most one
            // pstream chunk, and a record packet needs several: a single iterate
            // per wake consumed ~45k of 48k frames/s, so the server-side queue grew
            // ~50 ms every second until its ~0.5 s limit (audio visibly late).
            for (int pass = 0; pass < 256 && pa_mainloop_iterate(loop_, 0, nullptr) > 0; ++pass) {}
            if (trace_) {
                const auto now = clock_.elapsed();
                if (traceLoop_) traceMaxLoop_ = std::max(traceMaxLoop_, now - traceLoop_);
                traceLoop_ = now;
                if (now - traceAt_ >= 1000) {
                    std::fprintf(stderr, "audio-trace t=%llds frames/s=%lld worker-gap-max=%lldms backlog-max=%lldms source-latency-max=%lldms\n",
                        (long long)(now / 1000), (long long)(traceFrames_ * 1000 / std::max<qint64>(1, now - traceAt_)),
                        (long long)traceMaxLoop_, (long long)traceMaxBacklog_, (long long)traceMaxLatency_);
                    traceAt_ = now; traceFrames_ = traceMaxLoop_ = traceMaxBacklog_ = traceMaxLatency_ = 0;
                }
            }
            const auto state = pa_context_get_state(context_);
            if (!PA_CONTEXT_IS_GOOD(state)) break;
            if (state == PA_CONTEXT_READY) {
                if (!subscribed) {
                    pa_context_set_subscribe_callback(context_, [](pa_context *, pa_subscription_event_type_t, uint32_t, void *data) {
                        static_cast<PulseWorker *>(data)->dirty_ = true;
                    }, this);
                    operation(pa_context_subscribe(context_, pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER), nullptr, nullptr));
                    subscribed = true; dirty_ = true;
                }
                if (dirty_ || clock_.elapsed() - lastQuery > 2000) {
                    dirty_ = false; lastQuery = clock_.elapsed();
                    operation(pa_context_get_server_info(context_, serverInfo, this));
                }
                int percent; bool toggle, enabled; quint64 generation;
                {
                    QMutexLocker lock(&mutex_);
                    percent = std::exchange(pendingVolume_, -1); toggle = std::exchange(pendingMute_, false);
                    enabled = captureEnabled_; generation = generation_;
                }
                if (!sink_.isEmpty()) {
                    if (percent >= 0) {
                        auto volume = channels_;
                        pa_cvolume_scale(&volume, pa_volume_t(percent * PA_VOLUME_NORM / 100));
                        operation(pa_context_set_sink_volume_by_name(context_, sink_.toUtf8().constData(), &volume, nullptr, nullptr));
                    }
                    if (toggle) operation(pa_context_set_sink_mute_by_name(context_, sink_.toUtf8().constData(), !muted_, nullptr, nullptr));
                }
                updateStream(enabled, generation);
            }
            // Wake as soon as the socket has data instead of sleeping blindly;
            // the timeout still services queued volume/mute requests.
            if (pa_mainloop_prepare(loop_, (stream_ ? 5 : 20) * 1000) >= 0 && pa_mainloop_poll(loop_) >= 0)
                pa_mainloop_dispatch(loop_);
        }
        stopStream();
        pa_context_disconnect(context_); pa_context_unref(context_); context_ = nullptr;
        pa_mainloop_free(loop_); loop_ = nullptr;
        sink_.clear(); monitor_.clear(); tracker_.reset();
        publishVolume({false, 0, false, {}, QStringLiteral("PipeWire/PulseAudio недоступний")});
        LinuxAudioFrame frame;
        { QMutexLocker lock(&mutex_); frame.generation = generation_; }
        frame.error = QStringLiteral("PipeWire/PulseAudio недоступний"); publishAudio(frame);
        for (int retry = 0; retry < 50 && !isInterruptionRequested(); ++retry) msleep(20);
    }
}
