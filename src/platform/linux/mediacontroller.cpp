#include "mediacontroller.h"
#include "accentcolor.h"
#include "dbushelpers.h"
#include <QDBusObjectPath>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QBuffer>
#include <QImageReader>
#include <QUrl>
#include <QFile>
#include <QScopedValueRollback>
#include <cmath>

class MediaWorker final : public QObject {
    Q_OBJECT
public:
    explicit MediaWorker(QString preferred) : preferred_(std::move(preferred)) {}
public slots:
    void initialize() {
        auto *timer = new QTimer(this); timer->setInterval(900);
        connect(timer, &QTimer::timeout, this, &MediaWorker::poll); timer->start(); poll();
    }
    void refresh(QString preferred, quint64 generation) { preferred_ = std::move(preferred); generation_ = generation; poll(); }
    void execute(QString source, QString action, qint64 position, quint64 generation) {
        if (generation != generation_ || source != displayedSource_) { emit commandDone(generation, false); return; }
        QString method; QVariantList args;
        if (action == "toggle") method = "PlayPause";
        else if (action == "next") method = "Next";
        else if (action == "previous") method = "Previous";
        else if (action == "seek" && !trackId_.isEmpty()) {
            method = "SetPosition"; args = {QVariant::fromValue(QDBusObjectPath(trackId_)), QVariant::fromValue(position * 1000)};
        }
        bool success = false;
        if (!method.isEmpty()) success = LinuxBus::call(QDBusConnection::sessionBus(), source, "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player", method, args).type() == QDBusMessage::ReplyMessage;
        emit commandDone(generation, success); poll();
    }
signals:
    void snapshotReady(quint64 generation, MediaSnapshot snapshot);
    void commandDone(quint64 generation, bool success);
private:
    QImage cover(const QString &uri) {
        if (uri == artworkKey_ && (!artwork_.isNull() || (artRetry_.isValid() && artRetry_.elapsed() < 5000))) return artwork_;
        artRetry_.start();
        artworkKey_ = uri; artwork_ = {};
        const QUrl url(uri);
        QByteArray bytes;
        if (url.isLocalFile()) {
            QFile file(url.toLocalFile());
            if (file.size() <= 4 * 1024 * 1024 && file.open(QIODevice::ReadOnly)) bytes = file.readAll();
        } else if (url.scheme() == "https" || url.scheme() == "http") {
            QNetworkAccessManager network;
            QNetworkRequest request(url); request.setTransferTimeout(1500);
            auto *reply = network.get(request);
            QEventLoop loop; QTimer deadline, interrupted;
            deadline.setSingleShot(true); deadline.setInterval(1500);
            connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            connect(reply, &QNetworkReply::readyRead, &loop, [&] {
                bytes += reply->readAll();
                if (bytes.size() > 4 * 1024 * 1024) { bytes.clear(); reply->abort(); loop.quit(); }
            });
            interrupted.setInterval(50);
            connect(&interrupted, &QTimer::timeout, &loop, [&] { if (QThread::currentThread()->isInterruptionRequested()) loop.quit(); });
            deadline.start(); interrupted.start(); loop.exec();
            if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) bytes.clear();
            reply->abort();
        }
        QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer);
        const auto size = reader.size();
        if (size.isValid() && size.width() <= 8192 && size.height() <= 8192) {
            reader.setScaledSize(size.scaled(512, 512, Qt::KeepAspectRatio)); artwork_ = reader.read();
        }
        return artwork_;
    }
    void poll() {
        if (polling_ || QThread::currentThread()->isInterruptionRequested()) return;
        QScopedValueRollback<bool> guard(polling_, true);
        const auto pollGeneration = generation_;
        const auto bus = QDBusConnection::sessionBus();
        MediaSnapshot snapshot;
        snapshot.sources.append(QVariantMap{{"id", ""}, {"label", QStringLiteral("Автоматично")}});
        const auto namesReply = LinuxBus::call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames");
        if (namesReply.type() != QDBusMessage::ReplyMessage) {
            snapshot.error = QStringLiteral("Сеанс D-Bus недоступний"); emit snapshotReady(pollGeneration, snapshot); return;
        }
        auto names = namesReply.arguments().value(0).toStringList(); names.sort();
        QVariantMap selected; QString selectedId;
        for (const auto &name : names) {
            if (QThread::currentThread()->isInterruptionRequested()) return;
            if (!name.startsWith("org.mpris.MediaPlayer2.")) continue;
            const auto properties = LinuxBus::properties(bus, name, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player");
            if (properties.isEmpty()) continue;
            const auto label = name.mid(23);
            snapshot.sources.append(QVariantMap{{"id", name}, {"label", label}});
            if ((!preferred_.isEmpty() && name == preferred_) || (preferred_.isEmpty()
                && (selected.isEmpty() || (properties.value("PlaybackStatus") == "Playing" && selected.value("PlaybackStatus") != "Playing")
                    || (properties.value("PlaybackStatus") == selected.value("PlaybackStatus") && name == displayedSource_)))) {
                selected = properties; selectedId = name;
            }
        }
        if (selected.isEmpty()) {
            if (!preferred_.isEmpty()) snapshot.error = QStringLiteral("Вибраний плеєр недоступний. Запустіть його або оберіть «Автоматично».");
            displayedSource_.clear(); trackId_.clear(); artworkKey_.clear(); artwork_ = {};
            emit snapshotReady(pollGeneration, snapshot); return;
        }
        displayedSource_ = selectedId;
        const auto metadata = LinuxBus::map(selected.value("Metadata"));
        snapshot.available = true; snapshot.sourceId = selectedId; snapshot.sourceName = selectedId.mid(23);
        snapshot.title = metadata.value("xesam:title").toString();
        snapshot.artist = metadata.value("xesam:artist").toStringList().join(", ");
        snapshot.album = metadata.value("xesam:album").toString();
        snapshot.playing = selected.value("PlaybackStatus") == "Playing";
        const bool controls = selected.value("CanControl").toBool();
        snapshot.canToggle = controls && selected.value(snapshot.playing ? "CanPause" : "CanPlay").toBool();
        snapshot.canNext = controls && selected.value("CanGoNext").toBool();
        snapshot.canPrevious = controls && selected.value("CanGoPrevious").toBool();
        trackId_ = qvariant_cast<QDBusObjectPath>(metadata.value("mpris:trackid")).path();
        snapshot.canSeek = controls && selected.value("CanSeek").toBool() && !trackId_.isEmpty()
            && trackId_ != "/org/mpris/MediaPlayer2/TrackList/NoTrack";
        snapshot.durationMs = std::max<qint64>(0, metadata.value("mpris:length").toLongLong() / 1000);
        snapshot.positionMs = std::clamp(selected.value("Position").toLongLong() / 1000, qint64(0), snapshot.durationMs);
        const auto rate = selected.value("Rate", 1.0).toDouble();
        snapshot.rate = std::isfinite(rate) && rate > 0 && rate < 16 ? rate : 1;
        // Account for cover download time without turning paused tracks into motion.
        QElapsedTimer elapsed; elapsed.start();
        snapshot.artwork = cover(metadata.value("mpris:artUrl").toString());
        snapshot.accent = accentFromImage(snapshot.artwork);
        if (snapshot.playing) snapshot.positionMs = std::min(snapshot.durationMs,
            snapshot.positionMs + qint64(elapsed.elapsed() * snapshot.rate));
        emit snapshotReady(pollGeneration, snapshot);
    }
    QString preferred_, displayedSource_, trackId_, artworkKey_;
    QImage artwork_;
    QElapsedTimer artRetry_;
    quint64 generation_ = 1;
    bool polling_ = false;
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
    thread_.setObjectName("LinuxMpris");
    thread_.start();
}
MediaController::~MediaController() {
    thread_.requestInterruption();
    thread_.quit();
    thread_.wait(); // D-Bus calls are bounded; cover downloads check interruption.
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
    const bool accentDiffers = snapshot.accent != snapshot_.accent;
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
    if (accentDiffers) emit accentChanged();
}
void MediaController::selectSource(const QString &source) {
    if (source == preferredSource_) return;
    preferredSource_ = source;
    settings_.setValue("media/source", source); settings_.sync();
    ++generation_; busy_ = false; sourcePending_ = true; commandError_.clear();
    // Both shell sizes must stop showing the old source immediately. Keep the
    // chooser's options while the worker resolves the new preferred session.
    const auto sources = snapshot_.sources;
    const bool hadAccent = snapshot_.accent.isValid();
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
    if (hadAccent) emit accentChanged();
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
