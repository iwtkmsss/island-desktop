// Windows Home panel controller. Read-only towards the system: tray capture is
// disabled, launchers point at executables that do not exist, and no action
// that activates, minimizes or launches anything is triggered.
#include "desktopcontroller.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString alpha = QStringLiteral("exe:c:\\island-test-missing\\alpha.exe");
const QString beta = QStringLiteral("exe:c:\\island-test-missing\\beta.exe");
QStringList storedKeys(const QString &file) {
    QStringList keys;
    for (const auto &entry : QSettings(file, QSettings::IniFormat).value("desktop/launchers").toStringList())
        keys.append(entry.section('|', 0, 0));
    return keys;
}
QVariant role(QAbstractItemModel *model, int row, const QByteArray &name) {
    const auto roles = model->roleNames();
    return model->index(row, 0).data(roles.key(name, -1));
}
}

class WindowsDesktopTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { DesktopController::setTrayCaptureAllowed(false); }

    void launchersPersistAndKeepOrder() {
        QTemporaryDir dir;
        const auto file = dir.filePath("state.ini");
        {
            QSettings seed(file, QSettings::IniFormat);
            seed.setValue("desktop/launchers", QStringList{alpha + "|C:\\island-test-missing\\alpha.exe",
                                                           beta + "|C:\\island-test-missing\\beta.exe"});
        }
        {
            DesktopController desktop(file);
            QVERIFY(desktop.available());
            auto *model = desktop.tasks();
            QVERIFY(model);
            QVERIFY(model->rowCount() >= 2);
            const auto roles = model->roleNames().values();
            for (const auto *name : {"display", "decoration", "AppName", "IsLauncher"})
                QVERIFY2(roles.contains(name), name);
            QVERIFY(role(model, 0, "IsLauncher").toBool());
            QTRY_COMPARE(role(model, 0, "AppName").toString(), QString("alpha"));
            QCOMPARE(role(model, 1, "display").toString(), QString("beta"));
            QVERIFY(role(model, 0, "decoration").toString().startsWith("image://desktop/task%2Fexe%3A"));
            QVERIFY(role(model, 0, "decoration").toString().endsWith("&raw"));
            QCOMPARE(desktop.taskState(0).value("count").toInt(), 0);
            QCOMPARE(desktop.taskState(0).value("active").toInt(), -1);
            QCOMPARE(desktop.taskState(-1).value("count").toInt(), 0);
            const int revision = desktop.taskRevision();
            desktop.moveTask(0, 1);
            QVERIFY(desktop.taskRevision() > revision);
            QCOMPARE(role(model, 0, "AppName").toString(), QString("beta"));
            QCOMPARE(storedKeys(file), QStringList({beta, alpha}));
            desktop.moveTask(0, 99);  // ignored
            QCOMPARE(storedKeys(file), QStringList({beta, alpha}));
        }
        {
            DesktopController desktop(file);
            auto *model = desktop.tasks();
            QVERIFY(role(model, 0, "IsLauncher").toBool());
            QTRY_COMPARE(role(model, 0, "AppName").toString(), QString("beta"));
            QCOMPARE(role(model, 1, "AppName").toString(), QString("alpha"));
            // The context menu has the Linux structure; unpinning a launcher removes it.
            desktop.taskMenu(0);
            QTRY_VERIFY(desktop.menuOpen());
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            QStringList texts;
            for (auto *action : menu->actions()) texts.append(action->text());
            QCOMPARE(texts, QStringList({"Відкріпити", "Нове вікно", "Ліворуч", "Праворуч"}));
            QVERIFY(!menu->actions()[2]->isEnabled());
            QVERIFY(menu->actions()[3]->isEnabled());
            menu->actions()[0]->trigger();
            menu->close();
            QTRY_VERIFY(!desktop.menuOpen());
            QCOMPARE(role(model, 0, "AppName").toString(), QString("alpha"));
            QCOMPARE(storedKeys(file), QStringList({alpha}));
        }
    }

    void metricsUseLinuxFormat() {
        QTemporaryDir dir;
        DesktopController desktop(dir.filePath("metrics.ini"));
        desktop.setMetricsActive(true);
        // The first sample has no CPU baseline; the second one has a value.
        const QRegularExpression format(QStringLiteral("^CPU  \\d{1,3}%   ·   RAM  \\d+,\\d / \\d+,\\d ГіБ$"));
        QTRY_VERIFY_WITH_TIMEOUT(format.match(desktop.metrics()).hasMatch(), 7000);
    }

    void unknownRequestsAreIgnored() {
        QTemporaryDir dir;
        DesktopController desktop(dir.filePath("ignored.ini"));
        QSignalSpy errors(&desktop, &DesktopController::errorChanged);
        desktop.openSettings("kcm_unknown");
        desktop.trayAction("tray/none", "Activate");
        desktop.trayAction("tray/none", "Scroll", 120);
        desktop.activateTask(-1);
        desktop.taskMenu(-1);
        QCOMPARE(errors.count(), 0);
        QVERIFY(!desktop.menuOpen());
        QVERIFY(desktop.trayItems().isEmpty());
    }
};

QTEST_MAIN(WindowsDesktopTest)
#include "windows_desktop_test.moc"
