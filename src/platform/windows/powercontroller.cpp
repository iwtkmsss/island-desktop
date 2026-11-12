#include "powercontroller.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QMetaObject>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class PowerEventFilter final : public QAbstractNativeEventFilter {
public:
    explicit PowerEventFilter(PowerController *owner) : owner_(owner) {}
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override {
#ifdef Q_OS_WIN
        if (eventType != "windows_generic_MSG" || !message) return false;
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_POWERBROADCAST || msg->wParam != PBT_APMPOWERSTATUSCHANGE) return false;
        QMetaObject::invokeMethod(owner_, [owner = owner_] { owner->refresh(); }, Qt::QueuedConnection);
#else
        Q_UNUSED(eventType)
        Q_UNUSED(message)
#endif
        return false;  // never consume: other listeners may need it
    }
private:
    PowerController *owner_;
};

PowerController::PowerController(QObject *parent) : QObject(parent) {
    filter_ = std::make_unique<PowerEventFilter>(this);
    if (auto *app = QCoreApplication::instance()) app->installNativeEventFilter(filter_.get());
    fallback_.setInterval(60000);
    connect(&fallback_, &QTimer::timeout, this, &PowerController::refresh);
    fallback_.start();
    refresh();
}

PowerController::~PowerController() {
    if (auto *app = QCoreApplication::instance()) app->removeNativeEventFilter(filter_.get());
}

void PowerController::refresh() {
    PowerSnapshot now;
#ifdef Q_OS_WIN
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status)) {
        now.onAc = status.ACLineStatus != 0;  // 1 online, 255 unknown -> treat unknown as online
        now.percent = status.BatteryLifePercent <= 100 ? status.BatteryLifePercent : -1;
        // Desktop PCs may report flag 0 with an unknown percent instead of
        // "no system battery" (128); a battery without a level is not shown.
        now.present = (status.BatteryFlag & 128) == 0 && status.BatteryFlag != 255 && now.percent >= 0;
    }
#endif
    const bool different = now.present != snapshot_.present || now.onAc != snapshot_.onAc || now.percent != snapshot_.percent;
    snapshot_ = now;
    const PowerEvent event = tracker_.update(now);
    if (different) emit changed();
    if (event != PowerEvent::None) emit powerEvent(static_cast<int>(event), now.percent);
}
