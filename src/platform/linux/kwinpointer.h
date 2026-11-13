#pragma once
#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QTemporaryDir>

// KDE Wayland's compositor owns the real desktop cursor. XWayland's cursor
// query can retain the last point over an X11 surface after the mouse leaves.
class KWinPointer final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.iwtkmsss.IslandDesktop.Pointer")
public:
    explicit KWinPointer(const QString &identity, QObject *parent = nullptr);
    ~KWinPointer() override;
    void setEnabled(bool enabled);
    bool available() const { return enabled_ && available_; }
    QPointF position() const;
    quint64 movementSerial() const { return movementSerial_; }
public slots:
    bool Update(const QString &payload);
signals:
    void availabilityChanged();
private:
    void start(const QString &owner);
    void stop();
    void setAvailable(bool available);
    QDBusServiceWatcher watcher_;
    QTemporaryDir temporary_;
    QString plugin_, path_, script_, owner_;
    QPointF position_;
    QRectF output_;
    QString screen_;
    quint64 epoch_ = 0, movementSerial_ = 0, sourceMovement_ = 0;
    bool enabled_ = false, available_ = false, registered_ = false;
};
