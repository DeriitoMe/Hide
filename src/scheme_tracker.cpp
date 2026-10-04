#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "scheme_tracker.hpp"
#include <objbase.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <set>

namespace hide_cursor {
static Value read(HKEY key, const wchar_t *name) {
    Value v;
    DWORD size = 0;
    LONG e = RegQueryValueExW(key, name, nullptr, &v.type, nullptr, &size);
    if (e == ERROR_FILE_NOT_FOUND)
        return v;
    if (e != ERROR_SUCCESS || size > 32768)
        throw std::runtime_error("cursor settings unavailable");
    v.exists = true;
    v.bytes.resize(size);
    DWORD actual = size;
    if (RegQueryValueExW(key, name, nullptr, &v.type, v.bytes.data(), &actual) != ERROR_SUCCESS ||
        actual != size)
        throw std::runtime_error("cursor settings changed during capture");
    return v;
}
static std::wstring path(const Value &v) {
    if (!v.exists || v.bytes.empty())
        return L"";
    if ((v.type != REG_SZ && v.type != REG_EXPAND_SZ) || v.bytes.size() % 2 || v.bytes.size() < 2)
        throw std::runtime_error("invalid cursor path setting");
    std::wstring s(v.bytes.size() / 2, L'\0');
    memcpy(s.data(), v.bytes.data(), v.bytes.size());
    if (s.back() != L'\0' || s.find(L'\0') != s.size() - 1)
        throw std::runtime_error("invalid cursor path string");
    s.pop_back();
    if (s.empty())
        return s;
    wchar_t expanded[32768];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), expanded, _countof(expanded));
    if (!n || n > _countof(expanded))
        throw std::runtime_error("cursor path expansion failed");
    return expanded;
}
Snapshot snapshot() {
    Snapshot s;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS)
        throw std::runtime_error("cursor registry unavailable");
    try {
        for (auto role : roles) {
            Value v = read(key, role);
            s.sources.push_back(path(v));
            s.values.push_back(std::move(v));
        }
        for (auto name : {L"", L"Scheme Source", L"CursorBaseSize"})
            s.values.push_back(read(key, name));
    } catch (...) {
        RegCloseKey(key);
        throw;
    }
    RegCloseKey(key);
    key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", 0, KEY_QUERY_VALUE,
                      &key) == ERROR_SUCCESS) {
        try {
            for (auto name : {L"CursorType", L"CursorSize", L"CursorColor"})
                s.values.push_back(read(key, name));
        } catch (...) {
            RegCloseKey(key);
            throw;
        }
        RegCloseKey(key);
    } else
        for (unsigned i = 0; i < 3; ++i)
            s.values.push_back(Value{});
    for (auto &source : s.sources) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (source.empty()) {
            s.stamps.push_back(0);
            s.stamps.push_back(0);
            continue;
        }
        if (!GetFileAttributesExW(source.c_str(), GetFileExInfoStandard, &a)) {
            s.stamps.push_back(UINT64_MAX);
            s.stamps.push_back(UINT64_MAX);
            continue;
        }
        s.stamps.push_back((uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32) |
                           a.ftLastWriteTime.dwLowDateTime);
        s.stamps.push_back((uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow);
    }
    return s;
}
static SIZE cursor_size(HCURSOR cursor) {
    ICONINFO info{};
    if (!cursor || !GetIconInfo(cursor, &info))
        throw std::runtime_error("cannot inspect displayed cursor");
    BITMAP bitmap{};
    bool ok = GetObjectW(info.hbmColor ? info.hbmColor : info.hbmMask, sizeof(bitmap), &bitmap) != 0;
    SIZE size{bitmap.bmWidth, info.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2};
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    if (!ok || size.cx <= 0 || size.cy <= 0 || size.cx > 256 || size.cy > 256)
        throw std::runtime_error("displayed cursor dimensions unavailable");
    return size;
}
std::vector<uint64_t> cursor_appearance(HCURSOR cursor) {
    SIZE size = cursor_size(cursor);
    ICONINFO info{};
    if (!GetIconInfo(cursor, &info))
        throw std::runtime_error("cannot inspect cursor hotspot");
    std::vector<uint64_t> signature{uint64_t(size.cx), uint64_t(size.cy), info.xHotspot,
                                    info.yHotspot};
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size.cx;
    bi.bmiHeader.biHeight = -size.cy;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!bitmap || !dc || !pixels) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        throw std::runtime_error("cursor appearance buffer unavailable");
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    bool ok = old && old != HGDI_ERROR;
    size_t count = size_t(size.cx) * size.cy * 4;
    for (BYTE background : {BYTE(0), BYTE(255)})
        for (unsigned step = 0; step < 3 && ok; ++step) {
            memset(pixels, background, count);
            ok = DrawIconEx(dc, 0, 0, cursor, size.cx, size.cy, step, nullptr, DI_NORMAL) != FALSE;
            GdiFlush();
            uint64_t hash = 14695981039346656037ull;
            auto bytes = static_cast<const BYTE *>(pixels);
            for (size_t i = 0; i < count && ok; ++i)
                if (i % 4 != 3)
                    hash = (hash ^ bytes[i]) * 1099511628211ull;
            signature.push_back(hash);
        }
    if (old && old != HGDI_ERROR)
        SelectObject(dc, old);
    DeleteDC(dc);
    DeleteObject(bitmap);
    if (!ok)
        throw std::runtime_error("cursor appearance unavailable");
    return signature;
}
bool live_cursors_match(const Prepared &prepared) {
    if (prepared.original_appearances.size() != _countof(roles))
        return false;
    try {
        for (size_t i = 0; i < _countof(roles); ++i)
            if (cursor_appearance(LoadCursorW(nullptr, MAKEINTRESOURCEW(ids[i]))) !=
                prepared.original_appearances[i])
                return false;
        return true;
    } catch (...) {
        return false;
    }
}
bool restore_current_cursors() {
    // Reload stock/empty-path roles, then explicitly restore every configured custom role.
    // In particular, a successful SPI_SETCURSORS call may leave a custom Person role stale.
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (!SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0))
            return false;
        std::vector<HCURSOR> loaded(_countof(roles), nullptr);
        auto cleanup = [&]() {
            for (auto cursor : loaded)
                if (cursor)
                    DestroyCursor(cursor);
        };
        try {
            Snapshot source = snapshot();
            for (size_t i = 0; i < _countof(roles); ++i) {
                if (source.sources[i].empty())
                    continue;
                SIZE size = cursor_size(LoadCursorW(nullptr, MAKEINTRESOURCEW(ids[i])));
                loaded[i] = HCURSOR(LoadImageW(nullptr, source.sources[i].c_str(), IMAGE_CURSOR,
                                              size.cx, size.cy, LR_LOADFROMFILE));
                if (!loaded[i])
                    throw std::runtime_error("current cursor source cannot be restored");
            }
            if (!(snapshot() == source)) {
                cleanup();
                continue;
            }
            bool ok = true;
            for (size_t i = 0; i < loaded.size(); ++i) {
                if (!loaded[i])
                    continue;
                if (!SetSystemCursor(loaded[i], ids[i])) {
                    ok = false;
                    break;
                }
                loaded[i] = nullptr; // Windows consumed the owned handle, preserving full ANI.
            }
            cleanup();
            if (!ok)
                return false;
            if (snapshot() == source)
                return true;
        } catch (...) {
            cleanup();
            return false;
        }
    }
    return false;
}
Prepared::~Prepared() {
    for (auto c : cursors)
        if (c)
            DestroyCursor(c);
    for (auto &f : files)
        DeleteFileW(f.c_str());
    if (!directory.empty())
        RemoveDirectoryW(directory.c_str());
}
SchemeTracker::~SchemeTracker() { stop(); }
bool SchemeTracker::start(HWND window, UINT changed, UINT ready, const std::wstring &cache) {
    window_ = window;
    changed_ = changed;
    ready_ = ready;
    cache_ = cache;
    std::error_code error;
    std::filesystem::create_directories(cache, error);
    if (error)
        return false;
    if (GetFileAttributesW(cache.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
        return false;
    // Prior cache handles cannot survive a process restart; remove only our generated directories.
    for (auto &entry : std::filesystem::directory_iterator(cache, error)) {
        if (entry.is_directory() && entry.path().filename().wstring().rfind(L"g-", 0) == 0 &&
            !(GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) {
            for (unsigned i = 0; i < _countof(roles); ++i)
                for (auto ext : {L".cur", L".ani"})
                    DeleteFileW((entry.path() / (std::to_wstring(i) + ext)).c_str());
            RemoveDirectoryW(entry.path().c_str());
        }
    }
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    request_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stop_ || !request_)
        return false;
    thread_ = CreateThread(nullptr, 0, run, this, 0, nullptr);
    return thread_ != nullptr;
}
uint64_t SchemeTracker::prepare(const Snapshot &source, unsigned transparency) {
    std::lock_guard<std::mutex> lock(mutex_);
    requested_source_ = source;
    requested_transparency_ = transparency;
    uint64_t v = version_.fetch_add(1) + 1;
    SetEvent(request_);
    return v;
}
void SchemeTracker::invalidate() { version_.fetch_add(1); }
std::unique_ptr<Prepared> SchemeTracker::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(result_);
}
void SchemeTracker::stop() {
    if (stop_)
        SetEvent(stop_);
    if (thread_) {
        CancelSynchronousIo(thread_);
        WaitForSingleObject(thread_, INFINITE);
    }
    for (HANDLE h : {thread_, stop_, request_})
        if (h)
            CloseHandle(h);
    thread_ = stop_ = request_ = nullptr;
}
DWORD WINAPI SchemeTracker::run(void *self) {
    static_cast<SchemeTracker *>(self)->loop();
    return 0;
}
void SchemeTracker::loop() {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr),
           access_changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HKEY key = nullptr, access = nullptr;
    auto open_watch = [&](const wchar_t *name, HKEY &k, HANDLE event) {
        return event &&
               RegOpenKeyExW(HKEY_CURRENT_USER, name, 0, KEY_NOTIFY, &k) == ERROR_SUCCESS &&
               RegNotifyChangeKeyValue(k, FALSE,
                                       REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC,
                                       event, TRUE) == ERROR_SUCCESS;
    };
    bool watching = open_watch(L"Control Panel\\Cursors", key, changed);
    bool access_watching =
        open_watch(L"Software\\Microsoft\\Accessibility", access, access_changed);
    std::vector<HANDLE> directories;
    Snapshot watched_source;
    for (;;) {
        std::vector<HANDLE> waits{stop_, request_};
        if (watching)
            waits.push_back(changed);
        if (access_watching)
            waits.push_back(access_changed);
        waits.insert(waits.end(), directories.begin(), directories.end());
        DWORD n = WaitForMultipleObjects(DWORD(waits.size()), waits.data(), FALSE, INFINITE);
        if (n == WAIT_OBJECT_0 || n == WAIT_FAILED)
            break;
        if (n != WAIT_OBJECT_0 + 1) {
            HANDLE signaled = waits.at(n - WAIT_OBJECT_0);
            if (signaled == changed)
                watching = RegNotifyChangeKeyValue(
                               key, FALSE, REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC,
                               changed, TRUE) == ERROR_SUCCESS;
            else if (signaled == access_changed)
                access_watching =
                    RegNotifyChangeKeyValue(access, FALSE,
                                            REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_THREAD_AGNOSTIC,
                                            access_changed, TRUE) == ERROR_SUCCESS;
            else
                FindNextChangeNotification(signaled);
            bool file_change = signaled != changed && signaled != access_changed;
            bool relevant = file_change;
            try {
                relevant = relevant || !(snapshot() == watched_source);
            } catch (...) {
                relevant = true;
            }
            if (relevant) {
                invalidate();
                PostMessageW(window_, changed_, file_change ? 1 : 0, 0);
            }
            continue;
        }
        Snapshot source;
        unsigned opacity;
        uint64_t version;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            source = requested_source_;
            opacity = requested_transparency_;
            version = version_.load();
        }
        watched_source = source;
        for (HANDLE d : directories)
            FindCloseChangeNotification(d);
        directories.clear();
        std::set<std::wstring> parents;
        for (auto &p : source.sources)
            if (!p.empty())
                parents.insert(std::filesystem::path(p).parent_path().wstring());
        for (auto &parent : parents) {
            HANDLE d = FindFirstChangeNotificationW(parent.c_str(), FALSE,
                                                    FILE_NOTIFY_CHANGE_FILE_NAME |
                                                        FILE_NOTIFY_CHANGE_LAST_WRITE |
                                                        FILE_NOTIFY_CHANGE_SIZE);
            if (d != INVALID_HANDLE_VALUE)
                directories.push_back(d);
        }
        auto prepared = std::make_unique<Prepared>();
        prepared->version = version;
        prepared->source = source;
        prepared->transparency = opacity;
        prepared->directory = cache_ + L"\\g-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(version);
        try {
            if (!CreateDirectoryW(prepared->directory.c_str(), nullptr))
                throw std::runtime_error("cannot create cursor cache");
            size_t total = 0;
            for (size_t i = 0; i < _countof(roles); ++i) {
                if (version != version_.load() || WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0)
                    throw std::runtime_error("cursor preparation superseded");
                Bytes bytes = source.sources[i].empty()
                                  ? from_cursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(ids[i])),
                                                opacity == 100)
                                  : read_file(source.sources[i]);
                Bytes converted = transform(bytes, opacity);
                total += converted.size();
                if (total > 64 * 1024 * 1024)
                    throw std::runtime_error("scheme cache exceeds 64 MiB limit");
                bool ani = converted.size() >= 4 && memcmp(converted.data(), "RIFF", 4) == 0;
                auto file =
                    prepared->directory + L"\\" + std::to_wstring(i) + (ani ? L".ani" : L".cur");
                prepared->files.push_back(file);
                write_file(file, converted);
                ICONINFO live{};
                BITMAP bitmap{};
                unsigned width = 0, height = 0;
                if (GetIconInfo(LoadCursorW(nullptr, MAKEINTRESOURCEW(ids[i])), &live)) {
                    if (GetObjectW(live.hbmColor ? live.hbmColor : live.hbmMask, sizeof(bitmap),
                                   &bitmap)) {
                        width = unsigned(bitmap.bmWidth);
                        height = unsigned(live.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2);
                    }
                    if (live.hbmColor)
                        DeleteObject(live.hbmColor);
                    if (live.hbmMask)
                        DeleteObject(live.hbmMask);
                }
                if (width > 256 || height > 256)
                    throw std::runtime_error("displayed cursor exceeds 256-pixel limit");
                HCURSOR original = source.sources[i].empty()
                                       ? LoadCursorW(nullptr, MAKEINTRESOURCEW(ids[i]))
                                       : HCURSOR(LoadImageW(nullptr, source.sources[i].c_str(),
                                                            IMAGE_CURSOR, int(width), int(height),
                                                            LR_LOADFROMFILE));
                if (!original)
                    throw std::runtime_error("current cursor source unavailable");
                try {
                    prepared->original_appearances.push_back(cursor_appearance(original));
                } catch (...) {
                    if (!source.sources[i].empty())
                        DestroyCursor(original);
                    throw;
                }
                if (!source.sources[i].empty())
                    DestroyCursor(original);
                HCURSOR c = HCURSOR(LoadImageW(nullptr, file.c_str(), IMAGE_CURSOR, int(width),
                                               int(height), LR_LOADFROMFILE));
                if (!c)
                    throw std::runtime_error("Windows rejected converted cursor");
                // CopyIcon flattens ANI objects on Windows. Validate here, then reload a fresh
                // owned ANI at apply time.
                prepared->sizes.push_back({LONG(width), LONG(height)});
                if (ani) {
                    DestroyCursor(c);
                    prepared->cursors.push_back(nullptr);
                } else
                    prepared->cursors.push_back(c);
                if (!source.sources[i].empty() &&
                    digest(read_file(source.sources[i])) != digest(bytes))
                    throw std::runtime_error("cursor source changed during preparation");
            }
            if (!(snapshot() == source))
                throw std::runtime_error("cursor scheme changed during preparation");
            if (!live_cursors_match(*prepared))
                throw std::runtime_error("displayed cursor differs from configured skin; reapply your skin in Windows");
        } catch (const std::exception &e) {
            prepared->error = e.what();
        }
        if (version == version_.load()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                result_ = std::move(prepared);
            }
            PostMessageW(window_, ready_, 0, 0);
        }
    }
    for (HANDLE d : directories)
        FindCloseChangeNotification(d);
    if (key)
        RegCloseKey(key);
    if (access)
        RegCloseKey(access);
    if (changed)
        CloseHandle(changed);
    if (access_changed)
        CloseHandle(access_changed);
    if (SUCCEEDED(co))
        CoUninitialize();
}
} // namespace hide_cursor
