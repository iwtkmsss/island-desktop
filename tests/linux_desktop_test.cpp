#include "desktopcontroller.h"
#include "taskactivation.h"
#include <QStandardItemModel>
#include <taskmanager/abstracttasksmodel.h>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QDBusVirtualObject>
#include <QDBusMessage>
#include <QApplication>
#include <QQuickView>
#include <QQmlComponent>
#include <QQuickItem>
#include <QFile>
#include <QMenu>
#include <QTemporaryDir>
#include <QtTest>
struct IconPixmap { int width=1,height=1; QByteArray bytes=QByteArray::fromHex("ffabcdef"); };
using IconPixmaps=QList<IconPixmap>;
Q_DECLARE_METATYPE(IconPixmap)
Q_DECLARE_METATYPE(IconPixmaps)
QDBusArgument &operator<<(QDBusArgument &a,const IconPixmap &p){a.beginStructure();a<<p.width<<p.height<<p.bytes;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,IconPixmap &p){a.beginStructure();a>>p.width>>p.height>>p.bytes;a.endStructure();return a;}
struct MenuNode { int id=0; QVariantMap props; QList<QDBusVariant> children; };
Q_DECLARE_METATYPE(MenuNode)
QDBusArgument &operator<<(QDBusArgument &a,const MenuNode &n){a.beginStructure();a<<n.id<<n.props<<n.children;a.endStructure();return a;}
const QDBusArgument &operator>>(const QDBusArgument &a,MenuNode &n){a.beginStructure();a>>n.id>>n.props>>n.children;a.endStructure();return a;}
class MenuServer:public QDBusVirtualObject {
public:
    int clicks=0;
    QString introspect(const QString&)const override{return {};}
    bool handleMessage(const QDBusMessage &m,const QDBusConnection &bus)override{
        if(m.member()=="AboutToShow")bus.send(m.createReply(QVariantList{false}));
        else if(m.member()=="GetLayout"){
            MenuNode child{7,{{"label","Test action"},{"enabled",true}}, {}};
            MenuNode root{0,{}, {QDBusVariant(QVariant::fromValue(child))}};
            bus.send(m.createReply(QVariantList{uint(1),QVariant::fromValue(root)}));
        }else if(m.member()=="Event"){++clicks;bus.send(m.createReply());}
        else return false;return true;
    }
};
class Watcher:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems MEMBER items)
public: QStringList items;
signals: void StatusNotifierItemRegistered(const QString &id);void StatusNotifierItemUnregistered(const QString &id);
};
class Item:public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface","org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)
    Q_PROPERTY(IconPixmaps IconPixmap READ pixmaps)
public:
    int activations=0,secondary=0,menus=0,scroll=0;
    QDBusObjectPath menuPath{QStringLiteral("/")};
    QDBusObjectPath menu()const{return menuPath;}
    QString title()const{return "Isolated test tray";}
    QString status()const{return "Active";}
    QString iconName()const{return {};}
    IconPixmaps pixmaps()const{return {{}};}
public slots:
    void Activate(int,int){++activations;}
    void SecondaryActivate(int,int){++secondary;}
    void ContextMenu(int,int){++menus;}
    void Scroll(int value,const QString &orientation){if(orientation=="vertical")scroll+=value;}
};
class DesktopTest:public QObject {
    Q_OBJECT
private slots:
    void trayLifecyclePixmapAndDispatch(){
        qDBusRegisterMetaType<MenuNode>();qDBusRegisterMetaType<IconPixmap>();qDBusRegisterMetaType<IconPixmaps>();
        auto bus=QDBusConnection::sessionBus();Watcher watcher;Item item;
        QVERIFY(bus.registerService("org.kde.StatusNotifierWatcher"));
        QVERIFY(bus.registerObject("/StatusNotifierWatcher",&watcher,QDBusConnection::ExportAllProperties|QDBusConnection::ExportAllSignals));
        QVERIFY(bus.registerService("org.example.IslandTrayTest"));
        QVERIFY(bus.registerObject("/StatusNotifierItem",&item,QDBusConnection::ExportAllProperties|QDBusConnection::ExportAllSlots));
        const QString id="org.example.IslandTrayTest/StatusNotifierItem";watcher.items={id};
        QTemporaryDir dir;DesktopController desktop(dir.filePath("settings.ini"));
        QTRY_COMPARE(desktop.trayItems().size(),1);
        QVERIFY(!desktop.trayIcon(id).isNull());
        QCOMPARE(desktop.trayIcon(id).pixmap(1,1).toImage().pixelColor(0,0),QColor(0xab,0xcd,0xef));
        desktop.trayAction(id,"Activate");QTRY_COMPARE(item.activations,1);
        desktop.trayAction(id,"SecondaryActivate");QTRY_COMPARE(item.secondary,1);
        desktop.trayAction(id,"ContextMenu");QTRY_COMPARE(item.menus,1);
        desktop.trayAction(id,"Scroll",120);QTRY_COMPARE(item.scroll,120);
        desktop.trayAction("org.example.other/StatusNotifierItem","Activate");QTest::qWait(20);QCOMPARE(item.activations,1);
        MenuServer menuServer;QVERIFY(bus.registerVirtualObject("/Menu",&menuServer));
        item.menuPath=QDBusObjectPath("/Menu");emit watcher.StatusNotifierItemRegistered(id);
        QTRY_COMPARE(qvariant_cast<QDBusObjectPath>(desktop.trayItems()[0].toMap().value("menu")).path(),QString("/Menu"));
        desktop.trayAction(id,"ContextMenu");
        QTRY_VERIFY(QApplication::activePopupWidget());
        auto *popup=qobject_cast<QMenu*>(QApplication::activePopupWidget());QVERIFY(popup);
        QCOMPARE(popup->actions().size(),1);QCOMPARE(popup->actions()[0]->text(),QString("Test action"));
        popup->actions()[0]->trigger();QTRY_COMPARE(menuServer.clicks,1);popup->close();QTRY_VERIFY(!desktop.menuOpen());
        bus.unregisterObject("/Menu");
        watcher.items.clear();emit watcher.StatusNotifierItemUnregistered(id);QTRY_VERIFY(desktop.trayItems().isEmpty());
        watcher.items={id};emit watcher.StatusNotifierItemRegistered(id);QTRY_COMPARE(desktop.trayItems().size(),1);
        bus.unregisterService("org.kde.StatusNotifierWatcher");QTRY_VERIFY(desktop.trayItems().isEmpty());
        bus.unregisterObject("/StatusNotifierWatcher");bus.unregisterObject("/StatusNotifierItem");bus.unregisterService("org.example.IslandTrayTest");
    }
    void windowClickMinimizesRestoresAndLaunches(){
        using Roles=TaskManager::AbstractTasksModel;
        QStandardItemModel model;model.appendRow(new QStandardItem("window"));
        const auto task=model.index(0,0);
        struct Commands {
            QStringList actions;
            void requestActivate(const QModelIndex &){actions<<"activate";}
            void requestToggleMinimized(const QModelIndex &){actions<<"toggle";}
        } commands;
        model.setData(task,true,Roles::IsWindow);model.setData(task,true,Roles::IsMinimizable);
        model.setData(task,true,Roles::IsActive);
        IslandDesktop::activateTask(model,commands,task);
        QCOMPARE(commands.actions,QStringList({"toggle"}));commands.actions.clear();
        model.setData(task,false,Roles::IsActive);model.setData(task,true,Roles::IsMinimized);
        IslandDesktop::activateTask(model,commands,task);
        QCOMPARE(commands.actions,QStringList({"toggle","activate"}));commands.actions.clear();
        model.setData(task,false,Roles::IsMinimized);
        IslandDesktop::activateTask(model,commands,task);
        QCOMPARE(commands.actions,QStringList({"activate"}));commands.actions.clear();
        model.setData(task,false,Roles::IsWindow);model.setData(task,true,Roles::IsLauncher);
        IslandDesktop::activateTask(model,commands,task);
        QCOMPARE(commands.actions,QStringList({"activate"}));commands.actions.clear();
        IslandDesktop::activateTask(model,commands,QModelIndex());QVERIFY(commands.actions.isEmpty());
        // The island took focus on click, so the window is no longer IsActive:
        // the remembered task must still minimize instead of re-activating.
        model.setData(task,true,Roles::IsWindow);model.setData(task,false,Roles::IsLauncher);
        IslandDesktop::activateTask(model,commands,task,task);
        QCOMPARE(commands.actions,QStringList({"toggle"}));commands.actions.clear();
    }
    void groupClickUsesRecentThenCyclesWithoutMenu(){
        using Roles=TaskManager::AbstractTasksModel;
        QStandardItemModel model;auto *group=new QStandardItem("group");model.appendRow(group);
        group->setData(true,Roles::IsGroupParent);
        for(int row=0;row<3;++row){auto *item=new QStandardItem(QString::number(row));group->appendRow(item);item->setData(row,Roles::StackingOrder);}
        struct Commands {
            QPersistentModelIndex activated;
            int minimized=0;
            void requestActivate(const QModelIndex &index){activated=index;}
            void requestToggleMinimized(const QModelIndex &){++minimized;}
        } commands;
        const auto parent=model.index(0,0);
        group->child(1)->setData(QDateTime::fromMSecsSinceEpoch(10000),Roles::LastActivated);
        group->child(2)->setData(QDateTime::fromMSecsSinceEpoch(5000),Roles::LastActivated);
        IslandDesktop::activateTask(model,commands,parent);QCOMPARE(commands.activated.row(),1);
        for(int active:{1,2,0,1}){
            for(int row=0;row<3;++row)group->child(row)->setData(row==active,Roles::IsActive);
            // MRU changes must not reorder the cycle while a child is active.
            group->child(active)->setData(QDateTime::currentDateTime(),Roles::LastActivated);
            IslandDesktop::activateTask(model,commands,parent);QCOMPARE(commands.activated.row(),(active+1)%3);
        }
        QCOMPARE(commands.minimized,0);
        // Focus moved to the island: the remembered child/group continues the cycle.
        for(int row=0;row<3;++row)group->child(row)->setData(false,Roles::IsActive);
        IslandDesktop::activateTask(model,commands,parent,model.index(0,0,parent));QCOMPARE(commands.activated.row(),1);
        for(int row=0;row<3;++row)group->child(row)->setData(QDateTime::fromMSecsSinceEpoch(row==1?30000:20000),Roles::LastActivated);
        IslandDesktop::activateTask(model,commands,parent,parent);QCOMPARE(commands.activated.row(),2); // child 1 is the most recent
        for(int row=0;row<3;++row){group->child(row)->setData(false,Roles::IsActive);group->child(row)->setData(QVariant(),Roles::LastActivated);}
        IslandDesktop::activateTask(model,commands,parent);QCOMPARE(commands.activated.row(),2); // stacking fallback
        group->removeRow(2);IslandDesktop::activateTask(model,commands,parent);QCOMPARE(commands.activated.row(),1);
        group->removeRows(0,2);commands.activated=QModelIndex();IslandDesktop::activateTask(model,commands,parent);QVERIFY(!commands.activated.isValid());
    }
    void actualApplicationMenuButton(){
        QTemporaryDir directory;DesktopController desktop(directory.filePath("ui.ini"));
        QQuickView view;view.setResizeMode(QQuickView::SizeRootObjectToView);view.resize(472,126);
        QQmlComponent colors(view.engine(),QUrl::fromLocalFile(QStringLiteral(ISLAND_SOURCE_DIR "/qml/IslandColors.qml")));
        std::unique_ptr<QObject> palette(colors.create());QVERIFY2(palette, qPrintable(colors.errorString()));
        QQmlComponent fake(view.engine());fake.setData("import QtQuick; QtObject { property bool available: false; property bool present: false; property int percent: 0; property int volume: 0; property bool muted: false }",QUrl());
        std::unique_ptr<QObject> status(fake.create());QVERIFY(status);
        view.setInitialProperties({{"colors",QVariant::fromValue(palette.get())},{"desktop",QVariant::fromValue(&desktop)},
            {"volume",QVariant::fromValue(status.get())},{"power",QVariant::fromValue(status.get())},{"bluetooth",QVariant::fromValue(status.get())}});
        view.setSource(QUrl::fromLocalFile(QStringLiteral(ISLAND_SOURCE_DIR "/qml/DesktopPanel.qml")));
        QVERIFY2(view.status()==QQuickView::Ready,qPrintable(view.errors().value(0).toString()));
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,{28,32});
        QTRY_VERIFY(desktop.menuOpen());
        auto *menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());QVERIFY(menu);QVERIFY(!menu->isEmpty());
        menu->close();QTRY_VERIFY(!desktop.menuOpen());
    }
    void launcherOrderSurvivesRestart(){
        QTemporaryDir dir;const auto settings=dir.filePath("state.ini");
        QStringList urls={"applications:org.kde.konsole.desktop","applications:org.kde.dolphin.desktop"};
        {
            DesktopController desktop(settings);auto *model=qobject_cast<TaskManager::TasksModel*>(desktop.tasks());QVERIFY(model);
            for(const auto &url:urls)model->requestAddLauncher(QUrl(url));
            QTRY_COMPARE(model->launcherList().size(),2);
            desktop.moveTask(0,1);QCOMPARE(model->launcherList(),QStringList({urls[1],urls[0]}));
        }
        {
            DesktopController desktop(settings);auto *model=qobject_cast<TaskManager::TasksModel*>(desktop.tasks());
            QCOMPARE(model->launcherList(),QStringList({urls[1],urls[0]}));
            model->requestRemoveLauncher(QUrl(urls[1]));QCOMPARE(model->launcherList(),QStringList({urls[0]}));
        }
        QSettings stored(settings,QSettings::IniFormat);QCOMPARE(stored.value("desktop/launchers").toStringList(),QStringList({urls[0]}));
    }
};
QTEST_MAIN(DesktopTest)
#include "linux_desktop_test.moc"
