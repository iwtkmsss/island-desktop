// Windows notification-area host. See trayhost.h for the overview.
//
// Protocol notes (publicly documented by alternative-shell projects and
// observable with Spy++): Shell_NotifyIcon() sends WM_COPYDATA with
// dwData == 1 to the top-most top-level window of class "Shell_TrayWnd". The
// payload is a signature, the NIM_* command and a NOTIFYICONDATAW whose handles
// are always marshalled as 32-bit values so 32- and 64-bit callers share one
// layout. dwData == 0 is SHAppBarMessage and dwData == 3 is
// Shell_NotifyIconGetRect; both are only relayed.
#include "trayhost.h"

#include <QMetaObject>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr DWORD kNotifySignature = 0x34753423;
constexpr ULONG_PTR kCopyDataNotifyIcon = 1;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 500;          // top-of-class check period
constexpr int kGeometryTicks = 4;       // geometry refresh every 2 s
constexpr int kPruneTicks = 6;          // dead-owner pruning every 3 s
constexpr ULONGLONG kMinRaiseMs = 1000; // never re-raise more often than this
constexpr ULONGLONG kMaxRaiseMs = 15000;
constexpr ULONGLONG kFightWindowMs = 3000; // losing the top this fast = someone fights us
constexpr UINT kForwardTimeoutMs = 3000;
constexpr int kCoalesceMs = 50;

// NOTIFYICONDATAW as it travels inside WM_COPYDATA. Every handle is 32 bits on
// the wire, so the natural alignment of these fixed-width members yields the
// same layout in every process.
struct WireNotifyIconData {
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
static_assert(offsetof(WireNotifyIconData, szTip) == 24);
static_assert(offsetof(WireNotifyIconData, dwState) == 280);
static_assert(offsetof(WireNotifyIconData, uTimeoutOrVersion) == 800);
static_assert(offsetof(WireNotifyIconData, guidItem) == 936);
static_assert(sizeof(WireNotifyIconData) == 956);

struct WireHeader {
    std::uint32_t signature;
    std::uint32_t message;
};
static_assert(sizeof(WireHeader) == 8);

constexpr std::size_t kMinimumNid = offsetof(WireNotifyIconData, hIcon) + sizeof(std::uint32_t);

template <typename T>
T handleFromWire(std::uint32_t value) {
    // Handles are sign-extended when widened (same rule as HandleToLong/LongToHandle).
    return reinterpret_cast<T>(static_cast<std::intptr_t>(static_cast<std::int32_t>(value)));
}

bool isNullGuid(const GUID &guid) {
    static const GUID zero{};
    return std::memcmp(&guid, &zero, sizeof(GUID)) == 0;
}

QString guidId(const GUID &g) {
    QString s = QStringLiteral("g%1%2%3")
                    .arg(static_cast<quint32>(g.Data1), 8, 16, QLatin1Char('0'))
                    .arg(static_cast<quint32>(g.Data2), 4, 16, QLatin1Char('0'))
                    .arg(static_cast<quint32>(g.Data3), 4, 16, QLatin1Char('0'));
    for (unsigned char byte : g.Data4) s += QStringLiteral("%1").arg(static_cast<quint32>(byte), 2, 16, QLatin1Char('0'));
    return s;
}

QString windowId(HWND owner, UINT uid) {
    return QStringLiteral("h%1-%2")
        .arg(static_cast<quint64>(reinterpret_cast<quintptr>(owner)), 16, 16, QLatin1Char('0'))
        .arg(static_cast<quint32>(uid), 8, 16, QLatin1Char('0'));
}

QImage imageFromIcon(HICON icon) {
    if (!icon) return {};
    // The owner may destroy its icon right after Shell_NotifyIcon returns, so
    // take a private copy first. CopyIcon validates the handle.
    HICON copy = CopyIcon(icon);
    if (!copy) return {};
    QImage image = QImage::fromHICON(copy);
    DestroyIcon(copy);
    if (image.isNull()) return {};
    return image.convertToFormat(QImage::Format_ARGB32);
}

std::wstring className(HWND window) {
    wchar_t buffer[256] = {};
    const int length = GetClassNameW(window, buffer, 256);
    return length > 0 ? std::wstring(buffer, static_cast<std::size_t>(length)) : std::wstring();
}

} // namespace

class TrayHost::Private {
public:
    struct Icon {
        QString id;
        bool hasGuid = false;
        GUID guid{};
        HWND owner = nullptr;
        UINT uid = 0;
        UINT callback = 0;
        UINT version = 0;
        bool hidden = false;
        QString tip;
        QImage image;
        quint64 revision = 0;
    };
    struct Entry {
        TrayIconInfo info;
        HWND owner = nullptr;
        UINT uid = 0;
        UINT callback = 0;
        UINT version = 0;
        bool visible = true;
    };

    Private(TrayHost *owner, const Options &opts)
        : q(owner), options(opts), cls(reinterpret_cast<const wchar_t *>(opts.windowClass.utf16()),
                                       static_cast<std::size_t>(opts.windowClass.size())) {
        coalesce.setSingleShot(true);
        coalesce.setInterval(kCoalesceMs);
        QObject::connect(&coalesce, &QTimer::timeout, q, [this] { emit q->iconsChanged(); });
    }

    TrayHost *q;
    const Options options;
    const std::wstring cls;

    // --- GUI thread only ---------------------------------------------------
    std::thread thread;
    DWORD threadId = 0;
    QTimer coalesce;

    // --- shared --------------------------------------------------------------
    std::atomic<HWND> published{nullptr};
    mutable std::mutex mutex;          // guards snapshot only; never held across SendMessage
    QList<Entry> snapshot;
    std::atomic<bool> notifyPending{false};

    // --- host thread only ----------------------------------------------------
    HWND window = nullptr;
    HWND child = nullptr;
    bool registeredMain = false;
    bool registeredChild = false;
    HWND realTray = nullptr;
    UINT taskbarCreated = 0;
    DWORD ownPid = 0;
    bool broadcasting = false;
    std::vector<Icon> table;
    ULONGLONG lastRaise = 0;
    ULONGLONG raiseInterval = kMinRaiseMs;
    int ticks = 0;
    quint64 revisionCounter = 0; // monotonic across restarts (threads run sequentially)

    // ------------------------------------------------------------------------
    static LRESULT CALLBACK windowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCCREATE) {
            const auto *create = reinterpret_cast<const CREATESTRUCTW *>(l);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto *self = reinterpret_cast<Private *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!self) return DefWindowProcW(h, m, w, l);
        return self->handle(h, m, w, l);
    }

    LRESULT handle(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (taskbarCreated != 0 && m == taskbarCreated) {
            if (!broadcasting) {
                // Explorer (re)started: its new tray window sits above ours.
                realTray = nullptr;
                ensureTop(true);
                syncGeometry();
            }
            return 0;
        }
        switch (m) {
        case WM_COPYDATA:
            return onCopyData(w, l);
        case WM_TIMER:
            if (w == kTimerId) onTimer();
            return 0;
        case WM_WINDOWPOSCHANGED:
            ensureTop(false);
            return DefWindowProcW(h, m, w, l);
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            // Broadcasts reach Explorer by themselves; only refresh our geometry.
            syncGeometry();
            return DefWindowProcW(h, m, w, l);
        case WM_CLOSE:
            // Nobody but this thread may destroy the host window.
            return 0;
        case WM_COMMAND:
            return relay(m, w, l);
        default:
            break;
        }
        if (m >= WM_USER) return relay(m, w, l);
        return DefWindowProcW(h, m, w, l);
    }

    // Explorer's tray window: same class, not owned by this process.
    HWND resolveRealTray() {
        if (realTray) {
            DWORD pid = 0;
            if (IsWindow(realTray) && GetWindowThreadProcessId(realTray, &pid) && pid != ownPid && className(realTray) == cls)
                return realTray;
            realTray = nullptr;
        }
        HWND candidate = nullptr;
        while ((candidate = FindWindowExW(nullptr, candidate, cls.c_str(), nullptr)) != nullptr) {
            DWORD pid = 0;
            GetWindowThreadProcessId(candidate, &pid);
            if (pid != 0 && pid != ownPid) {
                realTray = candidate;
                break;
            }
        }
        return realTray;
    }

    void ensureTop(bool force) {
        if (!window) return;
        if (FindWindowW(cls.c_str(), nullptr) == window) return;
        const ULONGLONG now = GetTickCount64();
        if (!force && lastRaise != 0 && now - lastRaise < raiseInterval) return;
        // If we lose the top right after raising, another window is fighting
        // for it: back off exponentially instead of spinning.
        if (lastRaise != 0 && now - lastRaise < kFightWindowMs)
            raiseInterval = std::min(raiseInterval * 2, kMaxRaiseMs);
        else
            raiseInterval = kMinRaiseMs;
        lastRaise = now;
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    RECT desiredRect() {
        RECT rect{};
        if (HWND real = resolveRealTray(); real && GetWindowRect(real, &rect) && rect.right > rect.left && rect.bottom > rect.top)
            return rect;
        const int width = GetSystemMetrics(SM_CXSCREEN);
        const int height = GetSystemMetrics(SM_CYSCREEN);
        rect = {0, std::max(0, height - 48), std::max(1, width), std::max(1, height)};
        return rect;
    }

    void syncGeometry() {
        if (!window) return;
        const RECT wanted = desiredRect();
        RECT current{};
        GetWindowRect(window, &current);
        if (std::memcmp(&current, &wanted, sizeof(RECT)) == 0) return;
        const int w = wanted.right - wanted.left;
        const int h = wanted.bottom - wanted.top;
        SetWindowPos(window, nullptr, wanted.left, wanted.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        if (child) SetWindowPos(child, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void onTimer() {
        ++ticks;
        ensureTop(false);
        if (ticks % kGeometryTicks == 0) syncGeometry();
        if (ticks % kPruneTicks == 0) prune();
    }

    void prune() {
        const auto before = table.size();
        std::erase_if(table, [](const Icon &icon) { return !icon.owner || !IsWindow(icon.owner); });
        if (table.size() != before) publish(true);
    }

    // ------------------------------------------------------------------------
    LRESULT onCopyData(WPARAM w, LPARAM l) {
        const auto *cds = reinterpret_cast<const COPYDATASTRUCT *>(l);
        if (!cds) return FALSE;
        const bool notify = cds->dwData == kCopyDataNotifyIcon;
        DWORD command = 0xFFFFFFFF;
        // Update our table first; never hold iterators or locks across the forward.
        const BOOL applied = notify ? applyNotify(*cds, &command) : FALSE;

        HWND target = options.forwardToShell ? resolveRealTray() : nullptr;
        if (!target) return notify ? applied : FALSE;
        DWORD_PTR result = 0;
        // The system re-marshals the COPYDATASTRUCT for the target process.
        // This call pumps incoming sent messages, so handle() may re-enter here.
        const LRESULT ok = SendMessageTimeoutW(target, WM_COPYDATA, w, l, SMTO_ABORTIFHUNG | SMTO_NORMAL,
                                               kForwardTimeoutMs, &result);
        if (!ok) return notify ? applied : FALSE;
        // Explorer refuses NIM_ADD for icons it already has (apps re-adding after
        // our TaskbarCreated). The caller must not see that as failure.
        if (notify && command == NIM_ADD && applied) return TRUE;
        return static_cast<LRESULT>(result);
    }

    BOOL applyNotify(const COPYDATASTRUCT &cds, DWORD *commandOut) {
        if (!cds.lpData || cds.cbData < sizeof(WireHeader) + kMinimumNid) return FALSE;
        const auto *bytes = static_cast<const unsigned char *>(cds.lpData);
        WireHeader header{};
        std::memcpy(&header, bytes, sizeof(header));
        if (header.signature != kNotifySignature) return FALSE;
        *commandOut = header.message;

        std::size_t limit = std::min<std::size_t>(cds.cbData - sizeof(WireHeader), sizeof(WireNotifyIconData));
        WireNotifyIconData nid{};
        std::memcpy(&nid, bytes + sizeof(WireHeader), limit);
        // cbSize may describe a shorter (older) structure; it never extends cbData.
        if (nid.cbSize >= kMinimumNid && nid.cbSize < limit) limit = nid.cbSize;
        std::memset(reinterpret_cast<unsigned char *>(&nid) + limit, 0, sizeof(nid) - limit);
        const auto fits = [limit](std::size_t offset, std::size_t size) { return offset + size <= limit; };

        const HWND owner = handleFromWire<HWND>(nid.hWnd);
        const UINT uid = nid.uID;
        const UINT flags = nid.uFlags;
        const bool hasGuid = (flags & NIF_GUID) && fits(offsetof(WireNotifyIconData, guidItem), sizeof(GUID)) &&
                             !isNullGuid(nid.guidItem);

        int index = -1;
        for (int i = 0; i < static_cast<int>(table.size()); ++i) {
            const Icon &icon = table[static_cast<std::size_t>(i)];
            const bool match = hasGuid ? (icon.hasGuid && IsEqualGUID(icon.guid, nid.guidItem))
                                       : (icon.owner == owner && icon.uid == uid);
            if (match) {
                index = i;
                break;
            }
        }

        switch (header.message) {
        case NIM_DELETE:
            if (index < 0) return FALSE;
            table.erase(table.begin() + index);
            publish(true);
            return TRUE;
        case NIM_SETFOCUS:
            return TRUE;
        case NIM_SETVERSION:
            if (index < 0 || !fits(offsetof(WireNotifyIconData, uTimeoutOrVersion), sizeof(std::uint32_t))) return FALSE;
            table[static_cast<std::size_t>(index)].version = nid.uTimeoutOrVersion;
            publish(false); // trigger() needs the version; the visible list is unchanged
            return TRUE;
        case NIM_ADD:
        case NIM_MODIFY:
            break;
        default:
            return FALSE;
        }

        if (index < 0) {
            if (header.message == NIM_MODIFY && !((flags & NIF_ICON) && nid.hIcon) && !(flags & NIF_MESSAGE)) return FALSE;
            if (!owner && !hasGuid) return FALSE;
            Icon icon;
            icon.hasGuid = hasGuid;
            if (hasGuid) icon.guid = nid.guidItem;
            icon.id = hasGuid ? guidId(nid.guidItem) : windowId(owner, uid);
            table.push_back(std::move(icon));
            index = static_cast<int>(table.size()) - 1;
        }
        Icon &icon = table[static_cast<std::size_t>(index)];
        if (owner) icon.owner = owner;
        icon.uid = uid;
        if (flags & NIF_MESSAGE) icon.callback = nid.uCallbackMessage;
        if (flags & NIF_ICON) {
            QImage image = imageFromIcon(handleFromWire<HICON>(nid.hIcon));
            if (image != icon.image) {
                icon.image = std::move(image);
                icon.revision = ++revisionCounter;
            }
        }
        if (flags & NIF_TIP) {
            const std::size_t offset = offsetof(WireNotifyIconData, szTip);
            const std::size_t room = limit > offset ? (limit - offset) / sizeof(char16_t) : 0;
            const std::size_t maxChars = std::min<std::size_t>(room, 128);
            std::size_t length = 0;
            while (length < maxChars && nid.szTip[length] != u'\0') ++length;
            if (length == maxChars && length > 0) --length; // force NUL termination like Explorer
            icon.tip = QString::fromUtf16(nid.szTip, static_cast<qsizetype>(length));
        }
        if ((flags & NIF_STATE) && fits(offsetof(WireNotifyIconData, dwStateMask), sizeof(std::uint32_t))) {
            if (nid.dwStateMask & NIS_HIDDEN) icon.hidden = (nid.dwState & NIS_HIDDEN) != 0;
        }
        publish(true);
        return TRUE;
    }

    // Host thread: publish an immutable copy for the GUI thread. QImage data is
    // never modified after creation, so sharing it across threads is safe.
    void publish(bool notify) {
        QList<Entry> next;
        next.reserve(static_cast<qsizetype>(table.size()));
        for (const Icon &icon : table)
            next.append(Entry{TrayIconInfo{icon.id, icon.tip, icon.image, icon.revision}, icon.owner, icon.uid,
                              icon.callback, icon.version, !icon.hidden});
        {
            std::lock_guard lock(mutex);
            snapshot.swap(next);
        }
        if (notify && !notifyPending.exchange(true)) {
            // Never block on the GUI thread; the event dies with q if q goes first.
            QMetaObject::invokeMethod(
                q,
                [this] {
                    notifyPending = false;
                    if (!coalesce.isActive()) coalesce.start();
                },
                Qt::QueuedConnection);
        }
    }

    LRESULT relay(UINT m, WPARAM w, LPARAM l) {
        HWND target = options.forwardToShell ? resolveRealTray() : nullptr;
        if (!target) return 0;
        const DWORD kind = InSendMessageEx(nullptr);
        if (kind & ISMEX_NOTIFY) {
            SendNotifyMessageW(target, m, w, l);
            return 0;
        }
        if ((kind & (ISMEX_SEND | ISMEX_CALLBACK)) == 0) {
            // Posted to us: post it on.
            PostMessageW(target, m, w, l);
            return 0;
        }
        DWORD_PTR result = 0;
        if (SendMessageTimeoutW(target, m, w, l, SMTO_ABORTIFHUNG | SMTO_NORMAL, kForwardTimeoutMs, &result))
            return static_cast<LRESULT>(result);
        return 0;
    }

    // ------------------------------------------------------------------------
    void unregisterClasses(HINSTANCE instance) {
        if (registeredChild) UnregisterClassW(L"TrayNotifyWnd", instance);
        if (registeredMain) UnregisterClassW(cls.c_str(), instance);
        registeredChild = registeredMain = false;
    }

    void run(std::promise<HWND> &ready) {
        threadId = GetCurrentThreadId();
        ownPid = GetCurrentProcessId();
        table.clear();
        realTray = nullptr;
        lastRaise = 0;
        raiseInterval = kMinRaiseMs;
        ticks = 0;
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        const HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Private::windowProc;
        wc.hInstance = instance;
        wc.lpszClassName = cls.c_str();
        if (RegisterClassExW(&wc)) {
            registeredMain = true;
        } else {
            WNDCLASSEXW existing{};
            existing.cbSize = sizeof(existing);
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS || !GetClassInfoExW(instance, cls.c_str(), &existing) ||
                existing.lpfnWndProc != &Private::windowProc) {
                ready.set_value(nullptr);
                return;
            }
        }
        WNDCLASSEXW childClass{};
        childClass.cbSize = sizeof(childClass);
        childClass.lpfnWndProc = &DefWindowProcW;
        childClass.hInstance = instance;
        childClass.lpszClassName = L"TrayNotifyWnd";
        registeredChild = RegisterClassExW(&childClass) != 0; // "already exists" is fine

        const RECT rect = desiredRect();
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, cls.c_str(), L"", WS_POPUP, rect.left, rect.top,
                                 rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
        if (!window) {
            unregisterClasses(instance);
            ready.set_value(nullptr);
            return;
        }
        child = CreateWindowExW(0, L"TrayNotifyWnd", L"", WS_CHILD, 0, 0, rect.right - rect.left,
                                rect.bottom - rect.top, window, nullptr, instance, nullptr);
        // Let lower-integrity processes reach us if we ever run elevated.
        ChangeWindowMessageFilterEx(window, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
        if (taskbarCreated) ChangeWindowMessageFilterEx(window, taskbarCreated, MSGFLT_ALLOW, nullptr);
        ensureTop(true);
        SetTimer(window, kTimerId, kTimerMs, nullptr);
        published.store(window);
        ready.set_value(window);

        if (options.announce && taskbarCreated) {
            // Running applications re-add their icons; our own window receives
            // its copy synchronously while the flag is set.
            broadcasting = true;
            SendNotifyMessageW(HWND_BROADCAST, taskbarCreated, 0, 0);
            broadcasting = false;
        }

        MSG msg;
        for (;;) {
            const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
            if (got == 0 || got == -1) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        published.store(nullptr);
        KillTimer(window, kTimerId);
        if (child) DestroyWindow(child);
        DestroyWindow(window);
        child = nullptr;
        window = nullptr;
        realTray = nullptr;
        table.clear();
        unregisterClasses(instance);
    }

    void shutdown(bool notify) {
        if (!thread.joinable()) return;
        // The host thread always has a queue once start() succeeded.
        while (!PostThreadMessageW(threadId, WM_QUIT, 0, 0)) {
            if (GetLastError() == ERROR_INVALID_THREAD_ID) break;
            Sleep(5);
        }
        thread.join();
        threadId = 0;
        bool hadIcons = false;
        {
            std::lock_guard lock(mutex);
            hadIcons = !snapshot.isEmpty();
            snapshot.clear();
        }
        if (notify && hadIcons && !coalesce.isActive()) coalesce.start();
    }
};

TrayHost::TrayHost(QObject *parent) : TrayHost(Options{}, parent) {}

TrayHost::TrayHost(const Options &options, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, options)) {}

TrayHost::~TrayHost() {
    d->shutdown(false);
}

bool TrayHost::start() {
    if (d->thread.joinable()) return d->published.load() != nullptr;
    std::promise<HWND> ready;
    std::future<HWND> created = ready.get_future();
    Private *p = d.get();
    d->thread = std::thread([p, promise = std::move(ready)]() mutable { p->run(promise); });
    if (!created.get()) {
        d->thread.join();
        d->threadId = 0;
        return false;
    }
    return true;
}

void TrayHost::stop() {
    d->shutdown(true);
}

bool TrayHost::running() const {
    return d->published.load() != nullptr;
}

quintptr TrayHost::windowHandle() const {
    return reinterpret_cast<quintptr>(d->published.load());
}

QList<TrayIconInfo> TrayHost::icons() const {
    QList<TrayIconInfo> result;
    std::lock_guard lock(d->mutex);
    result.reserve(d->snapshot.size());
    for (const auto &entry : d->snapshot)
        if (entry.visible) result.append(entry.info);
    return result;
}

bool TrayHost::trigger(const QString &id, Action action, const QPoint &cursor) {
    Private::Entry entry;
    bool found = false;
    {
        std::lock_guard lock(d->mutex);
        for (const auto &candidate : d->snapshot) {
            if (candidate.info.id == id) {
                entry.owner = candidate.owner;
                entry.uid = candidate.uid;
                entry.callback = candidate.callback;
                entry.version = candidate.version;
                entry.visible = candidate.visible;
                found = true;
                break;
            }
        }
    }
    if (!found || !entry.callback || !entry.owner || !IsWindow(entry.owner)) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(entry.owner, &pid);
    // We just received the user's click, so we may pass foreground rights on;
    // without them the owner's popup menu cannot take focus or dismiss.
    if (pid) AllowSetForegroundWindow(pid);

    UINT sequence[3] = {};
    int count = 0;
    switch (action) {
    case Action::Activate:
        sequence[count++] = WM_LBUTTONDOWN;
        sequence[count++] = WM_LBUTTONUP;
        if (entry.version >= 3) sequence[count++] = NIN_SELECT;
        break;
    case Action::ContextMenu:
        sequence[count++] = WM_RBUTTONDOWN;
        sequence[count++] = WM_RBUTTONUP;
        if (entry.version >= 3) sequence[count++] = WM_CONTEXTMENU;
        break;
    case Action::SecondaryActivate:
        sequence[count++] = WM_MBUTTONDOWN;
        sequence[count++] = WM_MBUTTONUP;
        break;
    }

    bool delivered = true;
    for (int i = 0; i < count; ++i) {
        WPARAM wParam;
        LPARAM lParam;
        if (entry.version >= 4) {
            wParam = MAKEWPARAM(static_cast<WORD>(static_cast<short>(cursor.x())),
                                static_cast<WORD>(static_cast<short>(cursor.y())));
            lParam = MAKELPARAM(static_cast<WORD>(sequence[i]), static_cast<WORD>(entry.uid));
        } else {
            wParam = static_cast<WPARAM>(entry.uid);
            lParam = static_cast<LPARAM>(sequence[i]);
        }
        // Queue even for an owner on our GUI thread: opening its menu must not
        // re-enter the QML pointer handler that is still delivering this click.
        // Posting also keeps order without waiting for an unresponsive owner.
        if (!PostMessageW(entry.owner, entry.callback, wParam, lParam)) delivered = false;
    }
    return delivered;
}

bool TrayHost::nativeMenuOpen() {
    HWND menu = nullptr;
    while ((menu = FindWindowExW(nullptr, menu, L"#32768", nullptr)) != nullptr)
        if (IsWindowVisible(menu)) return true;
    return false;
}
