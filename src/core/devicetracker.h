#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

// Pure logic: detects connect/disconnect transitions between two scans of the
// paired Bluetooth devices. No OS calls.

struct DeviceInfo {
    QString id;
    QString name;
    QString kind;  // headphones, keyboard, mouse, phone, computer, gamepad, other
    bool connected = false;
    int battery = -1;  // percent, -1 unknown
};

struct DeviceChange {
    DeviceInfo device;
    bool connected = false;  // the new state
};

// Maps the Bluetooth Class of Device (major/minor bit fields) to a UI kind.
inline QString deviceKindFromClass(quint32 classOfDevice) {
    const quint32 major = (classOfDevice >> 8) & 0x1Fu;
    switch (major) {
    case 0x01: return QStringLiteral("computer");
    case 0x02: return QStringLiteral("phone");
    case 0x04: return QStringLiteral("headphones");  // audio/video
    case 0x05: {                                     // peripheral
        const quint32 subtype = (classOfDevice >> 2) & 0x0Fu;  // bits 2-5
        const quint32 input = (classOfDevice >> 6) & 0x03u;    // bits 6-7
        if (subtype == 0x01 || subtype == 0x02) return QStringLiteral("gamepad");  // joystick, gamepad
        if (input == 0x02) return QStringLiteral("mouse");
        if (input == 0x01 || input == 0x03) return QStringLiteral("keyboard");  // keyboard, combo
        return QStringLiteral("other");
    }
    default: return QStringLiteral("other");
    }
}

class DeviceTracker {
public:
    QList<DeviceChange> update(const QList<DeviceInfo> &snapshot) {
        QHash<QString, DeviceInfo> current;
        for (const DeviceInfo &d : snapshot)
            if (!current.contains(d.id)) current.insert(d.id, d);  // first wins

        QList<DeviceChange> changes;
        if (baselineDone_) {
            QSet<QString> reported;
            // Walk the snapshot in order for stable output, then disappearances.
            for (const DeviceInfo &d : snapshot) {
                if (reported.contains(d.id)) continue;
                reported.insert(d.id);
                const DeviceInfo &now = current[d.id];
                const auto it = known_.constFind(now.id);
                const bool was = it != known_.constEnd() && it->connected;
                if (now.connected && !was) changes.append({now, true});
                else if (!now.connected && was) changes.append({now, false});
            }
            for (auto it = known_.constBegin(); it != known_.constEnd(); ++it)
                if (it->connected && !current.contains(it.key())) changes.append({it.value(), false});
        }
        known_ = current;
        baselineDone_ = true;
        return changes;
    }

    void reset() {
        known_.clear();
        baselineDone_ = false;
    }

private:
    QHash<QString, DeviceInfo> known_;
    bool baselineDone_ = false;
};
