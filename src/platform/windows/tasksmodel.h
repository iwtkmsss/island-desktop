#pragma once
// QAbstractListModel view of IslandDesktop::TaskList with the role names the
// shared QML (DesktopPanel.qml) reads: display, decoration, AppName, IsLauncher.
#include "tasklist.h"
#include <QAbstractListModel>
#include <functional>

class TasksModel final : public QAbstractListModel, public IslandDesktop::TaskList::Observer {
public:
    enum Role { AppNameRole = Qt::UserRole + 1, IsLauncherRole };
    struct Source {
        std::function<QString(const QString &appKey)> appName;
        std::function<QString(const QString &appKey)> iconUrl;
    };

    TasksModel(const IslandDesktop::TaskList &list, Source source)
        : list_(list), source_(std::move(source)) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : list_.rowCount();
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (index.parent().isValid()) return {};
        const auto *row = list_.row(index.row());
        if (!row) return {};
        switch (role) {
        case Qt::DisplayRole: {
            // A single window shows its title; groups and launchers the app name.
            if (row->windows.size() == 1) {
                const auto title = list_.window(row->windows.first()).title;
                if (!title.isEmpty()) return title;
            }
            return name(*row);
        }
        case Qt::DecorationRole:
            return source_.iconUrl ? source_.iconUrl(row->appKey) : QString();
        case AppNameRole:
            return name(*row);
        case IsLauncherRole:
            return row->pinned && row->windows.isEmpty();
        default:
            return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{Qt::DisplayRole, "display"}, {Qt::DecorationRole, "decoration"},
                {AppNameRole, "AppName"}, {IsLauncherRole, "IsLauncher"}};
    }
    void refreshRow(int row) {
        if (row >= 0 && row < rowCount()) emit dataChanged(index(row), index(row));
    }
    void refreshAll() {
        if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
    }

    // TaskList::Observer
    void beginReset() override { beginResetModel(); }
    void endReset() override { endResetModel(); }
    void beginInsert(int first, int last) override { beginInsertRows(QModelIndex(), first, last); }
    void endInsert() override { endInsertRows(); }
    void beginRemove(int row) override { beginRemoveRows(QModelIndex(), row, row); }
    void endRemove() override { endRemoveRows(); }
    void beginMove(int from, int to) override {
        // Qt's destination is the row the item is inserted before.
        moving_ = beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
        if (!moving_) beginResetModel();
    }
    void endMove() override {
        if (moving_) endMoveRows();
        else endResetModel();
        moving_ = false;
    }
    void rowChanged(int row) override { refreshRow(row); }

private:
    QString name(const IslandDesktop::TaskList::Row &row) const {
        auto result = source_.appName ? source_.appName(row.appKey) : QString();
        if (result.isEmpty() && !row.windows.isEmpty()) result = list_.window(row.windows.first()).title;
        return result;
    }
    const IslandDesktop::TaskList &list_;
    Source source_;
    bool moving_ = false;
};
