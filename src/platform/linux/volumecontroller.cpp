#include "volumecontroller.h"
#include "pulseworker.h"
struct VolumeSharedState { PulseWorker worker; };
VolumeController::VolumeController(QObject *parent) : QObject(parent), shared_(std::make_shared<VolumeSharedState>()) {
    shared_->worker.volumeReady = [this](VolumeSnapshot snapshot, bool event) {
        QMetaObject::invokeMethod(this, [this, snapshot, event] { apply(snapshot, event); }, Qt::QueuedConnection);
    };
    shared_->worker.start();
}
VolumeController::~VolumeController() { shared_->worker.requestInterruption(); shared_->worker.wait(); }
void VolumeController::setVolume(int percent) { if (available()) shared_->worker.volume(percent); }
void VolumeController::toggleMute() { if (available()) shared_->worker.mute(); }
void VolumeController::apply(VolumeSnapshot snapshot, bool event) {
    if (snapshot == snapshot_) return;
    snapshot_ = std::move(snapshot); emit changed();
    if (event) emit volumeEvent(snapshot_.volume, snapshot_.muted);
}
