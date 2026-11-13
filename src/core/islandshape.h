#pragma once
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <algorithm>

inline QRectF islandEllipse(const QRectF &body, const QString &edge) {
    if (edge == "bottom") return {body.left(), body.top(), body.width(), body.height()*2};
    if (edge == "left") return {body.left()-body.width(), body.top(), body.width()*2, body.height()};
    if (edge == "right") return {body.left(), body.top(), body.width()*2, body.height()};
    return {body.left(), body.top()-body.height(), body.width(), body.height()*2};
}
// Pure geometry shared by the painter and native hit testing.
inline QPainterPath islandBodyPath(const QRectF &body, qreal radius, const QString &edge, bool halfEllipse, qreal crown = 0) {
    QPainterPath path;
    if (body.isEmpty()) return path;
    if (halfEllipse) {
        path.addEllipse(islandEllipse(body, edge));
        QPainterPath clip; clip.addRect(body); return path.intersected(clip);
    }
    radius = std::clamp(radius, qreal(0), std::min(body.width(), body.height())/2);
    crown = std::clamp(crown, qreal(0), qreal(10));
    if (crown > 0) {
        const qreal cx = body.center().x(), top = body.top();
        const qreal half = std::min(qreal(44), std::max(qreal(0), body.width()/2-radius));
        path.moveTo(body.left()+radius, top);
        path.lineTo(cx-half, top);
        path.cubicTo(cx-half*0.58, top, cx-half*0.58, top-crown, cx, top-crown);
        path.cubicTo(cx+half*0.58, top-crown, cx+half*0.58, top, cx+half, top);
        path.lineTo(body.right()-radius, top);
        path.arcTo(body.right()-2*radius, top, 2*radius, 2*radius, 90, -90);
        path.lineTo(body.right(), body.bottom()-radius);
        path.arcTo(body.right()-2*radius, body.bottom()-2*radius, 2*radius, 2*radius, 0, -90);
        path.lineTo(body.left()+radius, body.bottom());
        path.arcTo(body.left(), body.bottom()-2*radius, 2*radius, 2*radius, -90, -90);
        path.lineTo(body.left(), top+radius);
        path.arcTo(body.left(), top, 2*radius, 2*radius, 180, -90);
        path.closeSubpath();
        return path;
    }
    path.addRoundedRect(body, radius, radius);
    QPainterPath join;
    if (edge == "top") join.addRect(QRectF(body.left(), body.top(), body.width(), radius));
    else if (edge == "bottom") join.addRect(QRectF(body.left(), body.bottom()-radius, body.width(), radius));
    else if (edge == "left") join.addRect(QRectF(body.left(), body.top(), radius, body.height()));
    else if (edge == "right") join.addRect(QRectF(body.right()-radius, body.top(), radius, body.height()));
    return join.isEmpty() ? path : path.united(join);
}
