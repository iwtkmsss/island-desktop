#include "accentcolor.h"
#include <QtTest>
#include <cmath>

namespace {
QImage solid(const QColor &color, int size = 64) {
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}
double hueDegrees(const QColor &color) { return color.hsvHueF() * 360.0; }
double hueDistance(double a, double b) {
    const double d = std::fabs(a - b);
    return std::min(d, 360.0 - d);
}
}

class AccentTest final : public QObject {
    Q_OBJECT
private slots:
    void nullImageIsInvalid() { QVERIFY(!accentFromImage(QImage()).isValid()); }
    void achromaticArtworkIsInvalid_data() {
        QTest::addColumn<QColor>("color");
        QTest::newRow("grey") << QColor(128, 128, 128);
        QTest::newRow("black") << QColor(0, 0, 0);
        QTest::newRow("white") << QColor(255, 255, 255);
    }
    void achromaticArtworkIsInvalid() {
        QFETCH(QColor, color);
        QVERIFY(!accentFromImage(solid(color)).isValid());
    }
    void saturatedColoursKeepTheirHue_data() {
        QTest::addColumn<QColor>("color");
        QTest::addColumn<double>("hue");
        QTest::newRow("red") << QColor(255, 0, 0) << 0.0;
        QTest::newRow("green") << QColor(0, 200, 0) << 120.0;
        QTest::newRow("blue") << QColor(0, 0, 255) << 240.0;
    }
    void saturatedColoursKeepTheirHue() {
        QFETCH(QColor, color);
        QFETCH(double, hue);
        const auto accent = accentFromImage(solid(color));
        QVERIFY(accent.isValid());
        QVERIFY2(hueDistance(hueDegrees(accent), hue) <= 12.0, qPrintable(QString::number(hueDegrees(accent))));
    }
    void minorityOrangeOnDarkGreyIsPicked() {
        QImage image = solid(QColor(60, 60, 60), 50);
        for (int y = 0; y < 50; ++y)
            for (int x = 0; x < 15; ++x) image.setPixelColor(x, y, QColor(255, 128, 0));
        const auto accent = accentFromImage(image);
        QVERIFY(accent.isValid());
        QVERIFY2(hueDistance(hueDegrees(accent), 30.0) <= 12.0, qPrintable(QString::number(hueDegrees(accent))));
    }
    void strayPixelsAreIgnored() {
        QImage image = solid(QColor(120, 120, 120), 50);
        for (int i = 0; i < 20; ++i) image.setPixelColor(i, 0, QColor(255, 0, 0)); // 0.8%
        QVERIFY(!accentFromImage(image).isValid());
    }
    void hueWrapsAroundRed() {
        QImage image = solid(QColor(120, 120, 120), 40);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 20; ++x)
                image.setPixelColor(x, y, y % 2 ? QColor::fromHsvF(355.0 / 360, 1, 1) : QColor::fromHsvF(5.0 / 360, 1, 1));
        const auto accent = accentFromImage(image);
        QVERIFY(accent.isValid());
        QVERIFY2(hueDistance(hueDegrees(accent), 0.0) <= 12.0, qPrintable(QString::number(hueDegrees(accent))));
    }
    void transparentPixelsAreIgnored() {
        QImage image(40, 40, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QVERIFY(!accentFromImage(image).isValid());
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 20; ++x) image.setPixelColor(x, y, QColor(0, 0, 255));
        const auto accent = accentFromImage(image);
        QVERIFY(accent.isValid());
        QVERIFY(hueDistance(hueDegrees(accent), 240.0) <= 12.0);
        // Opaque-looking colour hidden behind alpha must not count.
        QImage hidden(40, 40, QImage::Format_ARGB32);
        hidden.fill(QColor(255, 0, 0, 0));
        QVERIFY(!accentFromImage(hidden).isValid());
    }
    void surfaceAdaptationRules() {
        QVERIFY(!accentForSurface(QColor(), false).isValid());
        QVERIFY(!accentForSurface(QColor(), true).isValid());
        const QColor vivid = QColor::fromHsvF(0.6, 1.0, 1.0);
        const auto dark = accentForSurface(vivid, false).toHsl();
        const auto light = accentForSurface(vivid, true).toHsl();
        QVERIFY(std::fabs(dark.lightnessF() - 0.62) < 0.01);
        QVERIFY(std::fabs(light.lightnessF() - 0.46) < 0.01);
        QVERIFY(dark.hslSaturationF() <= 0.90 + 0.01 && dark.hslSaturationF() >= 0.45 - 0.01);
        const QColor muted = QColor::fromHslF(0.1, 0.2, 0.5);
        QVERIFY(accentForSurface(muted, false).toHsl().hslSaturationF() >= 0.44);
        QVERIFY(hueDistance(accentForSurface(vivid, false).hueF() * 360, vivid.hueF() * 360) <= 3.0);
    }
};

QTEST_GUILESS_MAIN(AccentTest)
#include "accent_test.moc"
