#include "islandsurface.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <numbers>

IslandSurface::IslandSurface(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &IslandSurface::appearanceChanged, this, [this] { update(); });
}
void IslandSurface::paint(QPainter *painter) {
    const QRectF body(8, 8, width() - 16, height() - 16);
    if (body.isEmpty()) return;
    QPainterPath base;
    const qreal cornerRadius = std::clamp(radius, 0.0, std::min(body.width(), body.height()) / 2);
    base.addRoundedRect(body, cornerRadius, cornerRadius);
    QPainterPath path;
    constexpr int samples = 200;
    const QPointF pointer(pointerX + 8, pointerY + 8);
    for (int i = 0; i <= samples; ++i) {
        QPointF p = base.pointAtPercent(static_cast<qreal>(i) / samples);
        const auto toward = pointer - p;
        const double distance = std::hypot(toward.x(), toward.y());
        // Push along the rounded surface's outward normal. Crossing the edge
        // never reverses the force or drags the contour inward toward the mouse.
        const QPointF cornerCentre(std::clamp(p.x(), body.left() + cornerRadius, body.right() - cornerRadius),
            std::clamp(p.y(), body.top() + cornerRadius, body.bottom() - cornerRadius));
        const auto normal = p - cornerCentre;
        const double normalLength = std::hypot(normal.x(), normal.y());
        const double pressure = 5.5 * std::exp(-(distance * distance) / (2 * 32.0 * 32.0)) * proximity;
        if (normalLength > 0.001) p += normal * (pressure / normalLength);
        if (p.y() > body.bottom() - 2 && p.x() > body.left() + radius && p.x() < body.right() - radius) {
            const double u = (p.x() - body.left() - radius) / (body.width() - 2 * radius);
            const double envelope = std::sin(u * std::numbers::pi);
            double magnitude = 0;
            if (!bands.isEmpty()) {
                const double bin = u * (bands.size() - 1);
                const int lo = std::clamp(static_cast<int>(bin), 0, static_cast<int>(bands.size()) - 1);
                const int hi = std::min(lo + 1, static_cast<int>(bands.size()) - 1);
                const double weight = bin - lo;
                magnitude = std::clamp(bands[lo].toDouble() * (1 - weight) + bands[hi].toDouble() * weight, 0.0, 1.0);
            }
            p.ry() += wave * envelope * magnitude * (0.65 * std::sin(u * 7 * std::numbers::pi + phase)
                                      + 0.35 * std::sin(u * 13 * std::numbers::pi - phase * 0.7));
        }
        if (i == 0) path.moveTo(p); else path.lineTo(p);
    }
    path.closeSubpath();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillPath(path, surfaceColor);
    painter->save();
    painter->setClipPath(path);
    if (proximity > 0.005) {
        QRadialGradient highlight(pointer, 100);
        auto glow = inkColor; glow.setAlphaF(0.055 * std::clamp(proximity, 0.0, 1.0));
        highlight.setColorAt(0, glow); glow.setAlpha(0); highlight.setColorAt(1, glow);
        painter->fillRect(boundingRect(), highlight);
    }
    if (wave > 0) {
        QLinearGradient tint(0, body.bottom() - 38, 0, body.bottom());
        auto ink = inkColor; ink.setAlpha(0); tint.setColorAt(0, ink);
        ink.setAlpha(12); tint.setColorAt(1, ink);
        painter->fillRect(body, tint);
    }
    painter->restore();
    auto outline = inkColor; outline.setAlphaF(0.12 + 0.09 * std::clamp(proximity, 0.0, 1.0));
    painter->setPen(QPen(outline, 0.8)); painter->setBrush(Qt::NoBrush); painter->drawPath(path);
}
