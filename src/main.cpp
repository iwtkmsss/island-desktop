#include "appstate.h"
#include "desktopcontroller.h"
#include "mediacontroller.h"
#include "shellcontroller.h"
#include "volumecontroller.h"
#include "audiocontroller.h"
#include "monitorcontroller.h"
#include "startupcontroller.h"
#include "hotkeycontroller.h"
#include "powercontroller.h"
#include "bluetoothcontroller.h"
#include "eventtext.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMenu>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <memory>
#include <QElapsedTimer>
#include <QQuickImageProvider>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <cstdio>
#ifdef Q_OS_LINUX
#include <LayerShellQt/Window>
#include <KIconTheme>
#endif

static QIcon islandIcon() {
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#111111"));
    painter.setPen(QPen(QColor("#c8c8c8"), 2));
    painter.drawRoundedRect(QRectF(3, 14, 58, 36), 18, 18);
    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(17, 29, 30, 6), 3, 3);
    return QIcon(pixmap);
}

class ArtworkProvider final : public QQuickImageProvider {
public:
    explicit ArtworkProvider(MediaController *media) : QQuickImageProvider(QQuickImageProvider::Image), media_(media) {}
    QImage requestImage(const QString &, QSize *size, const QSize &requested) override {
        auto image = media_->artwork();
        if (size) *size = image.size();
        if (!image.isNull() && requested.isValid()) image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    }
private:
    MediaController *media_;
};

class DesktopIconProvider final : public QQuickImageProvider {
public:
    explicit DesktopIconProvider(DesktopController *desktop):QQuickImageProvider(QQuickImageProvider::Image),desktop_(desktop){}
    QImage requestImage(const QString &id,QSize *size,const QSize &requested) override {
        const auto key=QUrl::fromPercentEncoding(id.section('?',0,0).toUtf8());
        const QSize dimensions=requested.isValid()?requested:QSize(32,32);
        auto icon=desktop_->trayIcon(key);
        if(icon.isNull()) icon=QIcon::fromTheme("application-x-executable");
        auto image=icon.pixmap(dimensions).toImage();
        if(image.isNull()){image=QImage(dimensions,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter p(&image);p.setPen(QPen(Qt::gray,2));p.drawRoundedRect(image.rect().adjusted(3,3,-3,-3),4,4);}
        // Uniform monochrome tray glyphs must contrast with the island's theme.
        int monochrome=0,colored=0,lowest=255,highest=0;
        for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(c.alpha()<20)continue;
            if(qMax(c.red(),qMax(c.green(),c.blue()))-qMin(c.red(),qMin(c.green(),c.blue()))>12){++colored;continue;}
            ++monochrome;lowest=qMin(lowest,c.red());highest=qMax(highest,c.red());
        }
        if(monochrome>0&&colored*4<=monochrome&&highest-lowest<65){const int tone=id.contains("&light")?30:235;
            image=image.convertToFormat(QImage::Format_ARGB32);
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);if(qMax(c.red(),qMax(c.green(),c.blue()))-qMin(c.red(),qMin(c.green(),c.blue()))<=12)image.setPixelColor(x,y,QColor(tone,tone,tone,c.alpha()));}
        }
        if(size) *size=image.size();
        return image;
    }
private: DesktopController *desktop_;
};

int main(int argc, char *argv[]) {
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
#ifdef Q_OS_LINUX
    // Keep launchers and autostart consistent with run.sh. Native Wayland can
    // be selected explicitly with QT_QPA_PLATFORM=wayland.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
#endif
    QQuickWindow::setDefaultAlphaBuffer(true);
    #ifdef Q_OS_LINUX
    KIconTheme::initTheme();
#endif
    QApplication app(argc, argv);
    app.setOrganizationName("IslandDesktop");
    app.setApplicationName("IslandDesktop");
    app.setApplicationDisplayName("Island Desktop");
    app.setApplicationVersion("0.3.1");
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(islandIcon());
#ifdef Q_OS_WIN
    app.setFont(QFont("Segoe UI Variable Text", 10));
#else
    app.setDesktopFileName("io.github.iwtkmsss.island-desktop");
#endif
    QQuickStyle::setStyle("Basic");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Island Desktop"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"settings", "Use a separate settings file (development).", "file"});
    parser.addOption({"expanded", "Open the expanded panel."});
    parser.addOption({"pin", "Keep the expanded panel open."});
    parser.addOption({"section", "Open home, time, media or devices (legacy: clock, timer, music, volume).", "id"});
    parser.addOption({"theme", "Use dark or light theme.", "theme"});
    parser.addOption({"smoke-test", "Load QML and exit automatically."});
    parser.addOption({"inspect-ui", "Expose a normal taskbar window for UI inspection (development)."});
    parser.addOption({"screenshot", "Save a rendered PNG and exit (development).", "file"});
    parser.addOption({"diagnostics", "Save local audio/volume/shell status JSON and exit (development).", "file"});
    parser.addOption({"capture-delay", "Delay diagnostic capture in milliseconds (development).", "milliseconds", "1800"});
    parser.process(app);

    auto settingsFile = parser.value("settings");
    if (settingsFile.isEmpty())
        settingsFile = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.ini";
    settingsFile = QFileInfo(settingsFile).absoluteFilePath();
    QDir().mkpath(QFileInfo(settingsFile).absolutePath());
    const auto identity = QCryptographicHash::hash(settingsFile.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    const auto serverName = QStringLiteral("IslandDesktop-%1").arg(QString::fromLatin1(identity));
    QLockFile lock(settingsFile + ".instance.lock");
    // QLockFile checks the owning process; a crashed process does not block future launches.
    if (!lock.tryLock(0)) {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(1000)) {
            socket.write("show");
            socket.waitForBytesWritten(1000);
            return 0;
        }
        qCritical("Another instance is starting, or the settings directory is not writable.");
        return 1;
    }
    QLocalServer::removeServer(serverName);
    QLocalServer instanceServer;
    if (!instanceServer.listen(serverName)) {
        qCritical("Could not create the local instance server.");
        return 1;
    }

    AppState state(settingsFile);
    if (parser.isSet("section")) state.setSection(parser.value("section"));
    if (parser.isSet("theme")) state.setTheme(parser.value("theme"));
    state.setExpanded(parser.isSet("expanded"));
    DesktopController desktop(settingsFile);
    MediaController media(settingsFile);
    QObject::connect(&desktop, &DesktopController::errorChanged, &state, [&] {
        if (!desktop.error().isEmpty()) state.postEvent({"desktop", desktop.error(), "notice", 40, 5000, "desktop-error"});
    });
    VolumeController volume;
    AudioController audio;
    MonitorController monitors(settingsFile);
    StartupController startup;
    HotkeyController hotkeys(settingsFile);
    // Development runs must not grab global keys.
    if (parser.isSet("smoke-test") || parser.isSet("screenshot") || parser.isSet("diagnostics"))
        hotkeys.setSuspended(true);
    PowerController power;
    BluetoothController bluetooth;
    ShellController interaction(&state);
    if (parser.isSet("pin")) interaction.setPinned(true);
    QObject::connect(&volume, &VolumeController::volumeEvent, &state, [&](int value, bool muted) {
        // The expanded sound/music pages already show their live controls.
        if (!state.expanded() || state.section() != "media")
            state.showVolumeEvent(value, muted);
    });

    QObject::connect(&power, &PowerController::powerEvent, &state, [&](int event, int percent) {
        state.postEvent(powerIslandEvent(static_cast<PowerEvent>(event), percent));
    });
    QObject::connect(&bluetooth, &BluetoothController::deviceEvent, &state,
        [&](const QString &id, const QString &name, bool connected, int battery) {
            state.postEvent(deviceIslandEvent(id, name, connected, battery));
        });

    QQmlApplicationEngine engine;
    int qmlWarningCount = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError> &warnings) {
        qmlWarningCount += warnings.size();
    });
    engine.addImageProvider("media", new ArtworkProvider(&media));
    engine.addImageProvider("desktop", new DesktopIconProvider(&desktop));
    QVariantMap initialProperties{{"desktop", QVariant::fromValue(&desktop)}, {"model", QVariant::fromValue(&state)},
        {"media", QVariant::fromValue(&media)}, {"interaction", QVariant::fromValue(&interaction)},
        {"volume", QVariant::fromValue(&volume)}, {"audio", QVariant::fromValue(&audio)},
        {"monitors", QVariant::fromValue(&monitors)},
        {"power", QVariant::fromValue(&power)}, {"bluetooth", QVariant::fromValue(&bluetooth)}};
#ifdef Q_OS_LINUX
    initialProperties.insert("visible", false);
#endif
    engine.setInitialProperties(initialProperties);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
    engine.loadFromModule("Island", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (qEnvironmentVariableIsSet("ISLAND_AUDIO_TRACE") && window) {
        // Development trace: worst interval between presented frames per second.
        auto *clock = new QElapsedTimer; clock->start();
        auto last = std::make_shared<qint64>(0), worst = std::make_shared<qint64>(0), reported = std::make_shared<qint64>(0);
        auto frames = std::make_shared<int>(0);
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [=] {
            const auto now = clock->elapsed();
            if (*last) *worst = std::max(*worst, now - *last);
            *last = now; ++*frames;
            if (now - *reported >= 1000) {
                std::fprintf(stderr, "audio-trace render fps=%d frame-gap-max=%lldms\n", *frames, (long long)*worst);
                *worst = 0; *reported = now; *frames = 0;
            }
        }, Qt::DirectConnection);
    }
    if (!window) return 1;
#ifdef Q_OS_LINUX
    if (QGuiApplication::platformName().startsWith("wayland")) {
        auto *layer = LayerShellQt::Window::get(window);
        layer->setScope("island-desktop");
        layer->setScreen(window->screen());
        QObject::connect(window, &QWindow::screenChanged, layer, &LayerShellQt::Window::setScreen);
        const auto anchor = [&monitors, layer] {
            const auto edge = monitors.dockEdge();
            layer->setAnchors(edge == "bottom" ? LayerShellQt::Window::AnchorBottom
                : edge == "left" ? LayerShellQt::Window::AnchorLeft
                : edge == "right" ? LayerShellQt::Window::AnchorRight : LayerShellQt::Window::AnchorTop);
        };
        anchor();
        QObject::connect(&monitors, &MonitorController::dockEdgeChanged, layer, anchor);
        layer->setLayer(LayerShellQt::Window::LayerTop);
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
        layer->setActivateOnShow(false);
        layer->setCloseOnDismissed(false);
    }
#endif
    window->setProperty("pointerBridgeKey", QString::fromLatin1(identity));
    interaction.attach(window);
    if (parser.isSet("inspect-ui")) {
        window->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        window->show();
    }
    QObject::connect(&interaction, &ShellController::frameRequested, &monitors, &MonitorController::position);
    monitors.attach(window);
#ifdef Q_OS_LINUX
    window->show();
#endif

    engine.setInitialProperties({{"model", QVariant::fromValue(&state)}, {"media", QVariant::fromValue(&media)},
        {"monitors", QVariant::fromValue(&monitors)}, {"startup", QVariant::fromValue(&startup)}, {"hotkeys", QVariant::fromValue(&hotkeys)}});
    engine.loadFromModule("Island", "SettingsWindow");
    if (engine.rootObjects().size() != 2) return 1;
    auto *settingsWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().last());
    if (!settingsWindow) return 1;
    settingsWindow->setTransientParent(nullptr);

    const auto showIsland = [&] {
        if (state.passThroughActive()) return;
        window->show();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &app, [&] {
        while (auto *socket = instanceServer.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket] { socket->readAll(); socket->disconnectFromServer(); });
            interaction.open();
            showIsland();
        }
    });

    QSystemTrayIcon tray(islandIcon());
    tray.setToolTip("Island Desktop");
    QMenu menu;
    menu.addAction(QStringLiteral("Відкрити острівець"), &app, [&] {
        interaction.open();
        showIsland();
    });
    menu.addAction(QStringLiteral("Приховати острівець"), &app, [&] {
        state.setExpanded(false);
        window->hide();
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Налаштування"), &app, [&] {
        state.setSettingsOpen(true);
        settingsWindow->show(); settingsWindow->raise(); settingsWindow->requestActivate();
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Вийти"), &app, &QCoreApplication::quit);
    tray.setContextMenu(&menu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &app, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            if (!window->isVisible() || !state.expanded()) interaction.open();
            else interaction.close();
            showIsland();
        }
    });
    QObject::connect(&state, &AppState::timerFinished, &tray, [&] {
        tray.showMessage(QStringLiteral("Час вийшов"), QStringLiteral("Таймер Island Desktop завершено."), QSystemTrayIcon::Information, 8000);
    });
    if (QSystemTrayIcon::isSystemTrayAvailable()) tray.show();

    // Global hotkeys. The explicit toggle also overrides a manual tray hide.
    QObject::connect(&hotkeys, &HotkeyController::toggleRequested, &app, [&] {
        if (state.expanded()) {
            interaction.setPinned(false);
            interaction.close();
        } else {
            interaction.open();
            interaction.setPinned(true);
            showIsland();
        }
    });
    QObject::connect(&hotkeys, &HotkeyController::playPauseRequested, &app, [&] {
        if (media.canToggle() && !media.busy()) media.togglePlayback();
    });

    if (parser.isSet("screenshot") || parser.isSet("diagnostics") || parser.isSet("smoke-test")) {
        bool validDelay = false;
        const int requestedDelay = parser.value("capture-delay").toInt(&validDelay);
        const int delay = parser.isSet("diagnostics") && validDelay ? qBound(100, requestedDelay, 15000) : 1200;
        QTimer::singleShot(delay, &app, [&] {
            bool saved = qmlWarningCount == 0;
            if (parser.isSet("screenshot")) {
                const auto path = QFileInfo(parser.value("screenshot")).absoluteFilePath();
                QDir().mkpath(QFileInfo(path).absolutePath());
                auto *captureWindow = settingsWindow->isVisible() ? settingsWindow : window;
                saved = captureWindow->grabWindow().save(path) && saved;
            }
            if (parser.isSet("diagnostics")) {
                const auto path = QFileInfo(parser.value("diagnostics")).absoluteFilePath();
                QDir().mkpath(QFileInfo(path).absolutePath());
                const QJsonObject report{{"version", app.applicationVersion()}, {"section", state.section()},
                    {"platform", QGuiApplication::platformName()},
                    {"desktop", QJsonObject{{"available", desktop.available()}, {"tasks", desktop.tasks() ? desktop.tasks()->rowCount() : 0},
                        {"trayItems", int(desktop.trayItems().size())}, {"menuOpen", desktop.menuOpen()}, {"metrics", desktop.metrics()}, {"error", desktop.error()}}},
                    {"media", QJsonObject{{"available", media.available()}, {"playing", media.playing()},
                        {"canToggle", media.canToggle()}, {"canSeek", media.canSeek()},
                        {"sourcesCount", int(media.sources().size())}, {"error", media.error()}}},
                    {"monitors", QJsonObject{{"selectedId", monitors.selectedId()}, {"dockEdge", monitors.dockEdge()},
                        {"frameX", window->x()}, {"frameY", window->y()}, {"frameWidth", window->width()}, {"frameHeight", window->height()},
                        {"screenX", window->screen() ? window->screen()->geometry().x() : 0},
                        {"screenY", window->screen() ? window->screen()->geometry().y() : 0},
                        {"screenWidth", window->screen() ? window->screen()->geometry().width() : 0},
                        {"screenHeight", window->screen() ? window->screen()->geometry().height() : 0},
                        {"screenName", window->screen() ? window->screen()->name() : QString{}},
                        {"optionsCount", static_cast<int>(monitors.options().size())}}},
                    {"volume", QJsonObject{{"available", volume.available()}, {"percent", volume.volume()}, {"muted", volume.muted()}, {"device", volume.deviceName()}, {"error", volume.error()}}},
                    {"audio", QJsonObject{{"enabled", audio.enabled()}, {"available", audio.available()}, {"active", audio.active()}, {"level", audio.level()}, {"bands", QJsonArray::fromVariantList(audio.bands())}, {"error", audio.error()}}},
                    {"power", QJsonObject{{"present", power.present()}, {"percent", power.percent()}, {"charging", power.charging()}}},
                    {"bluetooth", QJsonObject{{"available", bluetooth.available()}, {"connectedCount", bluetooth.connectedCount()}, {"deviceCount", static_cast<int>(bluetooth.devices().size())}}},
                    {"shell", QJsonObject{{"expanded", state.expanded()}, {"passThrough", state.passThroughActive()},
                        {"passThroughSeconds", state.passThroughSeconds()},
                        {"shellWidth", window->property("shellWidth").toDouble()}, {"shellHeight", window->property("shellHeight").toDouble()},
                        {"shellX", window->property("shellX").toDouble()}, {"shellY", window->property("shellY").toDouble()}, {"islandVisible", window->isVisible()},
                        {"settingsVisible", settingsWindow->isVisible()}, {"pinned", interaction.pinned()}, {"suppressed", interaction.property("suppressed").toBool()},
                        {"globalPointer", window->property("globalPointerAvailable").toBool()}, {"pointerActivity", qint64(interaction.pointerActivity())}}}};
                QSaveFile file(path);
                const bool opened = file.open(QIODevice::WriteOnly);
                if (opened) { file.write(QJsonDocument(report).toJson()); saved = file.commit() && saved; }
                else saved = false;
            }
            app.exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}
