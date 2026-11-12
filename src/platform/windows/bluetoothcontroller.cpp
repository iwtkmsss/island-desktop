#include "bluetoothcontroller.h"

#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <iterator>
#include <mutex>
#include <type_traits>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <devpropdef.h>
#include <setupapi.h>
#include <unordered_map>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#endif

namespace {
constexpr auto kScanInterval = std::chrono::seconds(5);

#ifdef Q_OS_WIN
using namespace winrt::Windows::Devices::Bluetooth;
using winrt::Windows::Devices::Enumeration::DeviceInformation;
using winrt::Windows::Devices::Enumeration::DeviceInformationCollection;

struct ScanResult {
    bool available = false;
    bool ok = true;  // false: the enumeration itself failed, keep the tracker untouched
    QList<DeviceInfo> devices;
    QString error;
};

QString addressId(uint64_t address) {
    return QString::number(static_cast<qulonglong>(address), 16).rightJustified(12, QLatin1Char('0')).toUpper();
}

// Adapted from ClassifyBluetoothDevice (supplied mod 1.4.0, lines 5354-5384):
// name heuristic, used here only for BLE devices that have no Class of Device.
QString kindFromName(const QString &name) {
    const QString n = name.toLower();
    auto has = [&](std::initializer_list<const char *> words) {
        return std::any_of(words.begin(), words.end(), [&](const char *w) { return n.contains(QLatin1String(w)); });
    };
    if (has({"headphone", "headset", "earbud", "buds", "airpods", "speaker", "soundbar", "boombox"}))
        return QStringLiteral("headphones");
    if (has({"mouse"})) return QStringLiteral("mouse");
    if (has({"keyboard"})) return QStringLiteral("keyboard");
    if (has({"gamepad", "controller", "joy-con"})) return QStringLiteral("gamepad");
    if (has({"iphone", "phone", "galaxy", "pixel"})) return QStringLiteral("phone");
    return QStringLiteral("other");
}

// Adapted from TryReadClassicBluetoothBatteryPercent (supplied mod 1.4.0, lines
// 5465-5539): the PnP property {104EA319-...}, pid 2 on BTHENUM devnodes.
// Unlike the mod, the devnodes are enumerated once per scan, not once per device.
struct BatteryNode {
    std::wstring lowerInstanceId;
    int percent;
};

std::vector<BatteryNode> readBatteryNodes() {
    static const DEVPROPKEY batteryKey = {
        {0x104ea319, 0x6ee2, 0x4701, {0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5}}, 2};
    std::vector<BatteryNode> nodes;
    HDEVINFO set = SetupDiGetClassDevsExW(nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT,
        nullptr, nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE) return nodes;
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        wchar_t instanceId[512] = {};
        if (!SetupDiGetDeviceInstanceIdW(set, &info, instanceId, ARRAYSIZE(instanceId), nullptr)) continue;
        DEVPROPTYPE type = 0;
        BYTE battery = 0;
        DWORD required = 0;
        if (!SetupDiGetDevicePropertyW(set, &info, &batteryKey, &type, &battery, sizeof(battery), &required, 0))
            continue;
        if (type != DEVPROP_TYPE_BYTE || battery > 100) continue;  // 0xFF = unknown
        std::wstring id = instanceId;
        std::transform(id.begin(), id.end(), id.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
        nodes.push_back({std::move(id), static_cast<int>(battery)});
    }
    SetupDiDestroyDeviceInfoList(set);
    return nodes;
}
#endif
}  // namespace

class BluetoothWorkerThread final : public QThread {
public:
    explicit BluetoothWorkerThread(BluetoothController *owner) : owner_(owner) {
        setObjectName(QStringLiteral("WindowsBluetooth"));
    }
    void requestRefresh() {
        {
            std::lock_guard lock(mutex_);
            refreshRequested_ = true;
        }
        cv_.notify_all();
    }
    void stop() {
        running_.store(false);
        requestInterruption();
        cv_.notify_all();
    }

protected:
    void run() override {
#ifdef Q_OS_WIN
        bool apartment = false;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartment = true;
        } catch (...) {}
        while (running_.load()) {
            const ScanResult scan = scanOnce();
            if (!running_.load()) break;
            QList<DeviceChange> changes;
            if (scan.ok) changes = tracker_.update(scan.devices);
            post(scan, changes);

            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, kScanInterval, [this] { return refreshRequested_ || !running_.load(); });
            refreshRequested_ = false;
        }
        classicCache_.clear();  // release WinRT objects while the apartment is alive
        leCache_.clear();
        if (apartment) winrt::uninit_apartment();
#else
        post({}, {});
#endif
    }

private:
    struct PostedScan {
        bool available = false;
        QList<DeviceInfo> devices;
        QString error;
    };
#ifdef Q_OS_WIN
    void post(const ScanResult &scan, const QList<DeviceChange> &changes) {
        const quint64 sequence = ++sequence_;
        const bool available = scan.available;
        const QList<DeviceInfo> devices = scan.devices;
        const QString error = scan.error;
        // Queued to the GUI thread; discarded by Qt if the controller is gone. The
        // sequence number lets the GUI side drop anything older than what it applied.
        QMetaObject::invokeMethod(owner_, [owner = owner_, sequence, available, devices, error, changes] {
            owner->apply(sequence, available, devices, error, changes);
        }, Qt::QueuedConnection);
    }

    // Waits for a WinRT operation in short slices so shutdown is never held up by
    // a stuck Bluetooth stack; throws on timeout/stop.
    template <typename Op>
    auto await(Op const &op) {
        using namespace std::chrono_literals;
        const auto deadline = std::chrono::steady_clock::now() + 8s;
        while (op.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
            if (!running_.load() || std::chrono::steady_clock::now() > deadline) {
                op.Cancel();
                throw winrt::hresult_canceled();
            }
            op.wait_for(100ms);
        }
        return op.GetResults();
    }

    ScanResult scanOnce() {
        ScanResult result;
        try {
            auto adapter = await(BluetoothAdapter::GetDefaultAsync());
            if (!adapter) {
                result.error = QStringLiteral("Адаптер Bluetooth не знайдено.");
                tracker_.update({});  // adapter gone: connected devices have disconnected
                return result;
            }
            if (!radioIsOn()) {
                result.error = QStringLiteral("Bluetooth вимкнено.");
                tracker_.update({});
                return result;
            }
            result.available = true;
            enumerate(result);
        } catch (...) {
            if (!running_.load()) return result;
            result.ok = false;
            result.available = false;
            result.devices.clear();
            result.error = QStringLiteral("Не вдалося прочитати список пристроїв Bluetooth.");
        }
        return result;
    }

    bool radioIsOn() {
        using namespace winrt::Windows::Devices::Radios;
        try {
            auto radios = await(Radio::GetRadiosAsync());
            bool any = false, on = false;
            for (auto const &radio : radios) {
                if (radio.Kind() != RadioKind::Bluetooth) continue;
                any = true;
                if (radio.State() == RadioState::On) on = true;
            }
            return !any || on;  // state unreadable -> do not claim it is off
        } catch (...) {
            if (!running_.load()) throw;
            return true;
        }
    }

    template <typename Device, typename Cache>
    void collect(Cache &cache, const winrt::hstring &selector, bool classic, QList<DeviceInfo> &out,
        std::vector<uint64_t> &addresses, std::vector<std::pair<size_t, uint64_t>> &toBattery) {
        DeviceInformationCollection infos = await(DeviceInformation::FindAllAsync(selector));
        std::unordered_map<std::wstring, bool> seen;
        for (auto const &info : infos) {
            if (!running_.load()) throw winrt::hresult_canceled();
            const std::wstring key = info.Id().c_str();
            seen[key] = true;
            auto it = cache.find(key);
            if (it == cache.end()) {
                auto device = await(Device::FromIdAsync(info.Id()));
                if (!device) continue;
                it = cache.emplace(key, device).first;
            }
            auto const &device = it->second;
            const uint64_t address = device.BluetoothAddress();
            if (!address) continue;
            if (std::find(addresses.begin(), addresses.end(), address) != addresses.end())
                continue;  // dual-mode device already listed from the classic side
            addresses.push_back(address);
            DeviceInfo d;
            d.id = addressId(address);
            d.name = QString::fromWCharArray(device.Name().c_str());
            if (d.name.isEmpty()) d.name = QString::fromWCharArray(info.Name().c_str());
            if (d.name.isEmpty()) d.name = QStringLiteral("Пристрій Bluetooth");
            if constexpr (std::is_same_v<Device, BluetoothDevice>) {
                d.kind = deviceKindFromClass(device.ClassOfDevice().RawValue());
                if (d.kind == QLatin1String("other")) d.kind = kindFromName(d.name);
            } else {
                d.kind = kindFromName(d.name);
            }
            d.connected = device.ConnectionStatus() == BluetoothConnectionStatus::Connected;
            out.append(d);
            if (d.connected) toBattery.emplace_back(static_cast<size_t>(out.size() - 1), address);
        }
        for (auto it = cache.begin(); it != cache.end();)
            it = seen.count(it->first) ? std::next(it) : cache.erase(it);
        Q_UNUSED(classic)
    }

    void enumerate(ScanResult &result) {
        std::vector<uint64_t> addresses;
        std::vector<std::pair<size_t, uint64_t>> toBattery;
        collect<BluetoothDevice>(classicCache_, BluetoothDevice::GetDeviceSelectorFromPairingState(true), true,
            result.devices, addresses, toBattery);
        // BLE-only paired peripherals (mice, keyboards, trackers). Failure here must not
        // hide the classic list.
        try {
            collect<BluetoothLEDevice>(leCache_, BluetoothLEDevice::GetDeviceSelectorFromPairingState(true), false,
                result.devices, addresses, toBattery);
        } catch (...) {
            if (!running_.load()) throw;
        }
        if (!toBattery.empty()) {
            const auto nodes = readBatteryNodes();
            for (const auto &[index, address] : toBattery) {
                const std::wstring needle = addressId(address).toLower().toStdWString();
                for (const auto &node : nodes) {
                    if (node.lowerInstanceId.find(needle) != std::wstring::npos) {
                        result.devices[static_cast<qsizetype>(index)].battery = node.percent;
                        break;
                    }
                }
            }
        }
        std::sort(result.devices.begin(), result.devices.end(), [](const DeviceInfo &a, const DeviceInfo &b) {
            if (a.connected != b.connected) return a.connected;
            const int c = a.name.compare(b.name, Qt::CaseInsensitive);
            return c != 0 ? c < 0 : a.id < b.id;
        });
    }

    std::unordered_map<std::wstring, BluetoothDevice> classicCache_;
    std::unordered_map<std::wstring, BluetoothLEDevice> leCache_;
#else
    void post(const PostedScan &, const QList<DeviceChange> &) {
        QMetaObject::invokeMethod(owner_, [owner = owner_] {
            owner->apply(1, false, {}, QStringLiteral("Керування Bluetooth цієї платформи ще не підключене."), {});
        }, Qt::QueuedConnection);
    }
#endif

    BluetoothController *owner_;
    std::atomic_bool running_{true};
    std::atomic<quint64> sequence_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
    bool refreshRequested_ = false;
    DeviceTracker tracker_;
};

BluetoothController::BluetoothController(QObject *parent)
    : QObject(parent), worker_(std::make_unique<BluetoothWorkerThread>(this)) {
    worker_->start();
}

BluetoothController::~BluetoothController() {
    worker_->stop();
    worker_->wait();
}

void BluetoothController::refresh() {
    worker_->requestRefresh();
}

void BluetoothController::apply(quint64 sequence, bool available, const QList<DeviceInfo> &devices,
    const QString &error, const QList<DeviceChange> &changes) {
    if (sequence <= lastSequence_) return;  // stale result
    lastSequence_ = sequence;

    QVariantList list;
    int connected = 0;
    for (const DeviceInfo &d : devices) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), d.id);
        map.insert(QStringLiteral("name"), d.name);
        map.insert(QStringLiteral("kind"), d.kind);
        map.insert(QStringLiteral("connected"), d.connected);
        map.insert(QStringLiteral("battery"), d.battery);
        list.append(map);
        if (d.connected) ++connected;
    }
    if (available != available_ || list != devices_ || error != error_) {
        available_ = available;
        devices_ = std::move(list);
        connectedCount_ = connected;
        error_ = error;
        emit changed();
    }
    for (const DeviceChange &c : changes)
        emit deviceEvent(c.device.id, c.device.name, c.connected, c.device.battery);
}
