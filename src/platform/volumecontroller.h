#pragma once

#include <QObject>
#include <QThread>
#include <memory>

struct VolumeSharedState;
struct VolumeSnapshot {
    bool available = false;
    int volume = 0;
    bool muted = false;
    QString deviceName;
    QString error;
    bool operator==(const VolumeSnapshot &) const = default;
};
Q_DECLARE_METATYPE(VolumeSnapshot)

class VolumeController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit VolumeController(QObject *parent = nullptr);
    ~VolumeController() override;

    bool available() const { return snapshot_.available; }
    int volume() const { return snapshot_.volume; }
    bool muted() const { return snapshot_.muted; }
    QString deviceName() const { return snapshot_.deviceName; }
    QString error() const { return snapshot_.error; }

    Q_INVOKABLE void setVolume(int percent);
    Q_INVOKABLE void toggleMute();

signals:
    void changed();
    void volumeEvent(int volume, bool muted);

private:
    void apply(VolumeSnapshot snapshot, bool event);
    std::shared_ptr<VolumeSharedState> shared_;
    QThread thread_;
    VolumeSnapshot snapshot_;
};
