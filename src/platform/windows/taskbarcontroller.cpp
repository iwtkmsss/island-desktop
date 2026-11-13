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
    ~Native() { stop(false); }
    void start() {
        if (!controlAllowed || child.process) return;
        owner->fail({});
        if (!child.start(QCoreApplication::applicationFilePath().toStdWString(), marker)) {
            owner->fail(QStringLiteral("Не вдалося запустити захист відновлення панелі задач Windows."));
            return;
        }
        starting = true; startup.start(); timer.start();
    }
    void check() {
        if (!child.process) return;
        if (WaitForSingleObject(child.process, 0) == WAIT_OBJECT_0) {
            DWORD code = 0; GetExitCodeProcess(child.process, &code);
            timer.stop(); child.close();
            const bool restored = TaskbarNative::restoreWindows(marker);
            owner->fail(restored
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
            SetEvent(child.stop);
            if (WaitForSingleObject(child.process, 2000) != WAIT_OBJECT_0) {
                // Only our child is terminated; its tagged windows are restored
                // below even if it failed before reaching its cleanup path.
                TerminateProcess(child.process, 4);
                WaitForSingleObject(child.process, 1000);
                clean = false;
            }
            child.close();
        }
        const bool restored = TaskbarNative::restoreWindows(marker);
        if (report && !restored) owner->fail(QStringLiteral("Не вдалося відновити панель задач Windows."));
        else if (report && !clean) owner->fail(QStringLiteral("Панель задач відновлено аварійним способом; захист не завершився штатно."));
    }
    TaskbarController *owner;
    TaskbarNative::GuardianProcess child;
    QTimer timer;
    QElapsedTimer startup;
    std::wstring marker;
    bool starting = false;
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
