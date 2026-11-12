#pragma once

#include "powertracker.h"
#include <QObject>
#include <QTimer>
#include <memory>

class PowerEventFilter;

// System battery state. GUI thread only.
class PowerController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool present READ present NOTIFY changed)
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    Q_PROPERTY(bool charging READ charging NOTIFY changed)
public:
    explicit PowerController(QObject *parent = nullptr);
    ~PowerController() override;

    bool present() const { return snapshot_.present; }
    int percent() const { return snapshot_.percent; }
    bool charging() const { return snapshot_.onAc; }

signals:
    void changed();
    // event is a PowerEvent value passed as int (Plugged, Unplugged, Low, Critical).
    void powerEvent(int event, int percent);

private:
    friend class PowerEventFilter;
    void refresh();
    PowerSnapshot snapshot_;
    PowerTracker tracker_;
    QTimer fallback_;
    std::unique_ptr<PowerEventFilter> filter_;
};
