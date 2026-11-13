#include "taskbarcontroller.h"
#include "taskbarguardian.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QUuid>

namespace { bool controlAllowed = true; }

class TaskbarController::Native {
public:
    explicit Native(TaskbarController *owner) : owner(owner) {
        marker = (QStringLiteral("IslandDesktop.HiddenTaskbar.")
            + QUuid::createUuid().toString(QUuid::WithoutBraces)).toStdWString();
        timer.setInterval(100);
        QObject::connect(&timer, &QTimer::timeout, owner, [this] { check(); });
    }
    ~Native() { destroying = true; stop(false); }
    void start() {
        if (!controlAllowed || child.process) return;
        stopping = false;
        owner->fail({});
        TaskbarNative::setActiveWorkAreaMarker(marker);
        if (!child.start(QCoreApplication::applicationFilePath().toStdWString(), marker)) {
            owner->fail(QStringLiteral("Не вдалося запустити захист відновлення панелі задач Windows."));
            TaskbarNative::setActiveWorkAreaMarker({});
            return;
        }
        starting = true; startup.start(); timer.start();
    }
    void check() {
        if (!child.process) return;
        if (WaitForSingleObject(child.process, 0) == WAIT_OBJECT_0) {
            DWORD code = 0; GetExitCodeProcess(child.process, &code);
            auto operations = TaskbarNative::systemWorkAreaOperations(marker);
            const auto snapshot = TaskbarNative::controlSnapshot({}, operations.get());
            recoveryGate.observe(TaskbarNative::isInteractiveSession(), snapshot.geometry, GetTickCount64());
            if (!recoveryGate.cleanupReady(GetTickCount64())) return;
            timer.stop();
            const bool restored = restore();
            if (!restored && !TaskbarNative::isInteractiveSession()) { timer.start(); return; }
            const bool expected = stopping && code == 0;
            child.close();
            if (expected) {
                if (owner->hidden()) start();
                return;
            }
            owner->fail(code == 5
                ? QStringLiteral("Windows повторно показує панель задач; приховування зупинено, щоб уникнути мерехтіння.") : restored
                ? QStringLiteral("Захист панелі задач Windows завершився (%1); панель відновлено.").arg(code)
                : QStringLiteral("Не вдалося відновити панель задач Windows після збою захисту."));
        } else if (starting && WaitForSingleObject(child.ready, 0) == WAIT_OBJECT_0) {
            starting = false;
        } else if (starting && startup.elapsed() > 5000) {
            stop(false);
            owner->fail(QStringLiteral("Захист панелі задач Windows не відповів; приховування скасовано."));
        }
    }
    void stop(bool report) {
        timer.stop();
        bool clean = true;
        if (child.process) {
            stopping = true;
            SetEvent(child.stop);
            if (WaitForSingleObject(child.process, 2000) != WAIT_OBJECT_0) {
                // It owns the journal and can be waiting for unlock/geometry
                // stabilization. Never kill the recovery process during lock.
                if (!destroying) timer.start();
                return;
            }
        }
        if (!TaskbarNative::isInteractiveSession() && child.workArea) {
            TaskbarNative::GuardianProcess recovery;
            // The restore-only child inherits a mapping handle, so the backup
            // survives even if this parent exits before its entry point runs.
            if (recovery.start(QCoreApplication::applicationFilePath().toStdWString(), marker, {}, true)) {
                child.close();
                return;
            }
            if (report) owner->fail(QStringLiteral("Не вдалося передати відкладене відновлення панелі задач."));
            return;
        }
        const bool restored = restore();
        if (!restored && !TaskbarNative::isInteractiveSession() && child.workArea) {
            TaskbarNative::GuardianProcess recovery;
            if (recovery.start(QCoreApplication::applicationFilePath().toStdWString(), marker, {}, true)) {
                child.close(); return;
            }
        }
        child.close();
        if (report && !restored) owner->fail(QStringLiteral("Не вдалося відновити панель задач Windows."));
        else if (report && !clean) owner->fail(QStringLiteral("Панель задач відновлено аварійним способом; захист не завершився штатно."));
    }
    bool restore() {
        const bool windows = TaskbarNative::restoreWindows(marker);
        bool area = true;
        if (child.workArea) {
            auto operations = TaskbarNative::systemWorkAreaOperations(marker);
            area = child.workArea->restore(*operations);
        }
        if (area) TaskbarNative::setActiveWorkAreaMarker({});
        return windows && area;
    }
    TaskbarController *owner;
    TaskbarNative::GuardianProcess child;
    QTimer timer;
    QElapsedTimer startup;
    std::wstring marker;
    bool starting = false;
    bool stopping = false, destroying = false;
    IslandDesktop::TaskbarSessionControl recoveryGate;
};

TaskbarController::TaskbarController(const QString &settingsFile, QObject *parent)
    : QObject(parent), settings_(settingsFile, QSettings::IniFormat) {
    hidden_ = settings_.value("desktop/hideSystemTaskbar", false).toBool();
    native_ = std::make_unique<Native>(this);
    if (hidden_) native_->start();
}
TaskbarController::~TaskbarController() = default;
void TaskbarController::setControlAllowed(bool allowed) { controlAllowed = allowed; }
bool TaskbarController::available() const { return true; }
void TaskbarController::fail(const QString &error) {
    if (error_ == error) return;
    error_ = error; emit errorChanged();
}
void TaskbarController::setHidden(bool hidden) {
    if (hidden_ == hidden) return;
    settings_.setValue("desktop/hideSystemTaskbar", hidden);
    settings_.sync();
    const bool stored = settings_.status() == QSettings::NoError;
    hidden_ = hidden;
    if (hidden_) native_->start();
    else { fail({}); native_->stop(true); }
    if (!stored) fail(QStringLiteral("Не вдалося зберегти налаштування панелі задач Windows."));
    emit hiddenChanged();
}
