#pragma once
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class IslandSurface : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QColor surfaceColor MEMBER surfaceColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor inkColor MEMBER inkColor NOTIFY appearanceChanged)
    Q_PROPERTY(qreal radius MEMBER radius NOTIFY appearanceChanged)
    Q_PROPERTY(qreal pointerX MEMBER pointerX NOTIFY appearanceChanged)
    Q_PROPERTY(qreal pointerY MEMBER pointerY NOTIFY appearanceChanged)
    Q_PROPERTY(qreal proximity MEMBER proximity NOTIFY appearanceChanged)
    Q_PROPERTY(qreal phase MEMBER phase NOTIFY appearanceChanged)
    Q_PROPERTY(qreal wave MEMBER wave NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantList bands MEMBER bands NOTIFY appearanceChanged)
public:
    explicit IslandSurface(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    QColor surfaceColor = QColor("#060606"), inkColor = Qt::white;
    qreal radius = 26, pointerX = 0, pointerY = 0, proximity = 0, phase = 0, wave = 0;
    QVariantList bands;
signals:
    void appearanceChanged();
};
