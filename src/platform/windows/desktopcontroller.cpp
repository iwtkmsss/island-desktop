#include "desktopcontroller.h"
DesktopController::DesktopController(const QString &file,QObject *parent):QObject(parent),settings_(file,QSettings::IniFormat){}
bool DesktopController::available()const{return false;}
QAbstractItemModel *DesktopController::tasks(){return nullptr;}
void DesktopController::activateTask(int){}
QVariantMap DesktopController::taskState(int) const{return {};}
void DesktopController::taskMenu(int){}
void DesktopController::moveTask(int,int){}
void DesktopController::applicationMenu(){}
void DesktopController::trayAction(const QString&,const QString&,int){}
void DesktopController::openSettings(const QString&){}
void DesktopController::refreshTray(){}
void DesktopController::fail(const QString&){}
void DesktopController::showMenu(QMenu*){}

void DesktopController::trayMenu(const QString&,const QString&){}
