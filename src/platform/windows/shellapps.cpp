#include "shellapps.h"
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <appmodel.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propidl.h>
#include <propsys.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace IslandDesktop::Shell {
namespace {
// Local copies avoid depending on INITGUID / uuid.lib for these three values.
constexpr PROPERTYKEY appUserModelIdKey{{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
constexpr GUID appsFolderId{0x1e87508d, 0x89c2, 0x42f0, {0x8a, 0x7e, 0x64, 0x5a, 0x0f, 0x50, 0xca, 0x58}};
constexpr GUID enumItemsHandler{0x94f60519, 0x2850, 0x4924, {0xaa, 0x5a, 0xd1, 0x5e, 0x84, 0x86, 0x80, 0x39}};
constexpr UINT foreignTimeoutMs = 50;

const wchar_t *wide(const QString &text) { return reinterpret_cast<const wchar_t *>(text.utf16()); }
HWND handle(quintptr window) { return reinterpret_cast<HWND>(window); }

QString classOf(HWND window) {
    wchar_t name[256]{};
    const int length = GetClassNameW(window, name, 256);
    return length > 0 ? QString::fromWCharArray(name, length) : QString();
}

bool isCloaked(HWND window) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

bool isShellClass(const QString &name) {
    static const QStringList shell{
        "Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd", "NotifyIconOverflowWindow",
        "Windows.UI.Core.CoreWindow", "TopLevelWindowForOverflowXamlIsland", "XamlExplorerHostIslandWindow",
        "Shell_InputSwitchTopLevelWindow", "SysShadow"};
    return shell.contains(name);
}

BOOL CALLBACK collectWindow(HWND window, LPARAM list) {
    reinterpret_cast<std::vector<HWND> *>(list)->push_back(window);
    return TRUE;
}

QString shellText(IShellItem *item, SIGDN form) {
    PWSTR text = nullptr;
    if (!item || FAILED(item->GetDisplayName(form, &text)) || !text) return {};
    const auto result = QString::fromWCharArray(text);
    CoTaskMemFree(text);
    return result;
}

ComPtr<IShellItem> appsFolderItem(const QString &id) {
    ComPtr<IShellItem> item;
    if (id.isEmpty()) return item;
    const QString path = QStringLiteral("shell:AppsFolder\\") + id;
    if (FAILED(SHCreateItemFromParsingName(wide(path), nullptr, IID_PPV_ARGS(&item)))) item.Reset();
    return item;
}

// 32-bit DIB with premultiplied alpha (what IShellItemImageFactory returns).
QImage imageFromBitmap(HBITMAP bitmap) {
    BITMAP info{};
    if (!bitmap || !GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight == 0) return {};
    const int width = info.bmWidth, height = info.bmHeight < 0 ? -info.bmHeight : info.bmHeight;
    BITMAPINFO header{};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = width;
    header.bmiHeader.biHeight = -height;  // top-down
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    HDC dc = GetDC(nullptr);
    const int lines = GetDIBits(dc, bitmap, 0, UINT(height), image.bits(), &header, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != height) return {};
    bool hasAlpha = false;
    for (int y = 0; y < height && !hasAlpha; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x)
            if (qAlpha(row[x])) { hasAlpha = true; break; }
    }
    if (!hasAlpha) {
        image.reinterpretAsFormat(QImage::Format_RGB32);
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    return image;
}

QImage shellItemImage(IShellItem *item, int size) {
    if (!item) return {};
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item->QueryInterface(IID_PPV_ARGS(&factory)))) return {};
    HBITMAP bitmap = nullptr;
    if (FAILED(factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)) || !bitmap)
        return {};
    auto image = imageFromBitmap(bitmap);
    DeleteObject(bitmap);
    return image;
}

bool execute(SHELLEXECUTEINFOW &info) {
    info.cbSize = sizeof(info);
    info.fMask |= SEE_MASK_FLAG_NO_UI;
    if (!info.nShow) info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) != FALSE;
}

struct CoreSearch {
    DWORD frameProcess = 0;
    HWND core = nullptr;
};

BOOL CALLBACK findCoreWindow(HWND child, LPARAM data) {
    auto *search = reinterpret_cast<CoreSearch *>(data);
    if (classOf(child) != QLatin1String("Windows.UI.Core.CoreWindow")) return TRUE;
    DWORD process = 0;
    GetWindowThreadProcessId(child, &process);
    if (!process || process == search->frameProcess) return TRUE;
    search->core = child;
    return FALSE;
}

QString packageAppId(DWORD process) {
    if (!process) return {};
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
    if (!handle) return {};
    wchar_t id[APPLICATION_USER_MODEL_ID_MAX_LENGTH]{};
    UINT32 length = APPLICATION_USER_MODEL_ID_MAX_LENGTH;
    QString result;
    if (GetApplicationUserModelId(handle, &length, id) == ERROR_SUCCESS && length > 1)
        result = QString::fromWCharArray(id, int(length) - 1);
    CloseHandle(handle);
    return result;
}
}  // namespace

QList<WindowInfo> taskbarWindows() {
    std::vector<HWND> all;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&all));
    QList<WindowInfo> result;
    for (const HWND window : all) {
        // Same eligibility as a taskbar button: visible; WS_EX_APPWINDOW forces
        // a button, otherwise owned, tool and no-activate windows have none.
        if (!IsWindowVisible(window)) continue;
        const auto exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_APPWINDOW)) {
            if (GetWindow(window, GW_OWNER)) continue;
            if (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) continue;
        }
        if (isCloaked(window)) continue;
        const auto className = classOf(window);
        if (className.isEmpty() || isShellClass(className)) continue;
        wchar_t title[512]{};
        // Visible windows only: for other processes this reads the cached text
        // without sending WM_GETTEXT.
        const int length = GetWindowTextW(window, title, 512);
        if (length <= 0) continue;
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (!process) continue;
        WindowInfo info;
        info.handle = reinterpret_cast<quintptr>(window);
        info.processId = process;
        info.title = QString::fromWCharArray(title, length);
        info.className = className;
        info.minimized = IsIconic(window) != FALSE;
        result.append(info);
    }
    return result;
}

QString windowAppId(quintptr window) {
    if (!IsWindow(handle(window))) return {};
    ComPtr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreForWindow(handle(window), IID_PPV_ARGS(&store)))) return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    QString result;
    if (SUCCEEDED(store->GetValue(appUserModelIdKey, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
        result = QString::fromWCharArray(value.pwszVal);
    PropVariantClear(&value);
    return result.trimmed();
}

QString frameAppId(quintptr window) {
    const HWND frame = handle(window);
    if (!IsWindow(frame)) return {};
    CoreSearch search;
    GetWindowThreadProcessId(frame, &search.frameProcess);
    EnumChildWindows(frame, findCoreWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.core) {
        // A suspended or minimized UWP app moves its CoreWindow out of the frame;
        // it stays a top-level window with the same title.
        wchar_t title[512]{};
        if (GetWindowTextW(frame, title, 512) > 0) {
            HWND candidate = nullptr;
            while ((candidate = FindWindowExW(nullptr, candidate, L"Windows.UI.Core.CoreWindow", title))) {
                DWORD process = 0;
                GetWindowThreadProcessId(candidate, &process);
                if (process && process != search.frameProcess) { search.core = candidate; break; }
            }
        }
    }
    if (!search.core) return {};
    DWORD process = 0;
    GetWindowThreadProcessId(search.core, &process);
    return packageAppId(process);
}

QString processAppId(quint32 processId) { return packageAppId(processId); }

QString processImagePath(quint32 processId) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return {};
    std::vector<wchar_t> buffer(32768);
    DWORD length = DWORD(buffer.size());
    QString result;
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &length) && length)
        result = QDir::toNativeSeparators(QString::fromWCharArray(buffer.data(), int(length)));
    CloseHandle(process);
    return result;
}

QString fileDescription(const QString &path) {
    if (path.isEmpty()) return {};
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(wide(path), &ignored);
    if (!size) return {};
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(wide(path), 0, size, data.data())) return {};
    struct Translation { WORD language, codePage; };
    QList<Translation> translations;
    Translation *list = nullptr;
    UINT bytes = 0;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&list), &bytes) && list)
        for (UINT n = 0; n < bytes / sizeof(Translation); ++n) translations.append(list[n]);
    translations.append({0x0409, 0x04b0});
    translations.append({0x0409, 0x04e4});
    for (const auto &t : translations) {
        const auto key = QStringLiteral("\\StringFileInfo\\%1%2\\FileDescription")
            .arg(uint(t.language), 4, 16, QLatin1Char('0')).arg(uint(t.codePage), 4, 16, QLatin1Char('0'));
        wchar_t *text = nullptr;
        UINT length = 0;
        if (VerQueryValueW(data.data(), wide(key), reinterpret_cast<void **>(&text), &length) && text && length > 1) {
            const auto description = QString::fromWCharArray(text, int(length) - 1).trimmed();
            if (!description.isEmpty()) return description;
        }
    }
    return {};
}

QString appsFolderName(const QString &id) {
    const auto item = appsFolderItem(id);
    return item ? shellText(item.Get(), SIGDN_NORMALDISPLAY) : QString();
}

QImage appsFolderImage(const QString &id, int size) {
    const auto item = appsFolderItem(id);
    return item ? shellItemImage(item.Get(), size) : QImage();
}

QImage fileImage(const QString &path, int size) {
    if (path.isEmpty()) return {};
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(wide(path), nullptr, IID_PPV_ARGS(&item)))) return {};
    return shellItemImage(item.Get(), size);
}

QImage windowIcon(quintptr window) {
    const HWND target = handle(window);
    if (!IsWindow(target)) return {};
    HICON icon = nullptr;
    for (const WPARAM type : {WPARAM(ICON_BIG), WPARAM(ICON_SMALL2), WPARAM(ICON_SMALL)}) {
        DWORD_PTR result = 0;
        // SMTO_BLOCK: no re-entrant message dispatch while waiting.
        if (SendMessageTimeoutW(target, WM_GETICON, type, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, foreignTimeoutMs, &result)
            && result) {
            icon = reinterpret_cast<HICON>(result);
            break;
        }
    }
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(target, GCLP_HICON));
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(target, GCLP_HICONSM));
    // The icon belongs to the window; it is copied, not destroyed.
    return icon ? QImage::fromHICON(icon) : QImage();
}

bool launchAppsFolder(const QString &id) {
    const auto item = appsFolderItem(id);
    if (!item) return false;
    PIDLIST_ABSOLUTE list = nullptr;
    if (FAILED(SHGetIDListFromObject(item.Get(), &list)) || !list) return false;
    SHELLEXECUTEINFOW info{};
    info.fMask = SEE_MASK_INVOKEIDLIST;
    info.lpIDList = list;
    const bool ok = execute(info);
    CoTaskMemFree(list);
    return ok;
}

bool launchFile(const QString &path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    const auto directory = QDir::toNativeSeparators(QFileInfo(path).absolutePath());
    const auto file = QDir::toNativeSeparators(path);
    SHELLEXECUTEINFOW info{};
    info.lpFile = wide(file);
    info.lpDirectory = wide(directory);
    return execute(info);
}

bool openUri(const QString &uri) {
    if (uri.isEmpty()) return false;
    SHELLEXECUTEINFOW info{};
    info.lpFile = wide(uri);
    return execute(info);
}

QList<StartApp> startApps() {
    QList<StartApp> result;
    ComPtr<IShellItem> folder;
    if (FAILED(SHGetKnownFolderItem(appsFolderId, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder)))) return result;
    ComPtr<IEnumShellItems> items;
    if (FAILED(folder->BindToHandler(nullptr, enumItemsHandler, IID_PPV_ARGS(&items))) || !items) return result;
    for (;;) {
        ComPtr<IShellItem> item;
        ULONG fetched = 0;
        if (items->Next(1, &item, &fetched) != S_OK || !fetched || !item) break;
        const auto name = shellText(item.Get(), SIGDN_NORMALDISPLAY).trimmed();
        if (name.isEmpty()) continue;
        PIDLIST_ABSOLUTE list = nullptr;
        if (FAILED(SHGetIDListFromObject(item.Get(), &list)) || !list) continue;
        result.append({name, std::shared_ptr<void>(list, [](void *pidl) { CoTaskMemFree(pidl); })});
    }
    return result;
}

QImage itemImage(const std::shared_ptr<void> &item, int size) {
    if (!item) return {};
    ComPtr<IShellItem> shellItem;
    if (FAILED(SHCreateItemFromIDList(static_cast<PCIDLIST_ABSOLUTE>(item.get()), IID_PPV_ARGS(&shellItem)))) return {};
    return shellItemImage(shellItem.Get(), size);
}

bool launchItem(const std::shared_ptr<void> &item) {
    if (!item) return false;
    SHELLEXECUTEINFOW info{};
    info.fMask = SEE_MASK_INVOKEIDLIST;
    info.lpIDList = item.get();
    return execute(info);
}

bool activateWindow(quintptr window) {
    const HWND target = handle(window);
    if (!IsWindow(target)) return false;
    if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE);
    // The click was just delivered to this process, so it may set the foreground.
    if (SetForegroundWindow(target)) return true;
    SwitchToThisWindow(target, TRUE);
    return true;
}

bool minimizeWindow(quintptr window) {
    const HWND target = handle(window);
    if (!IsWindow(target)) return false;
    // Like the taskbar: let the application handle the minimize itself.
    if (PostMessageW(target, WM_SYSCOMMAND, SC_MINIMIZE, 0)) return true;
    return ShowWindowAsync(target, SW_MINIMIZE) != FALSE;
}

quintptr foregroundWindow() { return reinterpret_cast<quintptr>(GetForegroundWindow()); }

bool isOwnWindow(quintptr window) {
    if (!window) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(handle(window), &process);
    return process && process == GetCurrentProcessId();
}

bool isRestoredWindow(quintptr window) {
    return window && IsWindow(handle(window)) && !IsIconic(handle(window));
}

void cursorPosition(int *x, int *y) {
    POINT point{};
    GetCursorPos(&point);
    if (x) *x = point.x;
    if (y) *y = point.y;
}

}  // namespace IslandDesktop::Shell
