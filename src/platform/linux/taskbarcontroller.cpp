#include "taskbarcontroller.h"
class TaskbarController::Native {};
TaskbarController::TaskbarController(const QString &settingsFile, QObject *parent)
    : QObject(parent), settings_(settingsFile, QSettings::IniFormat) {}
TaskbarController::~TaskbarController() = default;
void TaskbarController::setControlAllowed(bool) {}
bool TaskbarController::available() const { return false; }
void TaskbarController::setHidden(bool) {}
void TaskbarController::fail(const QString &) {}
