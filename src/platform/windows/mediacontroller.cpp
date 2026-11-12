#include "mediacontroller.h"
#include "borrowed/mediatimeline.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <algorithm>
#include <chrono>
#include <cmath>
#ifdef Q_OS_WIN
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using PlaybackStatus = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

// Adapted from SelectActiveMediaSession, supplied mod 1.4.0, lines 4462–4511.
// Copyright (c) 2026 devcode90, MIT; third_party/dynamic-island/LICENSE.
static Session selectActiveSession(Manager const &manager, const std::wstring &preferred) {
    auto playing = [](auto const &candidate) {
        if (!candidate) return false;
        try { return candidate.GetPlaybackInfo().PlaybackStatus() == PlaybackStatus::Playing; }
        catch (...) { return false; }
    };
    Session current{nullptr};
    try { current = manager.GetCurrentSession(); } catch (...) {}
    if (!preferred.empty()) {
        try {
            for (auto const &candidate : manager.GetSessions())
                if (preferred == candidate.SourceAppUserModelId().c_str()) return candidate;
        } catch (...) {}
        return nullptr; // A manual choice must never silently control another player.
    }
    if (playing(current)) return current;
    try { for (auto const &candidate : manager.GetSessions()) if (playing(candidate)) return candidate; }
    catch (...) {}
    if (current) return current;
    try { for (auto const &candidate : manager.GetSessions()) return candidate; } catch (...) {}
    return nullptr;
}

template <typename Operation>
static bool waitFor(Operation const &operation, int timeoutMs = 1500) {
    QElapsedTimer deadline; deadline.start();
    while (deadline.elapsed() < timeoutMs && !QThread::currentThread()->isInterruptionRequested()) {
        // Repeated wait_for calls install Completed handlers repeatedly; some
        // players forbid that. Poll status on this worker with bounded waits.
        const auto status = operation.Status();
        if (status == winrt::Windows::Foundation::AsyncStatus::Completed) return true;
        if (status != winrt::Windows::Foundation::AsyncStatus::Started) {
            operation.GetResults(); // Propagate the WinRT error to the worker boundary.
            return false;
        }
        QThread::msleep(20);
    }
    operation.Cancel();
    return false;
}
#endif

static QString displayName(QString id) {
    if (id.contains("spotify", Qt::CaseInsensitive)) return "Spotify";
    if (id.contains("firefox", Qt::CaseInsensitive)) return "Firefox";
    if (id.contains("chrome", Qt::CaseInsensitive)) return "Chrome";
    if (id.contains("msedge", Qt::CaseInsensitive)) return "Edge";
    if (id.contains("vlc", Qt::CaseInsensitive)) return "VLC";
    if (id.contains("zunemusic", Qt::CaseInsensitive) || id.contains("mediaplayer", Qt::CaseInsensitive)) return "Медіапрогравач";
    id.replace('\\', '/');
    if (id.contains('/')) id = QFileInfo(id).baseName();
    if (id.contains('!')) id = id.section('!', -1);
    if (id.endsWith(".exe", Qt::CaseInsensitive)) id.chop(4);
    return id.isEmpty() ? QStringLiteral("Медіаплеєр") : id;
}

class MediaWorker final : public QObject {
    Q_OBJECT
public:
    explicit MediaWorker(QString preferred) : preferred_(std::move(preferred)) { clock_.start(); }
    ~MediaWorker() override {
#ifdef Q_OS_WIN
        manager_ = nullptr;
        if (apartment_) winrt::uninit_apartment();
#endif
    }
public slots:
    void initialize() {
#ifdef Q_OS_WIN
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment_ = true; }
        catch (...) { emit snapshotReady(generation_, unavailable()); return; }
#endif
        timer_ = new QTimer(this);
        timer_->setInterval(900);
        connect(timer_, &QTimer::timeout, this, &MediaWorker::poll);
        timer_->start();
        poll();
    }
    void refresh(QString preferred, quint64 generation) {
        preferred_ = std::move(preferred); generation_ = generation;
        timeline_.reset();
        poll();
    }
    void execute(QString source, QString action, qint64 position, quint64 generation) {
        bool success = false;
#ifdef Q_OS_WIN
        try {
            if (manager_) {
                // Resolve the exact displayed source. Never send a command to a newly
                // selected automatic source while metadata still shows the previous one.
                Session session{nullptr};
                for (auto const &candidate : manager_.GetSessions()) {
                    if (QString::fromWCharArray(candidate.SourceAppUserModelId().c_str()) == source) {
                        session = candidate; break;
                    }
                }
                if (session) {
                    winrt::Windows::Foundation::IAsyncOperation<bool> operation{nullptr};
                    if (action == "toggle") operation = session.TryTogglePlayPauseAsync();
                    else if (action == "next") operation = session.TrySkipNextAsync();
                    else if (action == "previous") operation = session.TrySkipPreviousAsync();
                    else if (action == "seek") operation = session.TryChangePlaybackPositionAsync(position * 10000);
                    if (operation && waitFor(operation)) success = operation.GetResults();
                    if (success) timeline_.reset();
                }
            }
        } catch (...) {}
#else
        Q_UNUSED(source) Q_UNUSED(action) Q_UNUSED(position)
#endif
        emit commandDone(generation, success);
        poll();
    }
signals:
    void snapshotReady(quint64 generation, MediaSnapshot snapshot);
    void commandDone(quint64 generation, bool success);
private:
    static MediaSnapshot unavailable() {
        MediaSnapshot snapshot;
        snapshot.error = QStringLiteral("Не вдалося підключитися до медіасесій Windows");
        return snapshot;
    }
    void poll() {
        if (QThread::currentThread()->isInterruptionRequested()) return;
        MediaSnapshot snapshot;
        snapshot.sources.append(QVariantMap{{"id", ""}, {"label", QStringLiteral("Автоматично")}});
#ifdef Q_OS_WIN
        try {
            if (!manager_) {
                auto request = Manager::RequestAsync();
                if (!waitFor(request)) { emit snapshotReady(generation_, unavailable()); return; }
                manager_ = request.GetResults();
            }
            for (auto const &session : manager_.GetSessions()) {
                const auto id = QString::fromWCharArray(session.SourceAppUserModelId().c_str());
                bool duplicate = false;
                for (const auto &entry : snapshot.sources) if (entry.toMap().value("id").toString() == id) duplicate = true;
                if (!duplicate) snapshot.sources.append(QVariantMap{{"id", id}, {"label", displayName(id)}});
            }
            auto session = selectActiveSession(manager_, preferred_.toStdWString());
            if (!session) {
                artworkKey_.clear();
                if (!preferred_.isEmpty()) snapshot.error = QStringLiteral("Вибраний плеєр зараз недоступний. Запустіть його або поверніться до автоматичного вибору.");
                emit snapshotReady(generation_, snapshot); return;
            }
            snapshot.sourceId = QString::fromWCharArray(session.SourceAppUserModelId().c_str());
            snapshot.sourceName = displayName(snapshot.sourceId);
            auto propertiesRequest = session.TryGetMediaPropertiesAsync();
            if (!waitFor(propertiesRequest)) { emit snapshotReady(generation_, unavailable()); return; }
            auto properties = propertiesRequest.GetResults();
            snapshot.available = true;
            snapshot.title = QString::fromWCharArray(properties.Title().c_str());
            snapshot.artist = QString::fromWCharArray(properties.Artist().c_str());
            snapshot.album = QString::fromWCharArray(properties.AlbumTitle().c_str());
            const auto playback = session.GetPlaybackInfo();
            snapshot.playing = playback.PlaybackStatus() == PlaybackStatus::Playing;
            const auto controls = playback.Controls();
            snapshot.canToggle = controls.IsPlayPauseToggleEnabled();
            snapshot.canNext = controls.IsNextEnabled();
            snapshot.canPrevious = controls.IsPreviousEnabled();
            snapshot.canSeek = controls.IsPlaybackPositionEnabled();
            if (const auto rate = playback.PlaybackRate()) {
                if (std::isfinite(rate.Value()) && rate.Value() > 0 && rate.Value() < 16) snapshot.rate = rate.Value();
            }
            if (const auto timeline = session.GetTimelineProperties()) {
                const auto start = timeline.StartTime().count() / 10000;
                snapshot.startMs = start;
                snapshot.durationMs = std::max<qint64>(0, timeline.EndTime().count() / 10000 - start);
                snapshot.positionMs = std::max<qint64>(0, timeline.Position().count() / 10000 - start);
                // Adapted from the supplied mod: Position is measured at LastUpdatedTime.
                const auto ageMs = (winrt::clock::now() - timeline.LastUpdatedTime()).count() / 10000;
                if (snapshot.playing && ageMs > 0 && ageMs < 10000)
                    snapshot.positionMs += static_cast<qint64>(ageMs * snapshot.rate);
                snapshot.positionMs = std::clamp(snapshot.positionMs, qint64(0), snapshot.durationMs);
            }
            const auto key = snapshot.sourceId + QChar(0x1f) + snapshot.title + QChar(0x1f) + snapshot.artist + QChar(0x1f) + snapshot.album;
            snapshot.positionMs = timeline_.accept(snapshot.positionMs, snapshot.durationMs, snapshot.playing, snapshot.rate, key, clock_.elapsed());
            if (key != artworkKey_) { artworkKey_ = key; artwork_ = {}; artRetry_.invalidate(); }
            if (artwork_.isNull() && (!artRetry_.isValid() || artRetry_.elapsed() > 5000)) {
                artRetry_.start();
                try {
                    if (const auto thumbnail = properties.Thumbnail()) {
                        auto open = thumbnail.OpenReadAsync();
                        if (waitFor(open, 1000)) {
                            const auto stream = open.GetResults();
                            const auto size = stream.Size();
                            if (size > 0 && size <= 4 * 1024 * 1024) {
                                winrt::Windows::Storage::Streams::DataReader reader(stream);
                                auto load = reader.LoadAsync(static_cast<uint32_t>(size));
                                if (waitFor(load, 1000)) {
                                    const auto count = load.GetResults();
                                    QByteArray bytes(static_cast<qsizetype>(count), Qt::Uninitialized);
                                    reader.ReadBytes(winrt::array_view<uint8_t>(reinterpret_cast<uint8_t *>(bytes.data()), reinterpret_cast<uint8_t *>(bytes.data()) + bytes.size()));
                                    QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
                                    QImageReader decoder(&buffer);
                                    const auto sizeHint = decoder.size();
                                    if (sizeHint.isValid() && sizeHint.width() <= 8192 && sizeHint.height() <= 8192) {
                                        decoder.setScaledSize(sizeHint.scaled(512, 512, Qt::KeepAspectRatio));
                                        artwork_ = decoder.read();
                                    }
                                }
                            }
                        }
                    }
                } catch (...) {} // A missing/invalid cover must not hide the actual track.
            }
            snapshot.artwork = artwork_;
        } catch (...) {
            snapshot = unavailable();
            manager_ = nullptr; // Reconnect after a player or media service disconnects.
        }
#else
        snapshot.error = QStringLiteral("Медіасесії цієї платформи ще не підключено");
#endif
        if (!QThread::currentThread()->isInterruptionRequested()) emit snapshotReady(generation_, snapshot);
    }
    QString preferred_, artworkKey_;
    QImage artwork_;
    QElapsedTimer artRetry_;
    QElapsedTimer clock_;
    MediaTimeline timeline_;
    quint64 generation_ = 1;
    QTimer *timer_ = nullptr;
#ifdef Q_OS_WIN
    Manager manager_{nullptr};
    bool apartment_ = false;
#endif
};

MediaController::MediaController(const QString &settingsFile, QObject *parent)
    : QObject(parent), settings_(settingsFile, QSettings::IniFormat) {
    preferredSource_ = settings_.value("media/source").toString();
    auto *worker = new MediaWorker(preferredSource_);
    worker_ = worker;
    worker->moveToThread(&thread_);
    connect(&thread_, &QThread::started, worker, &MediaWorker::initialize);
    connect(&thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &MediaController::refreshRequested, worker, &MediaWorker::refresh);
    connect(this, &MediaController::commandRequested, worker, &MediaWorker::execute);
    connect(worker, &MediaWorker::snapshotReady, this, &MediaController::apply);
    connect(worker, &MediaWorker::commandDone, this, [this](quint64 generation, bool success) {
        if (generation != generation_) return;
        busy_ = false;
        commandError_ = success ? QString() : QStringLiteral("Плеєр не виконав команду. Спробуйте ще раз.");
        emit changed();
    });
    receivedAt_.start();
    positionTicker_.setInterval(250);
    connect(&positionTicker_, &QTimer::timeout, this, &MediaController::positionChanged);
    thread_.setObjectName("WindowsMedia");
    thread_.start();
}
MediaController::~MediaController() {
    thread_.requestInterruption();
    thread_.quit();
    thread_.wait(); // WinRT waits are cancellable and check interruption every 20 ms.
}
qint64 MediaController::positionMs() const {
    const auto advance = snapshot_.playing ? static_cast<qint64>(receivedAt_.elapsed() * snapshot_.rate) : 0;
    return std::clamp(snapshot_.positionMs + advance, qint64(0), snapshot_.durationMs);
}
double MediaController::progress() const { return durationMs() > 0 ? static_cast<double>(positionMs()) / durationMs() : 0; }
QImage MediaController::artwork() const { QMutexLocker lock(&artworkMutex_); return artwork_; }
void MediaController::apply(quint64 generation, MediaSnapshot snapshot) {
    if (generation != generation_) return;
    sourcePending_ = false;
    const bool artChanged = snapshot.artwork.cacheKey() != snapshot_.artwork.cacheKey();
    snapshot_ = std::move(snapshot);
    receivedAt_.restart();
    if (artChanged) {
        QMutexLocker lock(&artworkMutex_);
        artwork_ = snapshot_.artwork;
        artworkUrl_ = artwork_.isNull() ? QString() : QStringLiteral("image://media/cover/%1").arg(++artworkRevision_);
    }
    if (snapshot_.playing && snapshot_.durationMs > 0) {
        if (!positionTicker_.isActive()) positionTicker_.start();
    } else positionTicker_.stop();
    emit changed(); emit positionChanged();
}
void MediaController::selectSource(const QString &source) {
    if (source == preferredSource_) return;
    preferredSource_ = source;
    settings_.setValue("media/source", source); settings_.sync();
    ++generation_; busy_ = false; sourcePending_ = true; commandError_.clear();
    // Both shell sizes must stop showing the old source immediately. Keep the
    // chooser's options while the worker resolves the new preferred session.
    const auto sources = snapshot_.sources;
    snapshot_ = {};
    snapshot_.sources = sources;
    {
        QMutexLocker lock(&artworkMutex_);
        artwork_ = {};
        artworkUrl_.clear();
    }
    positionTicker_.stop();
    receivedAt_.restart();
    emit changed();
    emit positionChanged();
    emit refreshRequested(source, generation_);
}
void MediaController::command(const QString &action, qint64 position) {
    if (!available() || busy()) return;
    busy_ = true; commandError_.clear(); emit changed();
    emit commandRequested(snapshot_.sourceId, action, position, generation_);
}
void MediaController::togglePlayback() { if (canToggle()) command("toggle"); }
void MediaController::next() { if (canNext()) command("next"); }
void MediaController::previous() { if (canPrevious()) command("previous"); }
void MediaController::seek(double fraction) {
    if (canSeek() && std::isfinite(fraction)) command("seek", snapshot_.startMs + static_cast<qint64>(std::clamp(fraction, 0.0, 1.0) * durationMs()));
}

#include "mediacontroller.moc"
