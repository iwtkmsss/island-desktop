#pragma once
#include <taskmanager/abstracttasksmodel.h>
#include <QAbstractItemModel>
#include <QDateTime>

// Behavior follows KDE TaskTools' cycle mode, implemented against the installed
// public task model API. No focus-changing popup is involved in a left click.
// https://invent.kde.org/plasma/plasma-desktop/-/blob/master/applets/taskmanager/qml/code/TaskTools.js
//
// Unlike the Plasma panel, the island surface can take keyboard focus on click,
// which deactivates the window before the click arrives. `wasActive` is the task
// that was active right before the island took focus; it is treated as active.
namespace IslandDesktop {
template<class Commands>
void activateTask(QAbstractItemModel &model, Commands &commands, const QModelIndex &task,
                  const QModelIndex &wasActive = {}) {
    using Roles = TaskManager::AbstractTasksModel;
    if (!task.isValid() || task.model() != &model) return;
    const auto active = [&](const QModelIndex &index) {
        return index.data(Roles::IsActive).toBool() || (wasActive.isValid() && index == wasActive);
    };
    if (task.data(Roles::IsGroupParent).toBool()) {
        const int count = model.rowCount(task);
        if (!count) return;
        QModelIndex target;
        QDateTime latest;
        int highestStack = -1;
        QModelIndex stacked;
        for (int row = 0; row < count; ++row) {
            const auto child = model.index(row, 0, task);
            if (active(child)) {
                commands.requestActivate(model.index((row + 1) % count, 0, task));
                return;
            }
            const auto timestamp = child.data(Roles::LastActivated).toDateTime();
            if (timestamp.isValid() && (!latest.isValid() || timestamp > latest)) {
                latest = timestamp;
                target = child;
            }
            bool valid = false;
            const int stack = child.data(Roles::StackingOrder).toInt(&valid);
            if (valid && stack > highestStack) {
                highestStack = stack;
                stacked = child;
            }
        }
        if (!target.isValid()) target = stacked.isValid() ? stacked : model.index(0, 0, task);
        // The group itself was active (child unknown): the most recent child was it.
        if (wasActive.isValid() && wasActive == task)
            target = model.index((target.row() + 1) % count, 0, task);
        commands.requestActivate(target);
        return;
    }
    if (task.data(Roles::IsMinimized).toBool()) {
        commands.requestToggleMinimized(task);
        commands.requestActivate(task);
    } else if (active(task) && task.data(Roles::IsWindow).toBool()) {
        if (task.data(Roles::IsMinimizable).toBool()) commands.requestToggleMinimized(task);
    } else {
        // Launchers and startup tasks retain the model's own activation rules.
        commands.requestActivate(task);
    }
}
}
