#include "volumecontroller.h"
#include "volumechangetracker.h"

#include <QElapsedTimer>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <wrl/client.h>
#endif

struct VolumeSharedState {
    std::atomic_bool running{true};
    std::atomic_bool endpointDirty{true};
    std::atomic_bool volumeDirty{true};
    // A mailbox, rather than queued commands for every slider movement. A slow
    // audio service cannot leave a backlog that keeps changing volume after drag.
    std::atomic_int requestedVolume{-1};
    std::atomic_uint muteToggles{0};
};

#ifdef Q_OS_WIN
using Microsoft::WRL::ComPtr;

// Adapted from VolumeNotifyClient / BindVolumeEndpoint in the supplied
// dynamic-island-for-windows 1.4.0 (lines 8108–8272).
// Copyright (c) 2026 devcode90, MIT; third_party/dynamic-island/LICENSE.
// Callbacks only set flags. Qt objects and all MMDevice calls stay on the worker.
class VolumeNotifyClient final : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
public:
    explicit VolumeNotifyClient(std::shared_ptr<VolumeSharedState> state) : state_(std::move(state)) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return refs_.fetch_add(1) + 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = refs_.fetch_sub(1) - 1;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioEndpointVolumeCallback))
            *object = static_cast<IAudioEndpointVolumeCallback *>(this);
        else if (iid == __uuidof(IMMNotificationClient))
            *object = static_cast<IMMNotificationClient *>(this);
        else { *object = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
        if (data && state_->running.load()) state_->volumeDirty.store(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) markEndpoint();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { markEndpoint(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { markEndpoint(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { markEndpoint(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { markEndpoint(); return S_OK; }
private:
    void markEndpoint() { if (state_->running.load()) state_->endpointDirty.store(true); }
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<VolumeSharedState> state_;
};

static QString friendlyDeviceName(IMMDevice *device) {
    // PKEY_Device_FriendlyName, as defined by functiondiscoverykeys_devpkey.h.
    static constexpr PROPERTYKEY friendlyNameKey = {
        {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    QString result;
    ComPtr<IPropertyStore> properties;
    if (device && SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
        PROPVARIANT value{};
        if (SUCCEEDED(properties->GetValue(friendlyNameKey, &value))
                && value.vt == VT_LPWSTR && value.pwszVal)
            result = QString::fromWCharArray(value.pwszVal);
        PropVariantClear(&value);
    }
    return result.isEmpty() ? QStringLiteral("Типовий пристрій виводу") : result;
}
#endif

class VolumeWorker final : public QObject {
    Q_OBJECT
public:
    explicit VolumeWorker(std::shared_ptr<VolumeSharedState> state) : state_(std::move(state)) {}
    ~VolumeWorker() override {
        state_->running.store(false);
#ifdef Q_OS_WIN
        unbind();
        if (enumerator_ && enumeratorNotifyRegistered_)
            enumerator_->UnregisterEndpointNotificationCallback(notify_);
        enumerator_.Reset();
        if (notify_) { notify_->Release(); notify_ = nullptr; }
        if (apartment_) CoUninitialize();
#endif
    }
public slots:
    void initialize() {
#ifdef Q_OS_WIN
        const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result)) { unavailable(QStringLiteral("Не вдалося підключитися до аудіослужби Windows.")); return; }
        apartment_ = true;
        notify_ = new VolumeNotifyClient(state_);
        refreshClock_.start();
        timer_ = new QTimer(this);
        timer_->setInterval(80);
        connect(timer_, &QTimer::timeout, this, &VolumeWorker::tick);
        tick();
        timer_->start();
#else
        unavailable(QStringLiteral("Керування гучністю цієї платформи ще не підключене."));
#endif
    }
signals:
    void snapshotReady(VolumeSnapshot snapshot, bool event);
private:
    void unavailable(const QString &error) {
        tracker_.reset();
        VolumeSnapshot snapshot;
        snapshot.error = error;
        emit snapshotReady(snapshot, false);
    }
#ifdef Q_OS_WIN
    void unbind() {
        if (endpoint_ && endpointNotifyRegistered_) endpoint_->UnregisterControlChangeNotify(notify_);
        endpointNotifyRegistered_ = false;
        endpoint_.Reset();
        deviceId_.clear();
        deviceName_.clear();
    }
    bool bindDefault() {
        if (!enumerator_) {
            const auto result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                CLSCTX_ALL, IID_PPV_ARGS(enumerator_.GetAddressOf()));
            if (FAILED(result)) { unavailable(QStringLiteral("Не вдалося підключитися до аудіослужби Windows.")); return false; }
            enumeratorNotifyRegistered_ = SUCCEEDED(enumerator_->RegisterEndpointNotificationCallback(notify_));
        }
        ComPtr<IMMDevice> device;
        if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
            unbind();
            unavailable(QStringLiteral("Пристрій виводу звуку недоступний. Підключіть навушники або динаміки."));
            return false;
        }
        LPWSTR allocatedId = nullptr;
        if (FAILED(device->GetId(&allocatedId))) {
            unbind(); unavailable(QStringLiteral("Не вдалося прочитати пристрій виводу звуку.")); return false;
        }
        const QString id = QString::fromWCharArray(allocatedId);
        CoTaskMemFree(allocatedId);
        if (id != deviceId_ || !endpoint_) {
            unbind();
            const auto result = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                nullptr, reinterpret_cast<void **>(endpoint_.GetAddressOf()));
            if (FAILED(result)) { unavailable(QStringLiteral("Керування гучністю цього пристрою недоступне.")); return false; }
            deviceId_ = id;
            endpointNotifyRegistered_ = SUCCEEDED(endpoint_->RegisterControlChangeNotify(notify_));
        }
        deviceName_ = friendlyDeviceName(device.Get());
        return true;
    }
    void read() {
        if (!endpoint_) return;
        float level = 0;
        BOOL muted = FALSE;
        if (FAILED(endpoint_->GetMasterVolumeLevelScalar(&level)) || FAILED(endpoint_->GetMute(&muted))
                || !std::isfinite(level)) {
            unbind();
            unavailable(QStringLiteral("Не вдалося прочитати гучність. Перевірте пристрій виводу звуку."));
            return;
        }
        VolumeSnapshot snapshot;
        snapshot.available = true;
        snapshot.volume = std::clamp(static_cast<int>(std::lround(level * 100)), 0, 100);
        snapshot.muted = muted != FALSE;
        snapshot.deviceName = deviceName_;
        const bool event = tracker_.observe(deviceId_, snapshot.volume, snapshot.muted);
        emit snapshotReady(snapshot, event);
    }
    void tick() {
        if (!state_->running.load() || QThread::currentThread()->isInterruptionRequested()) return;
        const int requestedVolume = state_->requestedVolume.exchange(-1);
        const bool toggleMute = (state_->muteToggles.exchange(0) & 1u) != 0;
        const bool safetyRefresh = refreshClock_.elapsed() >= 2000;
        const bool endpointDirty = state_->endpointDirty.exchange(false);
        const bool volumeDirty = state_->volumeDirty.exchange(false);
        const bool command = requestedVolume >= 0 || toggleMute;
        if (safetyRefresh) refreshClock_.restart();

        // Resolve immediately before a command; never write a previous default
        // output after the user has just switched Windows to another endpoint.
        if (endpointDirty || safetyRefresh || command) {
            if (!bindDefault()) return;
        }
        if (!endpoint_) return;

        if (command) {
            HRESULT result = S_OK;
            if (requestedVolume >= 0)
                result = endpoint_->SetMasterVolumeLevelScalar(static_cast<float>(requestedVolume) / 100, nullptr);
            if (SUCCEEDED(result) && toggleMute) {
                BOOL muted = FALSE;
                result = endpoint_->GetMute(&muted);
                if (SUCCEEDED(result)) result = endpoint_->SetMute(!muted, nullptr);
            }
            if (FAILED(result)) {
                unbind();
                unavailable(QStringLiteral("Не вдалося змінити гучність. Перевірте пристрій виводу звуку."));
                return;
            }
        }
        if (endpointDirty || safetyRefresh || volumeDirty || command || !endpointNotifyRegistered_) read();
    }
    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IAudioEndpointVolume> endpoint_;
    VolumeNotifyClient *notify_ = nullptr;
    bool apartment_ = false;
    bool enumeratorNotifyRegistered_ = false;
    bool endpointNotifyRegistered_ = false;
    QString deviceId_, deviceName_;
    QElapsedTimer refreshClock_;
    QTimer *timer_ = nullptr;
#endif
    std::shared_ptr<VolumeSharedState> state_;
    VolumeChangeTracker tracker_;
};

VolumeController::VolumeController(QObject *parent)
    : QObject(parent), shared_(std::make_shared<VolumeSharedState>()) {
    auto *worker = new VolumeWorker(shared_);
    worker->moveToThread(&thread_);
    connect(&thread_, &QThread::started, worker, &VolumeWorker::initialize);
    connect(&thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &VolumeWorker::snapshotReady, this, &VolumeController::apply);
    thread_.setObjectName("WindowsVolume");
    thread_.start();
}
VolumeController::~VolumeController() {
    shared_->running.store(false);
    thread_.requestInterruption();
    thread_.quit();
    thread_.wait();
}
void VolumeController::setVolume(int percent) {
    if (available()) shared_->requestedVolume.store(std::clamp(percent, 0, 100));
}
void VolumeController::toggleMute() {
    if (available()) shared_->muteToggles.fetch_xor(1u);
}
void VolumeController::apply(VolumeSnapshot snapshot, bool event) {
    if (snapshot_ != snapshot) { snapshot_ = std::move(snapshot); emit changed(); }
    if (event && snapshot_.available) emit volumeEvent(snapshot_.volume, snapshot_.muted);
}

#include "volumecontroller.moc"
