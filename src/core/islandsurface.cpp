#include "islandsurface.h"
#include "islandshape.h"
#include "borrowed/audioanalysis.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <numbers>

IslandSurface::IslandSurface(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &IslandSurface::appearanceChanged, this, [this] { update(); });
}
void IslandSurface::setGlowColor(const QColor &color) {
    if (glowColor_ == color) return;
    glowColor_ = color;
    emit glowChanged();
    update();
}
void IslandSurface::setGlowStrength(qreal strength) {
    const qreal clamped = std::isfinite(strength) ? std::clamp(strength, 0.0, 1.0) : 0.0;
    if (glowStrength_ == clamped) return;
    glowStrength_ = clamped;
    emit glowChanged();
    update();
}
void IslandSurface::paint(QPainter *painter) {
    // Paint margin around the contour: room for the pointer push, the audio
    // hills and the glow stroke. Main.qml anchors the item with the same value.
    constexpr qreal margin = 22;
    const QRectF body(margin, margin, width() - 2 * margin, height() - 2 * margin);
    if (body.isEmpty()) return;
    const qreal cornerRadius = std::clamp(radius, 0.0, std::min(body.width(), body.height()) / 2);
    const auto base = islandBodyPath(body, cornerRadius, dockEdge, halfEllipse, crownHeight);
    // Attack/release is already applied by the analyzer. Filtering again per
    // paint delays beats and makes the response depend on the display FPS.
    std::array<double, IslandAudio::Analyzer::bandCount> spectrum{};
    const int bandCount = std::min(static_cast<int>(bands.size()), static_cast<int>(spectrum.size()));
    for (int i = 0; i < bandCount; ++i) {
        const double target = std::clamp(bands[i].toDouble(), 0.0, 1.0);
        spectrum[i] = target;
    }
    // Bass drives the whole edge: a kick lifts every hill, not only the left ones.
    const double bass = bandCount > 1 ? (spectrum[0] + spectrum[1]) / 2 : bandCount == 1 ? spectrum[0] : 0;
    // Players level out overall loudness, so bass share is what tells a calm
    // track from a heavy one: weak bass keeps the hills low, a kick lifts them.
    const double kick = 0.45 + 1.1 * std::pow(bass, 1.5);
    QPainterPath path;
    constexpr int samples = 200;
    const QPointF pointer(pointerX + margin, pointerY + margin);
    for (int i = 0; i <= samples; ++i) {
        QPointF p = base.pointAtPercent(static_cast<qreal>(i) / samples);
        const auto toward = pointer - p;
        const double distance = std::hypot(toward.x(), toward.y());
        // Push along the rounded surface's outward normal. Crossing the edge
        // never reverses the force or drags the contour inward toward the mouse.
        const QPointF cornerCentre(std::clamp(p.x(), body.left() + cornerRadius, body.right() - cornerRadius),
            std::clamp(p.y(), body.top() + cornerRadius, body.bottom() - cornerRadius));
        QPointF normal = p - cornerCentre;
        if (halfEllipse) {
            const auto ellipse = islandEllipse(body, dockEdge);
            normal = {(p.x()-ellipse.center().x()) / std::pow(ellipse.width()/2, 2),
                      (p.y()-ellipse.center().y()) / std::pow(ellipse.height()/2, 2)};
        }
        if (crownHeight > 0 && p.y() <= body.top()+0.01) {
            const double angle = base.angleAtPercent(static_cast<qreal>(i)/samples) * std::numbers::pi / 180;
            normal = {-std::sin(angle), -std::cos(angle)};
        }
        const bool attachedLine = (dockEdge == "top" && std::abs(p.y()-body.top()) < 0.01)
            || (dockEdge == "bottom" && std::abs(p.y()-body.bottom()) < 0.01)
            || (dockEdge == "left" && std::abs(p.x()-body.left()) < 0.01)
            || (dockEdge == "right" && std::abs(p.x()-body.right()) < 0.01);
        if (attachedLine) normal = {}; // The glued edge stays on the screen boundary.
        const double normalLength = std::hypot(normal.x(), normal.y());
        const double pressure = 5.5 * std::exp(-(distance * distance) / (2 * 32.0 * 32.0)) * proximity;
        if (normalLength > 0.001) p += normal * (pressure / normalLength);
        const bool vertical = dockEdge == "left" || dockEdge == "right";
        const qreal start = vertical ? body.top() : body.left();
        const qreal span = (vertical ? body.height() : body.width()) - 2*cornerRadius;
        const qreal along = vertical ? p.y() : p.x();
        const bool freeEdge = dockEdge == "bottom" ? p.y() < body.top()+2
            : dockEdge == "left" ? p.x() > body.right()-2
            : dockEdge == "right" ? p.x() < body.left()+2 : p.y() > body.bottom()-2;
        if (freeEdge && span > 0 && along > start+cornerRadius && along < start+cornerRadius+span) {
            const double u = (along-start-cornerRadius)/span;
            const double envelope = std::sin(u * std::numbers::pi);
            double magnitude = 0;
            if (bandCount > 0) {
                const double bin = u * (bandCount - 1);
                const int lo = std::clamp(static_cast<int>(bin), 0, bandCount - 1);
                const int hi = std::min(lo + 1, bandCount - 1);
                // Cosine interpolation gives rounded hills instead of straight ramps.
                const double weight = (1 - std::cos((bin - lo) * std::numbers::pi)) / 2;
                magnitude = spectrum[lo] * (1 - weight) + spectrum[hi] * weight;
            }
            // Hills only ever bulge outwards (down) and follow the spectrum. Loud
            // music keeps every band high, so the bands are contrasted (power > 1)
            // to keep separate hills instead of one flat bulge. A travelling ripple,
            // always positive, adds motion without dipping into the panel. Capped
            // to stay inside the paint margin together with the glow stroke.
            // Low frequencies sit on the left and get taller hills than the highs.
            const double shaped = std::pow(magnitude, 1.2) * (1.2 - 0.4 * u);
            const double ripple = 0.72 + 0.28 * std::sin(u * 6 * std::numbers::pi + phase);
            const double lift = std::min(13.0, wave * kick * envelope * shaped * ripple);
            if (dockEdge == "bottom") p.ry() -= lift;
            else if (dockEdge == "left") p.rx() += lift;
            else if (dockEdge == "right") p.rx() -= lift;
            else p.ry() += lift;
        }
        if (dockEdge == "top") p.setY(std::max(p.y(), body.top()));
        else if (dockEdge == "bottom") p.setY(std::min(p.y(), body.bottom()));
        else if (dockEdge == "left") p.setX(std::max(p.x(), body.left()));
        else if (dockEdge == "right") p.setX(std::min(p.x(), body.right()));
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
    if (glowStrength_ > 0 && glowColor_.isValid()) {
        auto soft = glowColor_; soft.setAlphaF(0.22 * glowStrength_);
        auto crisp = glowColor_; crisp.setAlphaF(0.85 * glowStrength_);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(soft, 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(path);
        painter->setPen(QPen(crisp, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(path);
    }
}
