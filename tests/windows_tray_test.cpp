// Behavioural tests for TrayHost. They use a private window class with
// forwarding and announcing disabled, so the real notification area is never
// touched: packets are marshalled by hand and sent straight to the host window.
#include "trayhost.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest>

#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr UINT kCallback = WM_APP + 7;
constexpr wchar_t kOwnerClass[] = L"IslandDesktopTrayTestOwner";

// Same layout as the wire format parsed by TrayHost (handles are 32-bit).
struct WireNid {
    std::uint32_t cbSize;
    std::uint32_t hWnd;
    std::uint32_t uID;
    std::uint32_t uFlags;
    std::uint32_t uCallbackMessage;
    std::uint32_t hIcon;
    char16_t szTip[128];
    std::uint32_t dwState;
    std::uint32_t dwStateMask;
    char16_t szInfo[256];
    std::uint32_t uTimeoutOrVersion;
    char16_t szInfoTitle[64];
    std::uint32_t dwInfoFlags;
    GUID guidItem;
    std::uint32_t hBalloonIcon;
};
static_assert(sizeof(WireNid) == 956);

struct Packet {
    std::uint32_t signature = 0x34753423;
    std::uint32_t message = 0;
    WireNid nid{};
};

struct Received {
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
};
std::vector<Received> g_received;

LRESULT CALLBACK ownerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == kCallback) {
        g_received.push_back({m, w, l});
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

std::uint32_t wire(const void *handle) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(handle));
}

HICON solidIcon(QRgb colour) {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = 16;
    header.bV5Height = -16;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP colourBitmap = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO *>(&header), DIB_RGB_COLORS, &bits,
                                            nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!colourBitmap || !bits) return nullptr;
    auto *pixels = static_cast<std::uint32_t *>(bits);
    for (int i = 0; i < 16 * 16; ++i) pixels[i] = colour;
    const unsigned char maskBits[32] = {}; // 16 rows, word aligned, all opaque
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, maskBits);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = colourBitmap;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(colourBitmap);
    return icon;
}

} // namespace

class WindowsTrayTest : public QObject {
    Q_OBJECT
private:
    HWND owner_ = nullptr;
    HICON blue_ = nullptr;
    HICON red_ = nullptr;

    static TrayHost::Options privateOptions() {
        TrayHost::Options options;
        options.windowClass = QStringLiteral("IslandDesktopTrayTest");
        options.forwardToShell = false;
        options.announce = false;
        return options;
    }

    static HWND createOwner() {
        return CreateWindowExW(0, kOwnerClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr),
                               nullptr);
    }

    static LRESULT sendRaw(const TrayHost &host, const void *data, DWORD size, ULONG_PTR kind = 1) {
        COPYDATASTRUCT cds{};
        cds.dwData = kind;
        cds.cbData = size;
        cds.lpData = const_cast<void *>(data);
        return SendMessageW(reinterpret_cast<HWND>(host.windowHandle()), WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
    }

    static LRESULT send(const TrayHost &host, DWORD message, const WireNid &nid) {
        Packet packet;
        packet.message = message;
        packet.nid = nid;
        return sendRaw(host, &packet, sizeof(packet));
    }

    WireNid nid(UINT uid, UINT flags, HICON icon = nullptr, const char16_t *tip = nullptr, HWND owner = nullptr) const {
        WireNid n{};
        n.cbSize = sizeof(WireNid);
        n.hWnd = wire(owner ? owner : owner_);
        n.uID = uid;
        n.uFlags = flags;
        n.uCallbackMessage = kCallback;
        n.hIcon = wire(icon);
        if (tip) {
            for (int i = 0; i < 127 && tip[i]; ++i) n.szTip[i] = tip[i];
        }
        return n;
    }

    static QString onlyId(const TrayHost &host) {
        const auto icons = host.icons();
        return icons.size() == 1 ? icons.first().id : QString();
    }

private slots:
    void initTestCase() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &ownerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kOwnerClass;
        QVERIFY(RegisterClassExW(&wc) != 0);
        blue_ = solidIcon(0xFF2080C0);
        red_ = solidIcon(0xFFE01010);
        QVERIFY(blue_);
        QVERIFY(red_);
    }

    void cleanupTestCase() {
        DestroyIcon(blue_);
        DestroyIcon(red_);
        UnregisterClassW(kOwnerClass, GetModuleHandleW(nullptr));
    }

    void init() {
        g_received.clear();
        owner_ = createOwner();
        QVERIFY(owner_);
    }

    void cleanup() {
        if (owner_ && IsWindow(owner_)) DestroyWindow(owner_);
        owner_ = nullptr;
    }

    void addShowsTooltipAndPixels() {
        TrayHost host(privateOptions());
        QSignalSpy changed(&host, &TrayHost::iconsChanged);
        QVERIFY(host.start());
        QVERIFY(host.running());
        QVERIFY(host.windowHandle() != 0);
        QCOMPARE(FindWindowW(L"IslandDesktopTrayTest", nullptr), reinterpret_cast<HWND>(host.windowHandle()));

        QCOMPARE(send(host, NIM_ADD, nid(5, NIF_MESSAGE | NIF_ICON | NIF_TIP, blue_, u"Blue app")), LRESULT(TRUE));
        const auto icons = host.icons();
        QCOMPARE(icons.size(), 1);
        QCOMPARE(icons.first().title, QStringLiteral("Blue app"));
        QVERIFY(!icons.first().id.isEmpty());
        QCOMPARE(QUrl::toPercentEncoding(icons.first().id), icons.first().id.toUtf8());
        const QImage image = icons.first().image;
        QCOMPARE(image.size(), QSize(16, 16));
        QCOMPARE(image.format(), QImage::Format_ARGB32);
        QCOMPARE(image.pixel(8, 8), qRgba(0x20, 0x80, 0xC0, 0xFF));
        QVERIFY(icons.first().imageRevision != 0);
        QTRY_COMPARE(changed.count(), 1);
    }

    void modifyChangesTipAndRevisionOnlyOnNewPixels() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        send(host, NIM_ADD, nid(1, NIF_MESSAGE | NIF_ICON | NIF_TIP, blue_, u"First"));
        const quint64 first = host.icons().first().imageRevision;

        QCOMPARE(send(host, NIM_MODIFY, nid(1, NIF_TIP, nullptr, u"Second")), LRESULT(TRUE));
        QCOMPARE(host.icons().first().title, QStringLiteral("Second"));
        QCOMPARE(host.icons().first().imageRevision, first);

        // Same pixels through a different handle: no new revision.
        HICON sameColour = solidIcon(0xFF2080C0);
        send(host, NIM_MODIFY, nid(1, NIF_ICON, sameColour));
        DestroyIcon(sameColour);
        QCOMPARE(host.icons().first().imageRevision, first);

        send(host, NIM_MODIFY, nid(1, NIF_ICON, red_));
        const auto icon = host.icons().first();
        QVERIFY(icon.imageRevision != first);
        QCOMPARE(icon.image.pixel(3, 3), qRgba(0xE0, 0x10, 0x10, 0xFF));
        QCOMPARE(icon.title, QStringLiteral("Second"));
    }

    void modifyOfUnknownIconAddsOnlyWithIconOrCallback() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        QCOMPARE(send(host, NIM_MODIFY, nid(2, NIF_TIP, nullptr, u"Tip only")), LRESULT(FALSE));
        QCOMPARE(host.icons().size(), 0);
        send(host, NIM_MODIFY, nid(2, NIF_ICON | NIF_TIP, blue_, u"Late"));
        QCOMPARE(host.icons().size(), 1);
        QCOMPARE(host.icons().first().title, QStringLiteral("Late"));
    }

    void hiddenStateHidesIcon() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        send(host, NIM_ADD, nid(3, NIF_MESSAGE | NIF_ICON, blue_));
        WireNid hide = nid(3, NIF_STATE);
        hide.dwState = NIS_HIDDEN;
        hide.dwStateMask = NIS_HIDDEN;
        send(host, NIM_MODIFY, hide);
        QCOMPARE(host.icons().size(), 0);
        WireNid show = nid(3, NIF_STATE);
        show.dwStateMask = NIS_HIDDEN;
        send(host, NIM_MODIFY, show);
        QCOMPARE(host.icons().size(), 1);
    }

    void legacyTriggerSendsUidAndMouseMessages() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        send(host, NIM_ADD, nid(42, NIF_MESSAGE | NIF_ICON, blue_));
        const QString id = onlyId(host);
        QVERIFY(!id.isEmpty());

        QVERIFY(host.trigger(id, TrayHost::Action::Activate, QPoint(10, 20)));
        QTRY_COMPARE(g_received.size(), std::size_t(2));
        QTest::qWait(30);
        QCOMPARE(g_received.size(), std::size_t(2)); // no NIN_SELECT before version 3
        QCOMPARE(g_received[0].wParam, WPARAM(42));
        QCOMPARE(g_received[0].lParam, LPARAM(WM_LBUTTONDOWN));
        QCOMPARE(g_received[1].wParam, WPARAM(42));
        QCOMPARE(g_received[1].lParam, LPARAM(WM_LBUTTONUP));

        g_received.clear();
        QVERIFY(host.trigger(id, TrayHost::Action::SecondaryActivate, QPoint()));
        QTRY_COMPARE(g_received.size(), std::size_t(2));
        QCOMPARE(g_received[0].lParam, LPARAM(WM_MBUTTONDOWN));
        QCOMPARE(g_received[1].lParam, LPARAM(WM_MBUTTONUP));
    }

    void ownThreadContextMenuCallbacksAreDeferred() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        send(host, NIM_ADD, nid(9, NIF_MESSAGE | NIF_ICON, blue_));
        WireNid version = nid(9, 0);
        version.uTimeoutOrVersion = NOTIFYICON_VERSION_4;
        QCOMPARE(send(host, NIM_SETVERSION, version), LRESULT(TRUE));

        // owner_ was created on this test/GUI thread, like Island's own tray
        // icon. Its menu callback must run after the initiating input returns.
        QVERIFY(host.trigger(onlyId(host), TrayHost::Action::ContextMenu, QPoint(10, 20)));
        QVERIFY(g_received.empty());
        QTRY_COMPARE(g_received.size(), std::size_t(3));
        const UINT expected[] = {WM_RBUTTONDOWN, WM_RBUTTONUP, WM_CONTEXTMENU};
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(UINT(LOWORD(g_received[i].lParam)), expected[i]);
            QCOMPARE(UINT(HIWORD(g_received[i].lParam)), 9u);
        }
    }

    void versionFourTriggerPacksCursorAndUid() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        send(host, NIM_ADD, nid(9, NIF_MESSAGE | NIF_ICON, blue_));
        WireNid version = nid(9, 0);
        version.uTimeoutOrVersion = NOTIFYICON_VERSION_4;
        QCOMPARE(send(host, NIM_SETVERSION, version), LRESULT(TRUE));
        const QString id = onlyId(host);

        QVERIFY(host.trigger(id, TrayHost::Action::Activate, QPoint(100, 200)));
        QTRY_COMPARE(g_received.size(), std::size_t(3));
        const UINT expected[] = {WM_LBUTTONDOWN, WM_LBUTTONUP, NIN_SELECT};
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(UINT(LOWORD(g_received[i].lParam)), expected[i]);
            QCOMPARE(UINT(HIWORD(g_received[i].lParam)), 9u);
            QCOMPARE(int(LOWORD(g_received[i].wParam)), 100);
            QCOMPARE(int(HIWORD(g_received[i].wParam)), 200);
        }

        g_received.clear();
        QVERIFY(host.trigger(id, TrayHost::Action::ContextMenu, QPoint(-5, 7)));
        QTRY_COMPARE(g_received.size(), std::size_t(3));
        QCOMPARE(UINT(LOWORD(g_received[0].lParam)), UINT(WM_RBUTTONDOWN));
        QCOMPARE(UINT(LOWORD(g_received[1].lParam)), UINT(WM_RBUTTONUP));
        QCOMPARE(UINT(LOWORD(g_received[2].lParam)), UINT(WM_CONTEXTMENU));
        QCOMPARE(short(LOWORD(g_received[2].wParam)), short(-5));
        QCOMPARE(short(HIWORD(g_received[2].wParam)), short(7));
    }

    void unknownIdAndMissingCallbackReturnFalse() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        QVERIFY(!host.trigger(QStringLiteral("nope"), TrayHost::Action::Activate, QPoint()));
        send(host, NIM_ADD, nid(4, NIF_ICON, blue_)); // no NIF_MESSAGE
        QVERIFY(!host.trigger(onlyId(host), TrayHost::Action::Activate, QPoint()));
        QCOMPARE(g_received.size(), std::size_t(0));
    }

    void guidIdentityAndDelete() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        const GUID guid = {0x12345678, 0x9abc, 0xdef0, {1, 2, 3, 4, 5, 6, 7, 8}};
        WireNid a = nid(1, NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID, blue_, u"Guid icon");
        a.guidItem = guid;
        send(host, NIM_ADD, a);
        send(host, NIM_ADD, nid(2, NIF_MESSAGE | NIF_ICON, red_));
        QCOMPARE(host.icons().size(), 2);
        // Registration order is kept.
        QCOMPARE(host.icons().at(0).title, QStringLiteral("Guid icon"));
        const QString guidIconId = host.icons().at(0).id;

        // Modify by GUID with a different uID still hits the same icon.
        WireNid m = nid(77, NIF_TIP | NIF_GUID, nullptr, u"Renamed");
        m.guidItem = guid;
        send(host, NIM_MODIFY, m);
        QCOMPARE(host.icons().size(), 2);
        QCOMPARE(host.icons().at(0).title, QStringLiteral("Renamed"));
        QCOMPARE(host.icons().at(0).id, guidIconId);

        WireNid del = nid(0, NIF_GUID);
        del.guidItem = guid;
        QCOMPARE(send(host, NIM_DELETE, del), LRESULT(TRUE));
        QCOMPARE(host.icons().size(), 1);
        QCOMPARE(send(host, NIM_DELETE, nid(2, 0)), LRESULT(TRUE));
        QCOMPARE(host.icons().size(), 0);
        QCOMPARE(send(host, NIM_DELETE, nid(2, 0)), LRESULT(FALSE));
        QVERIFY(!host.trigger(guidIconId, TrayHost::Action::Activate, QPoint()));
    }

    void destroyedOwnerIsPruned() {
        TrayHost host(privateOptions());
        QSignalSpy changed(&host, &TrayHost::iconsChanged);
        QVERIFY(host.start());
        HWND doomed = createOwner();
        QVERIFY(doomed);
        send(host, NIM_ADD, nid(1, NIF_MESSAGE | NIF_ICON, blue_, nullptr, doomed));
        send(host, NIM_ADD, nid(1, NIF_MESSAGE | NIF_ICON, red_));
        QCOMPARE(host.icons().size(), 2);
        QTRY_COMPARE(changed.count(), 1);
        DestroyWindow(doomed);
        QTRY_COMPARE_WITH_TIMEOUT(host.icons().size(), 1, 8000);
        QCOMPARE(host.icons().first().image.pixel(0, 0), qRgba(0xE0, 0x10, 0x10, 0xFF));
        QTRY_COMPARE(changed.count(), 2);
    }

    void burstOfUpdatesIsCoalesced() {
        TrayHost host(privateOptions());
        QSignalSpy changed(&host, &TrayHost::iconsChanged);
        QVERIFY(host.start());
        for (UINT i = 0; i < 40; ++i) send(host, NIM_ADD, nid(100 + i, NIF_MESSAGE | NIF_ICON, blue_));
        QCOMPARE(host.icons().size(), 40);
        QTRY_VERIFY(changed.count() >= 1);
        QTest::qWait(200);
        QVERIFY2(changed.count() <= 3, qPrintable(QString::number(changed.count())));
    }

    void malformedPacketsAreIgnored() {
        TrayHost host(privateOptions());
        QVERIFY(host.start());
        const std::uint32_t tiny = 0x34753423;
        QCOMPARE(sendRaw(host, &tiny, sizeof(tiny)), LRESULT(FALSE));
        QCOMPARE(sendRaw(host, nullptr, 0), LRESULT(FALSE));

        Packet wrongSignature;
        wrongSignature.signature = 0xDEADBEEF;
        wrongSignature.message = NIM_ADD;
        wrongSignature.nid = nid(1, NIF_MESSAGE | NIF_ICON, blue_);
        QCOMPARE(sendRaw(host, &wrongSignature, sizeof(wrongSignature)), LRESULT(FALSE));

        Packet truncated;
        truncated.message = NIM_ADD;
        truncated.nid = nid(1, NIF_MESSAGE | NIF_ICON, blue_);
        QCOMPARE(sendRaw(host, &truncated, 8 + 12), LRESULT(FALSE));

        Packet unknownCommand;
        unknownCommand.message = 99;
        unknownCommand.nid = nid(1, NIF_MESSAGE | NIF_ICON, blue_);
        QCOMPARE(sendRaw(host, &unknownCommand, sizeof(unknownCommand)), LRESULT(FALSE));
        QCOMPARE(host.icons().size(), 0);

        // cbSize larger than the data is bounded by cbData; tooltip cut short
        // without NUL must still be terminated.
        Packet shortTip;
        shortTip.message = NIM_ADD;
        shortTip.nid = nid(2, NIF_MESSAGE | NIF_ICON | NIF_TIP, blue_);
        shortTip.nid.cbSize = 0xFFFFFFFF;
        for (auto &c : shortTip.nid.szTip) c = u'x';
        const DWORD shortSize = DWORD(8 + offsetof(WireNid, szTip) + 10 * sizeof(char16_t));
        QCOMPARE(sendRaw(host, &shortTip, shortSize), LRESULT(TRUE));
        QCOMPARE(host.icons().size(), 1);
        QVERIFY(host.icons().first().title.size() < 10);
        QVERIFY(host.icons().first().title.size() > 0);

        // Small cbSize (old structure) restricts reading even with full data.
        Packet oldStruct;
        oldStruct.message = NIM_ADD;
        oldStruct.nid = nid(3, NIF_MESSAGE | NIF_ICON | NIF_STATE, blue_);
        oldStruct.nid.cbSize = 152; // NOTIFYICONDATAW_V1 size on the wire
        oldStruct.nid.dwState = NIS_HIDDEN;
        oldStruct.nid.dwStateMask = NIS_HIDDEN;
        QCOMPARE(sendRaw(host, &oldStruct, sizeof(oldStruct)), LRESULT(TRUE));
        QCOMPARE(host.icons().size(), 2); // state field is outside V1, so not hidden

        // Garbage icon handle: listed with a null image.
        HICON stale = solidIcon(0xFF00FF00);
        DestroyIcon(stale); // a handle that no longer validates
        send(host, NIM_ADD, nid(4, NIF_MESSAGE | NIF_ICON, stale));
        QCOMPARE(host.icons().size(), 3);
        QVERIFY(host.icons().last().image.isNull());

        // Other WM_COPYDATA kinds are not interpreted.
        Packet appbar;
        appbar.message = NIM_ADD;
        appbar.nid = nid(5, NIF_MESSAGE | NIF_ICON, blue_);
        QCOMPARE(sendRaw(host, &appbar, sizeof(appbar), 0), LRESULT(FALSE));
        QCOMPARE(sendRaw(host, &appbar, sizeof(appbar), 3), LRESULT(FALSE));
        QCOMPARE(host.icons().size(), 3);

        // Unrelated messages are harmless with forwarding off.
        HWND window = reinterpret_cast<HWND>(host.windowHandle());
        QCOMPARE(SendMessageW(window, WM_USER + 100, 1, 2), LRESULT(0));
        SendMessageW(window, WM_CLOSE, 0, 0);
        QVERIFY(IsWindow(window));
        QVERIFY(host.running());
    }

    void lifecycle() {
        {
            TrayHost never(privateOptions());
            never.stop();
            QVERIFY(!never.running());
            QCOMPARE(never.windowHandle(), quintptr(0));
            QCOMPARE(never.icons().size(), 0);
        }
        {
            TrayHost host(privateOptions());
            QVERIFY(host.start());
            QVERIFY(host.start()); // already running: no second thread
            const HWND window = reinterpret_cast<HWND>(host.windowHandle());
            send(host, NIM_ADD, nid(1, NIF_MESSAGE | NIF_ICON, blue_));
            QCOMPARE(host.icons().size(), 1);
            host.stop();
            QVERIFY(!host.running());
            QCOMPARE(host.windowHandle(), quintptr(0));
            QVERIFY(!IsWindow(window));
            QCOMPARE(host.icons().size(), 0);
            host.stop();
            QVERIFY(!host.running());

            QVERIFY(host.start());
            QVERIFY(host.running());
            QCOMPARE(host.icons().size(), 0);
            send(host, NIM_ADD, nid(1, NIF_MESSAGE | NIF_ICON, blue_));
            QCOMPARE(host.icons().size(), 1);
        }
        HWND survivor = nullptr;
        {
            auto *host = new TrayHost(privateOptions());
            QVERIFY(host->start());
            survivor = reinterpret_cast<HWND>(host->windowHandle());
            send(*host, NIM_ADD, nid(1, NIF_MESSAGE | NIF_ICON, blue_));
            delete host; // destruction while running joins the thread
        }
        QVERIFY(!IsWindow(survivor));
        QVERIFY(!FindWindowW(L"IslandDesktopTrayTest", nullptr));
        QCoreApplication::processEvents(); // queued notifications must not touch the deleted host
    }

    void nativeMenuDetection() {
        QVERIFY(!TrayHost::nativeMenuOpen() || FindWindowW(L"#32768", nullptr));
    }
};

QTEST_MAIN(WindowsTrayTest)
#include "windows_tray_test.moc"
