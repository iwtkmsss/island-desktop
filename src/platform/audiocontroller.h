#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>

class AudioCaptureThread;

class AudioController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(qreal level READ level NOTIFY changed)
    Q_PROPERTY(QVariantList bands READ bands NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
public:
    explicit AudioController(QObject *parent = nullptr);
    ~AudioController() override;
    bool available() const { return available_; }
    bool active() const { return active_; }
    qreal level() const { return level_; }
    QVariantList bands() const { return bands_; }
    QString error() const { return error_; }
    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled);
signals:
    void changed();
    void enabledChanged();
private:
    void poll();
    void clear();
    std::unique_ptr<AudioCaptureThread> worker_;
    QTimer ticker_;
    bool enabled_ = false, available_ = false, active_ = false;
    qreal level_ = 0;
    QVariantList bands_;
    QString error_;
    quint64 generation_ = 0;
};
