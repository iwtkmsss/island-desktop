#include "hotkeymapping.h"
#include "startupentry.h"
#include <QtTest>

class SystemTest final : public QObject {
    Q_OBJECT
private slots:
    void startupCommandQuotesAndUsesNativeSeparators() {
        const auto command = startupCommand(QStringLiteral("C:/Apps/Island/IslandDesktop.exe"));
        QCOMPARE(command, QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QStringLiteral("C:/Apps/Island/IslandDesktop.exe"))));
        QVERIFY(command.startsWith(QLatin1Char('"')) && command.endsWith(QLatin1Char('"')));
        QVERIFY(!command.contains(QLatin1Char('/')) || QDir::separator() == QLatin1Char('/'));
    }
    void startupMatching_data() {
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("expected");
        QTest::newRow("quoted") << "\"C:\\Apps\\Island\\IslandDesktop.exe\"" << true;
        QTest::newRow("unquoted") << "C:\\Apps\\Island\\IslandDesktop.exe" << true;
        QTest::newRow("case") << "\"c:\\apps\\ISLAND\\islanddesktop.EXE\"" << true;
        QTest::newRow("forward slash") << "\"C:/Apps/Island/IslandDesktop.exe\"" << true;
        QTest::newRow("whitespace") << "  \"C:\\Apps\\Island\\IslandDesktop.exe\"  " << true;
        QTest::newRow("arguments") << "\"C:\\Apps\\Island\\IslandDesktop.exe\" --pin" << true;
        QTest::newRow("other exe") << "\"C:\\Apps\\Island\\Other.exe\"" << false;
        QTest::newRow("other dir") << "\"C:\\Other\\IslandDesktop.exe\"" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("blank") << "   " << false;
    }
    void startupMatching() {
        QFETCH(QString, value);
        QFETCH(bool, expected);
        QCOMPARE(startupCommandMatches(value, QStringLiteral("C:\\Apps\\Island\\IslandDesktop.exe")), expected);
    }
    void hotkeyTextAccepts() {
        QCOMPARE(hotkeyText(Qt::Key_Space, Qt::ControlModifier | Qt::AltModifier), QStringLiteral("Ctrl+Alt+Space"));
        QCOMPARE(hotkeyText(Qt::Key_F5, Qt::ControlModifier | Qt::ShiftModifier), QStringLiteral("Ctrl+Shift+F5"));
        QCOMPARE(hotkeyText(Qt::Key_A, Qt::MetaModifier), QStringLiteral("Meta+A"));
        QCOMPARE(hotkeyText(Qt::Key_7, Qt::AltModifier), QStringLiteral("Alt+7"));
        QCOMPARE(hotkeyText(Qt::Key_F24, Qt::ControlModifier), QStringLiteral("Ctrl+F24"));
        QVERIFY(!hotkeyText(Qt::Key_PageDown, Qt::ControlModifier).isEmpty());
        QVERIFY(!hotkeyText(Qt::Key_Return, Qt::AltModifier).isEmpty());
    }
    void hotkeyTextRejects() {
        QVERIFY(hotkeyText(Qt::Key_A, Qt::ShiftModifier).isEmpty());
        QVERIFY(hotkeyText(Qt::Key_A, Qt::NoModifier).isEmpty());
        QVERIFY(hotkeyText(Qt::Key_Control, Qt::ControlModifier).isEmpty());
        QVERIFY(hotkeyText(Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier).isEmpty());
        QVERIFY(hotkeyText(Qt::Key_Alt, Qt::AltModifier).isEmpty());
        QVERIFY(hotkeyText(Qt::Key_Meta, Qt::MetaModifier).isEmpty());
        QVERIFY(hotkeyText(Qt::Key_Escape, Qt::ControlModifier).isEmpty());
        QVERIFY(hotkeyText(Qt::Key_Comma, Qt::ControlModifier).isEmpty());
    }
    void nativeMapping() {
        auto h = nativeHotkey(QKeySequence::fromString(QStringLiteral("Ctrl+Alt+Space"), QKeySequence::PortableText));
        QVERIFY(h.valid);
        QCOMPARE(h.modifiers, 0x2u | 0x1u);
        QCOMPARE(h.virtualKey, 0x20u);
        h = nativeHotkey(QKeySequence::fromString(QStringLiteral("Ctrl+Shift+F5"), QKeySequence::PortableText));
        QVERIFY(h.valid);
        QCOMPARE(h.modifiers, 0x2u | 0x4u);
        QCOMPARE(h.virtualKey, 0x74u);
        h = nativeHotkey(QKeySequence::fromString(QStringLiteral("Meta+A"), QKeySequence::PortableText));
        QVERIFY(h.valid);
        QCOMPARE(h.modifiers, 0x8u);
        QCOMPARE(h.virtualKey, unsigned('A'));
    }
    void nativeRejects() {
        for (const auto &text : {QStringLiteral("Shift+A"), QStringLiteral("A"), QString(), QStringLiteral("Ctrl+A, Ctrl+B")})
            QVERIFY2(!nativeHotkey(QKeySequence::fromString(text, QKeySequence::PortableText)).valid, qPrintable(text));
    }
    void roundTrip() {
        const auto text = hotkeyText(Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
        const auto h = nativeHotkey(QKeySequence::fromString(text, QKeySequence::PortableText));
        QVERIFY(h.valid);
        QCOMPARE(h.virtualKey, unsigned('P'));
    }
};

QTEST_GUILESS_MAIN(SystemTest)
#include "system_test.moc"
