#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <memory>

class HotkeyEventFilter;

// Global hotkeys (toggle island, play/pause). Sequences are portable Qt text.
class HotkeyController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString toggleSequence READ toggleSequence WRITE setToggleSequence NOTIFY changed)
    Q_PROPERTY(QString playPauseSequence READ playPauseSequence WRITE setPlayPauseSequence NOTIFY changed)
    Q_PROPERTY(bool toggleActive READ toggleActive NOTIFY changed)
    Q_PROPERTY(bool playPauseActive READ playPauseActive NOTIFY changed)
    Q_PROPERTY(bool capturing READ capturing WRITE setCapturing NOTIFY changed)
public:
    explicit HotkeyController(const QString &settingsFile, QObject *parent = nullptr);
    ~HotkeyController() override;

    QString toggleSequence() const { return sequences_[0]; }
    QString playPauseSequence() const { return sequences_[1]; }
    bool toggleActive() const { return active_[0]; }
    bool playPauseActive() const { return active_[1]; }
    bool capturing() const { return capturing_; }
    void setToggleSequence(const QString &text) { setSequence(0, text); }
    void setPlayPauseSequence(const QString &text) { setSequence(1, text); }
    void setCapturing(bool capturing);
    // Development runs must not grab global keys.
    void setSuspended(bool suspended);

    Q_INVOKABLE QString sequenceFromKey(int key, int modifiers) const;

signals:
    void toggleRequested();
    void playPauseRequested();
    void changed();

private:
    friend class HotkeyEventFilter;
    void setSequence(int index, const QString &text);
    void registerAll();
    void unregisterAll();
    void handleHotkey(int index);
    QSettings settings_;
    QString sequences_[2];
    bool active_[2] = {false, false};
    bool capturing_ = false;
    bool suspended_ = false;
    std::unique_ptr<HotkeyEventFilter> filter_;
};
