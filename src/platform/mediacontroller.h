#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVariantList>

struct MediaSnapshot {
    bool available = false, playing = false;
    bool canToggle = false, canNext = false, canPrevious = false, canSeek = false;
    QString title, artist, album, sourceId, sourceName, error;
    qint64 positionMs = 0, durationMs = 0, startMs = 0;
    double rate = 1;
    QVariantList sources;
    QImage artwork;
};
Q_DECLARE_METATYPE(MediaSnapshot)

class MediaController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed)
    Q_PROPERTY(QString artworkUrl READ artworkUrl NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool canToggle READ canToggle NOTIFY changed)
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY positionChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QString selectedSource READ selectedSource NOTIFY changed)
public:
    explicit MediaController(const QString &settingsFile, QObject *parent = nullptr);
    ~MediaController() override;
    bool available() const { return snapshot_.available; }
    bool playing() const { return snapshot_.playing; }
    QString title() const { return snapshot_.title; }
    QString artist() const { return snapshot_.artist; }
    QString album() const { return snapshot_.album; }
    QString sourceName() const { return snapshot_.sourceName; }
    QString artworkUrl() const { return artworkUrl_; }
    QString error() const { return commandError_.isEmpty() ? snapshot_.error : commandError_; }
    bool canToggle() const { return snapshot_.canToggle; }
    bool canNext() const { return snapshot_.canNext; }
    bool canPrevious() const { return snapshot_.canPrevious; }
    bool canSeek() const { return snapshot_.canSeek; }
    bool busy() const { return busy_ || sourcePending_; }
    qint64 positionMs() const;
    qint64 durationMs() const { return snapshot_.durationMs; }
    double progress() const;
    QVariantList sources() const { return snapshot_.sources; }
    QString selectedSource() const { return preferredSource_; }
    QImage artwork() const;
    Q_INVOKABLE void selectSource(const QString &source);
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(double fraction);
signals:
    void changed();
    void positionChanged();
    void refreshRequested(QString preferred, quint64 generation);
    void commandRequested(QString source, QString action, qint64 position, quint64 generation);
private:
    void command(const QString &action, qint64 position = 0);
    void apply(quint64 generation, MediaSnapshot snapshot);
    QSettings settings_;
    QThread thread_;
    QObject *worker_ = nullptr;
    MediaSnapshot snapshot_;
    QString preferredSource_, artworkUrl_, commandError_;
    quint64 generation_ = 1, artworkRevision_ = 0;
    bool busy_ = false;
    bool sourcePending_ = false;
    QElapsedTimer receivedAt_;
    QTimer positionTicker_;
    mutable QMutex artworkMutex_;
    QImage artwork_;
};
