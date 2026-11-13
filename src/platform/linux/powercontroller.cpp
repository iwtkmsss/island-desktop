#include "powercontroller.h"
#include <QDir>
#include <QFile>
#include <algorithm>
class PowerEventFilter {};
namespace {
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray{}; }
}
PowerController::PowerController(QObject *parent) : QObject(parent) {
    connect(&fallback_, &QTimer::timeout, this, &PowerController::refresh);
    fallback_.start(5000); refresh();
}
PowerController::~PowerController() = default;
void PowerController::refresh() {
    PowerSnapshot next; int count = 0, capacity = 0;
    bool anyCharging = false, anyDischarging = false;
    const QDir dir("/sys/class/power_supply");
    for (const auto &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto path = dir.filePath(name) + '/';
        if (read(path + "type") != "Battery" || read(path + "scope") == "Device" || read(path + "present") == "0") continue;
        next.present = true;
        bool ok = false; const int value = read(path + "capacity").toInt(&ok);
        if (ok) { capacity += std::clamp(value, 0, 100); ++count; }
        const auto status = read(path + "status");
        anyCharging |= status == "Charging"; anyDischarging |= status == "Discharging";
    }
    next.percent = count ? capacity / count : -1;
    next.onAc = anyCharging || !anyDischarging;
    const auto event = tracker_.update(next);
    if (snapshot_.present != next.present || snapshot_.percent != next.percent || snapshot_.onAc != next.onAc) {
        snapshot_ = next; emit changed();
    }
    if (event != PowerEvent::None) emit powerEvent(int(event), next.percent);
}
