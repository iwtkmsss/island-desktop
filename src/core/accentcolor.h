#pragma once
#include <QColor>
#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

// Dominant chromatic colour of an artwork image. Invalid for null or
// essentially greyscale/black/white artwork. Deterministic.
inline QColor accentFromImage(const QImage &source) {
    if (source.isNull()) return {};
    QImage image = source;
    if (image.width() > 48 || image.height() > 48)
        image = image.scaled(48, 48, Qt::KeepAspectRatio, Qt::FastTransformation);
    image = image.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) return {};

    constexpr int buckets = 24;
    struct Bucket { double weight = 0, sin = 0, cos = 0, saturation = 0, value = 0; };
    std::array<Bucket, buckets> histogram{};
    double total = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = QColor::fromRgba(line[x]);
            if (pixel.alpha() < 128) continue;
            const double s = pixel.hsvSaturationF(), v = pixel.valueF();
            if (s < 0.10 || v < 0.15 || (v > 0.95 && s < 0.25)) continue;
            const double hue = pixel.hsvHueF();
            if (hue < 0) continue;
            const double weight = s * v;
            const double radians = hue * 2 * std::numbers::pi;
            auto &bucket = histogram[std::clamp(static_cast<int>(hue * buckets), 0, buckets - 1)];
            bucket.weight += weight;
            bucket.sin += std::sin(radians) * weight;
            bucket.cos += std::cos(radians) * weight;
            bucket.saturation += s * weight;
            bucket.value += v * weight;
            total += weight;
        }
    }
    const double maximum = static_cast<double>(image.width()) * image.height();
    if (maximum <= 0 || total < 0.02 * maximum) return {};

    int best = 0;
    for (int i = 1; i < buckets; ++i)
        if (histogram[i].weight > histogram[best].weight) best = i;
    Bucket merged;
    for (int offset = -1; offset <= 1; ++offset) {
        const auto &b = histogram[(best + offset + buckets) % buckets];
        merged.weight += b.weight; merged.sin += b.sin; merged.cos += b.cos;
        merged.saturation += b.saturation; merged.value += b.value;
    }
    if (merged.weight <= 0) return {};
    double hue = std::atan2(merged.sin, merged.cos) / (2 * std::numbers::pi);
    if (hue < 0) hue += 1;
    if (hue >= 1) hue -= 1;
    return QColor::fromHsvF(hue, std::clamp(merged.saturation / merged.weight, 0.0, 1.0),
                            std::clamp(merged.value / merged.weight, 0.0, 1.0));
}

// Adapts the raw accent so a thin glow is visible but not garish on the theme.
inline QColor accentForSurface(const QColor &accent, bool lightTheme) {
    if (!accent.isValid()) return {};
    const QColor hsl = accent.toHsl();
    const double hue = hsl.hslHueF() < 0 ? 0.0 : hsl.hslHueF();
    const float saturation = std::clamp(static_cast<float>(hsl.hslSaturationF()), 0.45f, 0.90f);
    return QColor::fromHslF(static_cast<float>(hue), saturation, lightTheme ? 0.46f : 0.62f);
}
