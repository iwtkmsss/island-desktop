#include "desktopcontroller.h"
#include "taskactivation.h"
#include "dbushelpers.h"
#include <taskmanager/abstracttasksmodel.h>
#include <KService>
#include <KIO/ApplicationLauncherJob>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QMenu>
#include <QCursor>
#include <QProcess>
#include <QTimer>
#include <QPainter>
#include <KWaylandExtras>
#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QGuiApplication>
#include <algorithm>
using Roles=TaskManager::AbstractTasksModel;
namespace {
const QString watcherService=QStringLiteral("org.kde.StatusNotifierWatcher");
QPair<QString,QString> address(const QString &id) {
    const int slash=id.indexOf('/');
    return slash<0 ? qMakePair(id,QStringLiteral("/StatusNotifierItem")) : qMakePair(id.left(slash),id.mid(slash));
}
QDBusMessage method(const QString &service,const QString &path,const QString &iface,const QString &name,const QVariantList &args={}) {
    auto m=QDBusMessage::createMethodCall(service,path,iface,name);m.setArguments(args);return m;
}
}
DesktopController::DesktopController(const QString &file,QObject *parent):QObject(parent),settings_(file,QSettings::IniFormat) {
    tasks_.classBegin();
    tasks_.setSortMode(TaskManager::TasksModel::SortManual);
    tasks_.setGroupMode(TaskManager::TasksModel::GroupApplications);
    tasks_.setGroupingWindowTasksThreshold(-1);
    tasks_.setLaunchInPlace(true);tasks_.setSeparateLaunchers(true);
    tasks_.setLauncherList(settings_.value("desktop/launchers").toStringList());
    tasks_.componentComplete();
    connect(&tasks_,&TaskManager::TasksModel::activeTaskChanged,this,[this]{
        const auto active=tasks_.activeTask();
        if(active.isValid()){recentActive_=active;activeLostAt_=0;}
        else if(!activeLostAt_)activeLostAt_=QDateTime::currentMSecsSinceEpoch();
    });
    connect(qGuiApp,&QGuiApplication::focusWindowChanged,this,[this](QWindow *window){
        if(window)focusGainedAt_=QDateTime::currentMSecsSinceEpoch();
        // Re-evaluate after the model has had a chance to report the deactivation.
        QTimer::singleShot(0,this,[this]{++taskRevision_;emit taskRevisionChanged();});
    });
    const auto bump=[this]{++taskRevision_;emit taskRevisionChanged();};
    connect(&tasks_,&QAbstractItemModel::dataChanged,this,bump);
    connect(&tasks_,&QAbstractItemModel::rowsInserted,this,bump);
    connect(&tasks_,&QAbstractItemModel::rowsRemoved,this,bump);
    connect(&tasks_,&QAbstractItemModel::rowsMoved,this,bump);
    connect(&tasks_,&QAbstractItemModel::modelReset,this,bump);
    connect(&tasks_,&TaskManager::TasksModel::activeTaskChanged,this,bump);
    connect(&tasks_,&TaskManager::TasksModel::launcherListChanged,this,[this]{settings_.setValue("desktop/launchers",tasks_.launcherList());settings_.sync();});
    auto bus=QDBusConnection::sessionBus();
    watcher_.setConnection(bus);watcher_.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);watcher_.addWatchedService(watcherService);
    connect(&watcher_,&QDBusServiceWatcher::serviceOwnerChanged,this,[this]{refreshTray();});
    for (const auto *signal:{"StatusNotifierItemRegistered","StatusNotifierItemUnregistered"})
        bus.connect(watcherService,"/StatusNotifierWatcher",watcherService,signal,this,SLOT(refreshTray()));
    // No replacement watcher: coexist with Plasma and recover when it restarts.
    auto *timer=new QTimer(this);timer->setInterval(5000);connect(timer,&QTimer::timeout,this,&DesktopController::refreshTray);timer->start();
    auto *metricsTimer=new QTimer(this);metricsTimer->setInterval(2000);
    connect(metricsTimer,&QTimer::timeout,this,[this]{
        if(!metricsActive_){cpuTotal_=cpuIdle_=0;return;}
        QFile cpu("/proc/stat"),mem("/proc/meminfo");
        QString cpuText=QStringLiteral("—"),ramText=QStringLiteral("—");
        if(cpu.open(QIODevice::ReadOnly)){
            const auto fields=cpu.readLine().simplified().split(' ');quint64 total=0,idle=0;
            for(int n=1;n<fields.size()&&n<=8;++n){auto v=fields[n].toULongLong();total+=v;if(n==4||n==5)idle+=v;}
            if(cpuTotal_&&total>cpuTotal_&&idle>=cpuIdle_)cpuText=QString::number(qBound(0,qRound(100.0*(1.0-double(idle-cpuIdle_)/double(total-cpuTotal_))),100))+"%";
            cpuTotal_=total;cpuIdle_=idle;
        }
        if(mem.open(QIODevice::ReadOnly)){
            quint64 total=0,available=0;bool hasAvailable=false;
            for(const auto &line:mem.readAll().split('\n')){const auto fields=line.simplified().split(' ');if(fields.size()<2)continue;
                if(fields[0]=="MemTotal:")total=fields[1].toULongLong();
                if(fields[0]=="MemAvailable:"){available=fields[1].toULongLong();hasAvailable=true;}
            }
            if(total&&hasAvailable&&available<=total){QLocale locale(QLocale::Ukrainian);ramText=locale.toString(double(total-available)/1048576.0,'f',1)+" / "+locale.toString(double(total)/1048576.0,'f',1)+QStringLiteral(" ГіБ");}
        }
        metrics_=QStringLiteral("CPU  %1   ·   RAM  %2").arg(cpuText,ramText);emit metricsChanged();
    });metricsTimer->start();
    refreshTray();
}
bool DesktopController::available() const{return true;}
QAbstractItemModel *DesktopController::tasks(){return &tasks_;}
void DesktopController::fail(const QString &m){error_=m;emit errorChanged();}
void DesktopController::showMenu(QMenu *menu){
    menu->setAttribute(Qt::WA_DeleteOnClose);menuOpen_=true;emit menuOpenChanged();
    connect(menu,&QMenu::aboutToHide,this,[this]{menuOpen_=false;emit menuOpenChanged();});menu->popup(QCursor::pos());
}
void DesktopController::activateTask(int row){
    IslandDesktop::activateTask(tasks_, tasks_, tasks_.index(row,0), displacedActive());
}
// Clicking the island can take focus and deactivate the clicked window first.
// Only trust the remembered task if the loss coincided with our focus gain.
QModelIndex DesktopController::displacedActive() const{
    if(!tasks_.activeTask().isValid()&&recentActive_.isValid()&&QGuiApplication::focusWindow()
        &&activeLostAt_&&qAbs(activeLostAt_-focusGainedAt_)<1000)return recentActive_;
    return {};
}
QVariantMap DesktopController::taskState(int row) const{
    const auto task=tasks_.index(row,0);
    if(!task.isValid())return {{"count",0},{"active",-1}};
    const auto displaced=displacedActive();
    const auto active=[&](const QModelIndex &i){return i.data(Roles::IsActive).toBool()||(displaced.isValid()&&i==displaced);};
    if(!task.data(Roles::IsGroupParent).toBool())
        return {{"count",task.data(Roles::IsLauncher).toBool()?0:1},{"active",active(task)?0:-1}};
    const int count=tasks_.rowCount(task);int current=-1;QDateTime latest;
    for(int r=0;r<count;++r){
        const auto child=tasks_.index(r,0,task);
        if(active(child)){current=r;break;}
        // The group itself was remembered: its most recently used child was the one.
        if(displaced.isValid()&&displaced==task){const auto t=child.data(Roles::LastActivated).toDateTime();
            if(t.isValid()&&(!latest.isValid()||t>latest)){latest=t;current=r;}}
    }
    return {{"count",count},{"active",current}};
}
void DesktopController::taskMenu(int row){
    QPersistentModelIndex i(tasks_.index(row,0));if(!i.isValid())return;
    auto *menu=new QMenu;
    QUrl url=i.data(Roles::LauncherUrlWithoutIcon).toUrl();
    // Installed Plasma 6.8 can dereference a null service for unregistered
    // local desktop files. Normalize registered entries before pinning.
    if(url.isLocalFile()&&url.path().endsWith(".desktop")){
        const auto service=KService::serviceByStorageId(url.toLocalFile());
        url=service&&!service->menuId().isEmpty()?QUrl("applications:"+service->menuId()):QUrl();
    }
    const bool pinned=i.data(Roles::HasLauncher).toBool()||i.data(Roles::IsLauncher).toBool();
    auto *pin=menu->addAction(pinned?QStringLiteral("Відкріпити"):QStringLiteral("Закріпити"),this,[this,url,pinned]{
        if(pinned)tasks_.requestRemoveLauncher(url);else tasks_.requestAddLauncher(url);});pin->setEnabled(url.isValid()&&!url.isEmpty());
    menu->addAction(QStringLiteral("Нове вікно"),this,[this,i]{if(i.isValid())tasks_.requestNewInstance(i);})->setEnabled(i.data(Roles::CanLaunchNewInstance).toBool());
    menu->addAction(QStringLiteral("Ліворуч"),this,[this,i]{if(i.isValid())moveTask(i.row(),i.row()-1);})->setEnabled(row>0);
    menu->addAction(QStringLiteral("Праворуч"),this,[this,i]{if(i.isValid())moveTask(i.row(),i.row()+1);})->setEnabled(row+1<tasks_.rowCount());
    showMenu(menu);
}
void DesktopController::moveTask(int from,int to){if(from<0||to<0||to>=tasks_.rowCount())return;tasks_.move(from,to);tasks_.syncLaunchers();}
void DesktopController::applicationMenu(){
    auto services=KService::allServices();
    std::sort(services.begin(),services.end(),[](const auto &a,const auto &b){return QString::localeAwareCompare(a->name(),b->name())<0;});
    auto *menu=new QMenu;QMap<QString,QMenu*> groups;
    const QList<QPair<QString,QString>> categories={{"AudioVideo","Мультимедіа"},{"Development","Розробка"},{"Education","Освіта"},{"Game","Ігри"},{"Graphics","Графіка"},{"Network","Інтернет"},{"Office","Офіс"},{"Settings","Налаштування"},{"System","Система"},{"Utility","Утиліти"}};
    for(const auto &c:categories)groups[c.first]=new QMenu(c.second,menu);
    auto *other=new QMenu(QStringLiteral("Інше"),menu);
    for(const auto &service:services){
        if(!service->isApplication()||service->noDisplay())continue;
        QMenu *group=other;for(const auto &c:categories)if(service->categories().contains(c.first)){group=groups[c.first];break;}
        group->addAction(QIcon::fromTheme(service->icon()),service->name(),this,[this,service]{
            auto *job=new KIO::ApplicationLauncherJob(service,this);
            connect(job,&KJob::result,this,[this,job]{if(job->error())fail(job->errorString());});job->start();});
    }
    for(const auto &c:categories)if(!groups[c.first]->isEmpty())menu->addMenu(groups[c.first]);
    if(!other->isEmpty())menu->addMenu(other);
    showMenu(menu);
}
void DesktopController::openSettings(const QString &module){
    static const QStringList allowed={"kcm_networkmanagement","kcm_bluetooth","kcm_pulseaudio","kcm_powerdevilprofilesconfig"};
    if(allowed.contains(module)&&!QProcess::startDetached("systemsettings",{module}))fail(QStringLiteral("Не вдалося відкрити налаштування KDE."));
}
void DesktopController::refreshTray(){
    if(menuOpen_)return;
    const auto gen=++generation_;
    auto m=method(watcherService,"/StatusNotifierWatcher","org.freedesktop.DBus.Properties","Get",{watcherService,"RegisteredStatusNotifierItems"});
    auto *call=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,1000),this);
    connect(call,&QDBusPendingCallWatcher::finished,this,[this,gen](QDBusPendingCallWatcher *call){
        QDBusPendingReply<QDBusVariant> reply=*call;call->deleteLater();if(gen!=generation_)return;
        if(reply.isError()){if(!items_.isEmpty()){items_.clear();icons_.clear();emit trayChanged();}return;}
        const auto ids=qdbus_cast<QStringList>(reply.value().variant());
        bool removed=false;
        for(int i=items_.size()-1;i>=0;--i)if(!ids.contains(items_[i].toMap().value("id").toString())){icons_.remove(items_[i].toMap().value("id").toString());items_.removeAt(i);removed=true;}
        if(removed)emit trayChanged();
        for(const auto &id:ids){
            const auto [service,path]=address(id);
            auto *p=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(method(service,path,"org.freedesktop.DBus.Properties","GetAll",{QStringLiteral("org.kde.StatusNotifierItem")}),1000),this);
            connect(p,&QDBusPendingCallWatcher::finished,this,[this,id,gen](QDBusPendingCallWatcher *p){
                QDBusPendingReply<QVariantMap> props=*p;p->deleteLater();if(gen!=generation_||props.isError())return;
                const auto data=props.value();QString name=data.value("IconName").toString();
                if(data.value("Status")=="NeedsAttention"&&!data.value("AttentionIconName").toString().isEmpty())name=data.value("AttentionIconName").toString();
                QIcon icon=QIcon::fromTheme(name);
                // SNI pixmaps are network-byte-order ARGB32, not native-endian words.
                if(icon.isNull()){
                    const auto arg=qvariant_cast<QDBusArgument>(data.value("IconPixmap"));
                    if(arg.currentType()==QDBusArgument::ArrayType){arg.beginArray();while(!arg.atEnd()){
                        int w=0,h=0;QByteArray bytes;arg.beginStructure();arg>>w>>h>>bytes;arg.endStructure();
                        if(w>0&&h>0&&w<=1024&&h<=1024&&bytes.size()==w*h*4){QImage image(w,h,QImage::Format_ARGB32);for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto *v=reinterpret_cast<const uchar*>(bytes.constData())+4*(y*w+x);image.setPixel(x,y,qRgba(v[1],v[2],v[3],v[0]));}icon.addPixmap(QPixmap::fromImage(image));}
                    }arg.endArray();}
                }
                const auto image=icon.pixmap(32,32).toImage().convertToFormat(QImage::Format_ARGB32);
                const size_t hash=image.isNull()?0:qHashBits(image.constBits(),image.sizeInBytes());
                const bool iconChanged=!iconHashes_.contains(id)||iconHashes_[id]!=hash;
                icons_[id]=icon;iconHashes_[id]=hash;
                QVariantMap item{{"menu",data.value("Menu")},{"itemIsMenu",data.value("ItemIsMenu")},{"id",id},{"title",data.value("Title").toString()},{"status",data.value("Status").toString()},{"revision",QVariant::fromValue(gen)}};
                int index=-1;for(int n=0;n<items_.size();++n)if(items_[n].toMap().value("id")==id){index=n;break;}
                if(index>=0&&!iconChanged)item["revision"]=items_[index].toMap().value("revision");
                if(index>=0&&items_[index].toMap()==item)return;
                if(index<0)items_.append(item);else items_[index]=item;emit trayChanged();
            });
        }
    });
}
void DesktopController::trayMenu(const QString &service,const QString &path){
    menuOpen_=true;emit menuOpenChanged();
    auto bus=QDBusConnection::sessionBus();
    bus.asyncCall(method(service,path,"com.canonical.dbusmenu","AboutToShow",{0}),1000);
    auto *call=new QDBusPendingCallWatcher(bus.asyncCall(method(service,path,"com.canonical.dbusmenu","GetLayout",{0,8,QStringList{}}),1000),this);
    connect(call,&QDBusPendingCallWatcher::finished,this,[this,service,path](QDBusPendingCallWatcher *call){
        const auto reply=call->reply();call->deleteLater();
        if(reply.type()==QDBusMessage::ErrorMessage||reply.arguments().size()<2){menuOpen_=false;emit menuOpenChanged();fail(QStringLiteral("Не вдалося прочитати меню трея."));return;}
        auto *menu=new QMenu;int remaining=500;
        std::function<void(const QDBusArgument&,QMenu*,int)> append;
        append=[&](const QDBusArgument &arg,QMenu *parent,int depth){
            if(depth>8||--remaining<0||arg.currentType()!=QDBusArgument::StructureType)return;
            int id;QVariantMap props;arg.beginStructure();arg>>id>>props;
            auto *sub=new QMenu(parent);arg.beginArray();while(!arg.atEnd()){
                QDBusVariant child;arg>>child;append(qvariant_cast<QDBusArgument>(child.variant()),sub,depth+1);
            }arg.endArray();arg.endStructure();
            if(id==0){parent->addActions(sub->actions());return;}
            if(!props.value("visible",true).toBool())return;
            if(props.value("type")=="separator"){parent->addSeparator();return;}
            QAction *action;
            if(!sub->isEmpty()){sub->setTitle(props.value("label").toString());action=parent->addMenu(sub);}
            else action=parent->addAction(props.value("label").toString(),this,[service,path,id]{
                QDBusConnection::sessionBus().asyncCall(method(service,path,"com.canonical.dbusmenu","Event",{id,QStringLiteral("clicked"),QVariant::fromValue(QDBusVariant(0)),uint(QDateTime::currentMSecsSinceEpoch())}),1000);
            });
            action->setEnabled(props.value("enabled",true).toBool());
            if(props.contains("toggle-type")){action->setCheckable(true);action->setChecked(props.value("toggle-state").toInt()==1);}
            if(props.contains("icon-name"))action->setIcon(QIcon::fromTheme(props.value("icon-name").toString()));
        };
        append(qvariant_cast<QDBusArgument>(reply.arguments()[1]),menu,0);
        if(menu->isEmpty()){delete menu;menuOpen_=false;emit menuOpenChanged();return;}showMenu(menu);
    });
}
void DesktopController::trayAction(const QString &id,const QString &action,int delta){
    QVariantMap item;for(const auto &entry:items_)if(entry.toMap().value("id")==id)item=entry.toMap();if(item.isEmpty())return;
    if(action!="Activate"&&action!="SecondaryActivate"&&action!="ContextMenu"&&action!="Scroll")return;
    const auto [service,path]=address(id);const auto pos=QCursor::pos();
    const auto menuPath=qvariant_cast<QDBusObjectPath>(item.value("menu")).path();
    if((action=="ContextMenu"||(action=="Activate"&&item.value("itemIsMenu").toBool()))&&!menuPath.isEmpty()&&menuPath!="/"){trayMenu(service,menuPath);return;}
    QVariantList args=action=="Scroll"?QVariantList{delta,QStringLiteral("vertical")}:QVariantList{pos.x(),pos.y()};
    const auto dispatch=[this,service,path,action,args](const QString &token){
        auto bus=QDBusConnection::sessionBus();
        if(!token.isEmpty())bus.asyncCall(method(service,path,"org.kde.StatusNotifierItem","ProvideXdgActivationToken",{token}),1000);
        auto *call=new QDBusPendingCallWatcher(bus.asyncCall(method(service,path,"org.kde.StatusNotifierItem",action,args),1000),this);
        connect(call,&QDBusPendingCallWatcher::finished,this,[this](QDBusPendingCallWatcher *call){QDBusPendingReply<> r=*call;call->deleteLater();if(r.isError())fail(r.error().message());});
    };
    if(QGuiApplication::platformName().startsWith("wayland")&&action!="Scroll")KWaylandExtras::xdgActivationToken(nullptr,{}).then(this,dispatch);
    else dispatch({});
}
