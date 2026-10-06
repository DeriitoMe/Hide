#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include "input_state.hpp"
#include "scheme_tracker.hpp"
#include "startup.hpp"
#include "shell_support.hpp"
#include <algorithm>
#include <atomic>
#include <bcrypt.h>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <powrprof.h>
#include <psapi.h>
#include <shellapi.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <windowsx.h>
#include <wtsapi32.h>
using namespace typing_cursor;

constexpr UINT M_TEXT = WM_APP + 1, M_MOUSE = WM_APP + 2, M_FOCUS = WM_APP + 3,
               M_RESOLVE = WM_APP + 4, M_TRAY = WM_APP + 5, M_STATUS = WM_APP + 6,
               M_PAUSE = WM_APP + 7, M_SCHEME = WM_APP + 8, M_PREPARED = WM_APP + 9,
               M_TEST_FADE = WM_APP + 10, M_TEST_RESTORE = WM_APP + 11,
               M_SETTINGS = WM_APP + 12, M_INPUT_FAILED = WM_APP + 13;
constexpr UINT C_ENABLE = 100, C_50 = 150, C_75 = 175, C_90 = 190, C_HIDE = 200, C_IDLE = 210,
               C_FORCE = 220, C_EXCLUDE = 221, C_AUTO = 222, C_STARTUP = 230, C_RESTORE = 240,
               C_RESCAN = 241, C_SETTINGS = 242, C_TRAY = 243, C_SHORTCUT = 244, C_EXIT = 250;
static const wchar_t *CLASS_NAME = L"Hide.Native.v1";
static const wchar_t *ROLES[] = {L"Arrow",    L"Help",     L"AppStarting", L"Wait",    L"Crosshair",
                                 L"IBeam",    L"NWPen",    L"No",          L"SizeNS",  L"SizeWE",
                                 L"SizeNWSE", L"SizeNESW", L"SizeAll",     L"UpArrow", L"Hand",
                                 L"Pin",      L"Person"};
static std::wstring base, legacy_resources, journal_path, settings_path, active_path, vanish_path;
static HWND main_window = nullptr, test_edit = nullptr, settings_window = nullptr;
static HFONT settings_font = nullptr;
static HANDLE stop_event = nullptr, focus_event = nullptr, input_ready = nullptr,
              worker_thread = nullptr, input_thread = nullptr;
static HANDLE guard_process = nullptr, guard_stop = nullptr, shared_mapping = nullptr;
static std::atomic<DWORD> input_thread_id{0};
static std::atomic<uint64_t> generation{1}, sequence{0}, latest_text{0}, latest_text_tick{0},
    latest_mouse{0};
static std::atomic<LONG> resolved_focus{0}, text_pending{0}, mouse_pending{0}, monitor_mouse{0},
    stopping{0};
static std::atomic<uint64_t> keys_seen{0}, text_seen{0}, mouse_seen{0}, focus_queries{0},
    focus_failures{0};
static std::atomic<uint64_t> resolved_generation{0};
static std::atomic<HWND> cached_foreground{nullptr}, requested_foreground{nullptr};
static std::atomic<bool> pending_new_text{false};
static std::atomic<bool> hooks_ready{false};
static std::atomic<LONG> force_pid{0}, exclude_pid{0};
static InputState policy;
static bool applied = false, shadow_was_on = false, shadow_changed = false, tray_added = false,
            supported = false, integration = false, crash_test = false, stress_test = false,
            backend_test = false, backspace_test = false, vanish_checked = false,
            show_tray = false, unattended = false, settings_requested = false,
            shutting_down = false;
static bool supervised_engine = false;
static unsigned engine_flags = 0;
static bool session_query_active = false, enabled_before_session_query = false;
static int final_exit_code = 0;
static uint64_t tray_retry_at = 0, started_tick = 0;
static unsigned tray_retries = 0;
static std::wstring executable;
static hide_startup::Status startup_state;
static uint64_t startup_checked_at = 0;
static int transparency = 75;
static uint64_t fade_count = 0, restore_count = 0;
static double last_fade_ms = 0, max_fade_ms = 0;
static uint64_t last_queue_ms = 0, last_apply_tick = 0;
static HICON tray_icon = nullptr;
struct Shared {
    volatile LONG64 heartbeat;
    volatile LONG dirty, shadow;
    volatile LONG intentional_stop, session_stop, exit_status;
};
static Shared *shared = nullptr;
template <class T> struct Com {
    T *p = nullptr;
    ~Com() {
        if (p)
            p->Release();
    }
    T **out() { return &p; }
    T *operator->() { return p; }
};
struct Record {
    DWORD type = 0;
    bool exists = false;
    std::vector<BYTE> bytes;
};
static hide_cursor::SchemeTracker scheme_tracker;
static std::unique_ptr<hide_cursor::Prepared> prepared_scheme;
static bool preparing_scheme = true;
static uint64_t scheme_changes = 0;
static std::string scheme_error;
static void scheme_changed(bool force = false);

static std::wstring join(const std::wstring &a, const std::wstring &b) { return a + L"\\" + b; }
static bool ensure_directory(const std::wstring &p) {
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
static uint64_t now() { return GetTickCount64(); }
static std::wstring lower(std::wstring s) {
    for (auto &c : s)
        c = (wchar_t)towlower(c);
    return s;
}
static std::string utc_stamp() {
    SYSTEMTIME stamp{};
    GetSystemTime(&stamp);
    char text[40]{};
    sprintf_s(text, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", stamp.wYear, stamp.wMonth,
              stamp.wDay, stamp.wHour, stamp.wMinute, stamp.wSecond, stamp.wMilliseconds);
    return text;
}
static void log_event(const char *s, DWORD code = 0) {
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    auto path = join(base, L"state\\lifecycle.log");
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        (info.nFileSizeHigh || info.nFileSizeLow >= 131072) &&
        !MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING)) {
        ReleaseSRWLockExclusive(&lock);
        return;
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        char record[1024]{};
        int length = _snprintf_s(record, sizeof(record), _TRUNCATE, "%s Hide/1.1.0 pid=%lu %s code=%lu\r\n",
                                utc_stamp().c_str(), GetCurrentProcessId(), s, code);
        DWORD written = 0;
        if (length > 0) WriteFile(file, record, (DWORD)length, &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&lock);
}
static void log_error(const char *s, DWORD code = 0) {
    // Errors can recur on a timer; throttle identical messages without recording input.
    static SRWLOCK lock = SRWLOCK_INIT;
    static std::string last;
    static DWORD last_code = 0;
    static uint64_t last_tick = 0;
    AcquireSRWLockExclusive(&lock);
    bool emit = last != s || last_code != code || now() - last_tick >= 60000;
    if (emit) { last = s; last_code = code; last_tick = now(); }
    ReleaseSRWLockExclusive(&lock);
    if (emit) log_event(s, code);
}
static void log_environment() {
    wchar_t station[256]{}, desktop[256]{};
    DWORD bytes = 0, session = 0;
    GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &bytes);
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), &bytes);
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    char text[768]{};
    sprintf_s(text, "interactive environment session=%lu station=%ls desktop=%ls", session, station, desktop);
    log_event(text);
}
static bool restore_mouse_vanish(DWORD owner = 0) {
    // The lease file is exclusively deletable by one restorer. An older guardian
    // cannot consume a newer process's lease during a fast restart.
    HANDLE file = CreateFileW(vanish_path.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    DWORD record[2]{}, got = 0;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart == sizeof(record) &&
              ReadFile(file, record, sizeof(record), &got, nullptr) && got == sizeof(record) &&
              record[0] == 0x31564d48 && record[1] != 0;
    if (!ok || (owner && owner != record[1])) {
        CloseHandle(file);
        return ok; // Another live generation owns a valid lease.
    }
    BOOL current = FALSE;
    ok = SystemParametersInfoW(SPI_GETMOUSEVANISH, 0, &current, 0) != FALSE;
    if (ok && !current)
        ok = SystemParametersInfoW(SPI_SETMOUSEVANISH, 0, (PVOID)TRUE, SPIF_SENDCHANGE) != FALSE;
    if (ok) {
        FILE_DISPOSITION_INFO remove{TRUE};
        ok = SetFileInformationByHandle(file, FileDispositionInfo, &remove, sizeof(remove)) != FALSE;
    }
    CloseHandle(file);
    return ok;
}
static bool acquire_mouse_vanish() {
    BOOL current = FALSE;
    if (!SystemParametersInfoW(SPI_GETMOUSEVANISH, 0, &current, 0))
        return false;
    if (!current)
        return true; // Nothing was changed, so there is no preference to restore.
    HANDLE file = CreateFileW(vanish_path.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD record[2]{0x31564d48, GetCurrentProcessId()}, wrote = 0;
    bool ok = WriteFile(file, record, sizeof(record), &wrote, nullptr) && wrote == sizeof(record) &&
              FlushFileBuffers(file);
    if (!ok) {
        FILE_DISPOSITION_INFO remove{TRUE};
        SetFileInformationByHandle(file, FileDispositionInfo, &remove, sizeof(remove));
    }
    CloseHandle(file);
    if (!ok)
        return false;
    // Runtime override only: never persist a change to the Windows preference.
    ok = SystemParametersInfoW(SPI_SETMOUSEVANISH, 0, (PVOID)FALSE, SPIF_SENDCHANGE) != FALSE;
    if (!ok)
        restore_mouse_vanish(GetCurrentProcessId());
    return ok;
}
static bool sync_mouse_vanish() {
    bool eligible = policy.enabled && supported && !preparing_scheme && !backend_test;
    if (eligible) {
        if (!vanish_checked) {
            if (!acquire_mouse_vanish())
                return false;
            vanish_checked = true;
        }
    } else if (vanish_checked) {
        if (!restore_mouse_vanish(GetCurrentProcessId()))
            return false;
        vanish_checked = false;
    }
    return true;
}
static Record read_role(HKEY k, const wchar_t *name) {
    Record r;
    DWORD n = 0;
    LONG e = RegQueryValueExW(k, name, nullptr, &r.type, nullptr, &n);
    if (e == ERROR_FILE_NOT_FOUND)
        return r;
    if (e != ERROR_SUCCESS || n > 32768)
        throw std::runtime_error("registry read failed");
    r.exists = true;
    r.bytes.resize(n);
    if (RegQueryValueExW(k, name, nullptr, &r.type, r.bytes.data(), &n) != ERROR_SUCCESS)
        throw std::runtime_error("registry read failed");
    if (n != r.bytes.size())
        throw std::runtime_error("registry changed during read");
    return r;
}
static std::wstring record_string(const Record &r) {
    if (!r.exists || (r.type != REG_SZ && r.type != REG_EXPAND_SZ) || r.bytes.size() % 2 ||
        r.bytes.size() < 2)
        return L"";
    std::wstring result(r.bytes.size() / sizeof(wchar_t), L'\0');
    memcpy(result.data(), r.bytes.data(), r.bytes.size());
    if (result.back() != L'\0')
        return L"";
    result.pop_back();
    return result;
}
static bool owned(const Record &r) {
    auto s = lower(record_string(r));
    auto prefix = lower(legacy_resources + L"\\");
    return s.size() > prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
static LONG write_role(HKEY k, size_t i, const Record &r) {
    return r.exists ? RegSetValueExW(k, ROLES[i], 0, r.type, r.bytes.data(), (DWORD)r.bytes.size())
                    : RegDeleteValueW(k, ROLES[i]);
}
static bool read_journal(std::vector<Record> &rs, bool &shadow) {
    HANDLE f = CreateFileW(journal_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    auto get = [&](void *p, DWORD n) {
        DWORD got = 0;
        return ReadFile(f, p, n, &got, nullptr) && got == n;
    };
    DWORD h[3]{};
    bool ok = size.QuadPart >= 12 && size.QuadPart <= 600000 && get(h, sizeof(h)) &&
              h[0] == 0x54435031 && h[1] == _countof(ROLES) && h[2] <= 1;
    rs.clear();
    if (ok) {
        shadow = h[2] != 0;
        for (DWORD i = 0; i < h[1] && ok; ++i) {
            DWORD e[3]{};
            ok = get(e, sizeof(e)) && e[0] <= 1 && e[2] <= 32768;
            Record r;
            if (ok) {
                r.exists = e[0] != 0;
                r.type = e[1];
                r.bytes.resize(e[2]);
                ok = get(r.bytes.data(), e[2]);
                if (r.exists)
                    ok = ok && (r.type == REG_SZ || r.type == REG_EXPAND_SZ) &&
                         !record_string(r).empty();
            }
            rs.push_back(std::move(r));
        }
    }
    if (ok) {
        BYTE extra;
        DWORD n;
        ok = ReadFile(f, &extra, 1, &n, nullptr) && n == 0;
    }
    CloseHandle(f);
    if (!ok)
        rs.clear();
    return ok;
}
static bool restore_owned(bool restore_shadow) {
    // v2 modifies only live system cursor objects. Current user settings are authoritative.
    HANDLE marker =
        CreateFileW(active_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD magic = 0, got = 0;
    bool legacy_marker = false;
    if (marker != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        legacy_marker = GetFileSizeEx(marker, &size) && size.QuadPart == 0;
        ReadFile(marker, &magic, sizeof(magic), &got, nullptr);
        CloseHandle(marker);
    }
    if (!legacy_marker || (got == sizeof(magic) && magic == 0x32444948))
        return hide_cursor::restore_current_cursors();
    std::vector<Record> rs;
    bool sh = false;
    bool have = read_journal(rs, sh);
    HKEY k = nullptr;
    bool ok = true;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
        try {
            for (size_t i = 0; i < _countof(ROLES); ++i) {
                auto current = read_role(k, ROLES[i]);
                if (owned(current)) {
                    if (!have) {
                        ok = false;
                        continue;
                    }
                    auto e = write_role(k, i, rs[i]);
                    if (e != ERROR_SUCCESS && e != ERROR_FILE_NOT_FOUND)
                        ok = false;
                }
            }
        } catch (...) {
            ok = false;
        }
        RegCloseKey(k);
    } else
        ok = false;
    // Registry paths usually already contain the original scheme. Reloading also
    // restores the live cursor when the process died after a completed transition.
    if (!hide_cursor::restore_current_cursors())
        ok = false;
    if (restore_shadow && have && sh &&
        !SystemParametersInfoW(SPI_SETCURSORSHADOW, 0, (PVOID)TRUE, 0))
        ok = false;
    return ok;
}
static void refresh_pointer() {
    POINT p{};
    if (!GetCursorPos(&p))
        return;
    HWND w = WindowFromPoint(p);
    if (w) {
        DWORD_PTR out = 0;
        SendMessageTimeoutW(w, WM_SETCURSOR, (WPARAM)w, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE),
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 20, &out);
    }
}
static bool fade() {
    if (applied)
        return true;
    if (!supported || preparing_scheme || !guard_process || !prepared_scheme ||
        WaitForSingleObject(guard_process, 0) != WAIT_TIMEOUT)
        return false;
    LARGE_INTEGER a{}, b{}, frequency{};
    QueryPerformanceCounter(&a);
    QueryPerformanceFrequency(&frequency);
    auto matches = [&]() {
        try {
            return prepared_scheme->version == scheme_tracker.version() &&
                   prepared_scheme->source == hide_cursor::snapshot();
        } catch (...) {
            return false;
        }
    };
    if (!matches()) {
        PostMessageW(main_window, M_SCHEME, 0, 0);
        return false;
    }
    if (!hide_cursor::live_cursors_match(*prepared_scheme)) {
        supported = false;
        log_error("displayed cursor differs from configured skin; fading paused");
        PostMessageW(main_window, M_SCHEME, 1, 0);
        return false;
    }
    HANDLE marker = CreateFileW(active_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD magic = 0x32444948, wrote = 0;
    bool journal_ok = marker != INVALID_HANDLE_VALUE &&
                      WriteFile(marker, &magic, sizeof(magic), &wrote, nullptr) &&
                      wrote == sizeof(magic) && FlushFileBuffers(marker);
    if (marker != INVALID_HANDLE_VALUE)
        CloseHandle(marker);
    if (!journal_ok) {
        log_error("cannot record live cursor recovery", GetLastError());
        return false;
    }
    InterlockedExchange(&shared->dirty, 1);
    applied = true;
    bool ok = true;
    for (size_t i = 0; i < _countof(hide_cursor::ids); ++i) {
        if (prepared_scheme->version != scheme_tracker.version()) {
            ok = false;
            break;
        }
        // CopyIcon preserves static cursors but flattens ANI. A fresh file load preserves ANI
        // sequence/rates.
        HCURSOR copy = prepared_scheme->cursors[i]
                           ? (HCURSOR)CopyIcon((HICON)prepared_scheme->cursors[i])
                           : (HCURSOR)LoadImageW(nullptr, prepared_scheme->files[i].c_str(),
                                                 IMAGE_CURSOR, prepared_scheme->sizes[i].cx,
                                                 prepared_scheme->sizes[i].cy, LR_LOADFROMFILE);
        if (!copy) {
            ok = false;
            break;
        }
        if (!SetSystemCursor(copy, hide_cursor::ids[i])) {
            ok = false;
            break;
        }
    }
    if (!ok || !matches()) {
        if (restore_owned(false)) {
            applied = false;
            DeleteFileW(active_path.c_str());
            InterlockedExchange(&shared->dirty, 0);
        }
        supported = false;
        log_error("live cursor application interrupted", GetLastError());
        PostMessageW(main_window, M_SCHEME, 0, 0);
        return false;
    }
    last_apply_tick = now();
    ++fade_count;
    refresh_pointer();
    QueryPerformanceCounter(&b);
    last_fade_ms = (b.QuadPart - a.QuadPart) * 1000.0 / frequency.QuadPart;
    max_fade_ms = std::max(max_fade_ms, last_fade_ms);
    return true;
}
static bool restore() {
    if (!applied && !shadow_changed)
        return true;
    bool ok = restore_owned(shadow_changed);
    if (ok) {
        applied = false;
        shadow_changed = false;
        DeleteFileW(active_path.c_str());
        if (shared) {
            InterlockedExchange(&shared->dirty, 0);
            InterlockedExchange(&shared->shadow, 0);
        }
        ++restore_count;
        refresh_pointer();
    } else
        log_error("restore failed; guardian retains recovery state", GetLastError());
    return ok;
}
static void reconcile() {
    if (!sync_mouse_vanish()) {
        log_error("Windows typing-hide compatibility could not be updated", GetLastError());
        policy.pause(true);
    }
    monitor_mouse.store(policy.faded || policy.pending ? 1 : 0);
    if (policy.faded) {
        if (!fade())
            policy.faded = policy.pending = false;
    } else
        restore();
}
static std::wstring process_name(HWND w) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p)
        return L"";
    wchar_t path[32768];
    DWORD n = _countof(path);
    std::wstring s;
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
        s = path;
        auto pos = s.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
            s = s.substr(pos + 1);
    }
    CloseHandle(p);
    return lower(s);
}
static bool listed(const wchar_t *section, const std::wstring &name) {
    return !name.empty() &&
           GetPrivateProfileIntW(section, name.c_str(), 0, settings_path.c_str()) != 0;
}
static void invalidate_focus() {
    requested_foreground.store(GetForegroundWindow());
    generation.fetch_add(1);
    resolved_focus.store(0);
    SetEvent(focus_event);
    PostMessageW(main_window, M_FOCUS, 0, 0);
}
static void CALLBACK win_event(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
    if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_OBJECT_FOCUS)
        invalidate_focus();
}
static POINT anchor{};
static Modifiers modifiers;
static void refresh_modifiers() {
    modifiers = Modifiers{};
    for (unsigned key : {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN})
        modifiers.update(key, (GetAsyncKeyState(key) & 0x8000) != 0);
}
static LRESULT CALLBACK keyboard_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto &k = *(const KBDLLHOOKSTRUCT *)lp;
        bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN,
             up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
        if (down || up) {
            ++keys_seen;
            if (!modifiers.update(k.vkCode, down) && down) {
                auto action = classify_key(k.vkCode, modifiers.control != 0, modifiers.menu != 0,
                                           modifiers.windows != 0, (modifiers.menu & 2) != 0);
                if (action != KeyAction::Ignore) {
                    ++text_seen;
                    if (action == KeyAction::Text)
                        pending_new_text.store(true);
                    latest_text_tick.store(now());
                    latest_text.store(sequence.fetch_add(1) + 1);
                    GetCursorPos(&anchor);
                    monitor_mouse.store(1);
                    if (!text_pending.exchange(1))
                        PostMessageW(main_window, M_TEXT, 0, 0);
                }
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
static LRESULT CALLBACK mouse_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        ++mouse_seen;
        const auto &m = *(const MSLLHOOKSTRUCT *)lp;
        if (monitor_mouse.load() && (wp != WM_MOUSEMOVE || std::abs(m.pt.x - anchor.x) >= 3 ||
                                     std::abs(m.pt.y - anchor.y) >= 3)) {
            latest_mouse.store(sequence.fetch_add(1) + 1);
            if (!mouse_pending.exchange(1))
                PostMessageW(main_window, M_MOUSE, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
static DWORD WINAPI input_main(void *) {
    input_thread_id.store(GetCurrentThreadId());
    MSG m{};
    PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE);
    refresh_modifiers();
    HHOOK keyboard = nullptr, mouse = nullptr;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, GetModuleHandleW(nullptr), 0);
        mouse = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, GetModuleHandleW(nullptr), 0);
        if (keyboard && mouse) break;
        log_error("input hook initialization failed", GetLastError());
        if (keyboard) UnhookWindowsHookEx(keyboard);
        if (mouse) UnhookWindowsHookEx(mouse);
        keyboard = mouse = nullptr;
        if (WaitForSingleObject(stop_event, 500 * (attempt + 1)) != WAIT_TIMEOUT) break;
    }
    auto foreground = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                      win_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    auto focus = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, win_event, 0, 0,
                                 WINEVENT_OUTOFCONTEXT);
    if (!keyboard || !mouse) {
        PostMessageW(main_window, M_INPUT_FAILED, 0, 0);
    }
    hooks_ready.store(keyboard && mouse);
    log_event(keyboard && mouse ? "input hooks ready" : "input hook retries exhausted");
    SetEvent(input_ready);
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_APP + 100) {
            refresh_modifiers();
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (focus)
        UnhookWinEvent(focus);
    if (foreground)
        UnhookWinEvent(foreground);
    if (keyboard)
        UnhookWindowsHookEx(keyboard);
    if (mouse)
        UnhookWindowsHookEx(mouse);
    hooks_ready.store(false);
    return 0;
}
class FocusHandler : public IUIAutomationFocusChangedEventHandler {
    LONG refs = 1;

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **p) override {
        if (id == __uuidof(IUnknown) || id == __uuidof(IUIAutomationFocusChangedEventHandler)) {
            *p = this;
            AddRef();
            return S_OK;
        }
        *p = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = InterlockedDecrement(&refs);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement *) override {
        if (!stopping.load())
            invalidate_focus();
        return S_OK;
    }
};
static int cache_boolean(IUIAutomationElement *e, PROPERTYID id) {
    VARIANT v{};
    HRESULT h = e->GetCachedPropertyValue(id, &v);
    int b = SUCCEEDED(h) && v.vt == VT_BOOL ? (v.boolVal == VARIANT_TRUE ? 1 : 0) : -1;
    VariantClear(&v);
    return b;
}
static bool cache_bool(IUIAutomationElement *e, PROPERTYID id) { return cache_boolean(e, id) == 1; }
static Focus query_focus(IUIAutomation *a, IUIAutomationCacheRequest *cache, HWND fg) {
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid)
        return Focus::Unknown;
    auto name = process_name(fg);
    if ((DWORD)exclude_pid.load() == pid || listed(L"Excluded", name))
        return Focus::ReadOnly;
    if ((DWORD)force_pid.load() == pid || listed(L"Forced", name))
        return Focus::Editable;
    Com<IUIAutomationElement> e;
    if (!a || FAILED(a->GetFocusedElementBuildCache(cache, e.out())) || !e.p) {
        ++focus_failures;
        return Focus::Unknown;
    }
    int type = 0, element_pid = 0;
    if (integration) {
        e->get_CachedControlType(&type);
        e->get_CachedProcessId(&element_pid);
        GUITHREADINFO gui{sizeof(gui)};
        GetGUIThreadInfo(GetWindowThreadProcessId(main_window, nullptr), &gui);
        FILE *diagnostic = nullptr;
        _wfopen_s(&diagnostic, join(base, L"state\\integration-focus.json").c_str(), L"wb");
        if (diagnostic) {
            fprintf(diagnostic,
                    "{\"foreground_pid\":%lu,\"element_pid\":%d,\"control_type\":%d,\"enabled\":%d,"
                    "\"value_pattern\":%d,\"read_only\":%d,\"own_foreground\":%s,\"edit_native_"
                    "focus\":%s}\n",
                    pid, element_pid, type, cache_boolean(e.p, UIA_IsEnabledPropertyId),
                    cache_boolean(e.p, UIA_IsValuePatternAvailablePropertyId),
                    cache_boolean(e.p, UIA_ValueIsReadOnlyPropertyId),
                    fg == main_window ? "true" : "false",
                    gui.hwndFocus == test_edit ? "true" : "false");
            fclose(diagnostic);
        }
    }
    if (FAILED(e->get_CachedControlType(&type)) || FAILED(e->get_CachedProcessId(&element_pid)) ||
        element_pid != (int)pid || !cache_bool(e.p, UIA_IsEnabledPropertyId))
        return Focus::ReadOnly;
    bool value = cache_bool(e.p, UIA_IsValuePatternAvailablePropertyId);
    if (value)
        return cache_boolean(e.p, UIA_ValueIsReadOnlyPropertyId) != 0
                   ? Focus::ReadOnly
                   : (type == UIA_EditControlTypeId || type == UIA_DocumentControlTypeId
                          ? Focus::Editable
                          : Focus::ReadOnly);
    if (cache_bool(e.p, UIA_IsTextEditPatternAvailablePropertyId))
        return Focus::Editable;
    if (type == UIA_EditControlTypeId || type == UIA_DocumentControlTypeId) {
        Com<IUIAutomationTextPattern> text;
        if (SUCCEEDED(e->GetCurrentPatternAs(UIA_TextPatternId, __uuidof(IUIAutomationTextPattern),
                                             (void **)text.out())) &&
            text.p) {
            Com<IUIAutomationTextRange> range;
            VARIANT v{};
            if (SUCCEEDED(text->get_DocumentRange(range.out())) && range.p &&
                SUCCEEDED(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &v))) {
                bool editable = v.vt == VT_BOOL && v.boolVal == VARIANT_FALSE;
                VariantClear(&v);
                if (editable)
                    return Focus::Editable;
            }
        }
        if (type == UIA_EditControlTypeId && cache_bool(e.p, UIA_IsKeyboardFocusablePropertyId))
            return Focus::Editable;
    }
    // Terminal providers expose output as read-only although their shell accepts input.
    if (name == L"windowsterminal.exe" || name == L"cmd.exe" || name == L"powershell.exe" ||
        name == L"pwsh.exe" || name == L"conhost.exe" || name == L"openconsole.exe")
        return Focus::Editable;
    return Focus::ReadOnly;
}
static DWORD WINAPI focus_main(void *) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IUIAutomation> a;
    Com<IUIAutomation2> a2;
    Com<IUIAutomationCacheRequest> cache;
    FocusHandler *handler = nullptr;
    bool registered = false;
    if (SUCCEEDED(init) &&
        SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IUIAutomation), (void **)a.out()))) {
        a->QueryInterface(__uuidof(IUIAutomation2), (void **)a2.out());
        if (a2.p) {
            a2->put_ConnectionTimeout(200);
            a2->put_TransactionTimeout(400);
        }
        if (SUCCEEDED(a->CreateCacheRequest(cache.out()))) {
            cache->put_TreeScope(TreeScope_Element);
            cache->put_AutomationElementMode(AutomationElementMode_Full);
            const PROPERTYID ids[] = {UIA_ControlTypePropertyId,
                                      UIA_ProcessIdPropertyId,
                                      UIA_IsEnabledPropertyId,
                                      UIA_IsKeyboardFocusablePropertyId,
                                      UIA_IsValuePatternAvailablePropertyId,
                                      UIA_ValueIsReadOnlyPropertyId,
                                      UIA_IsTextEditPatternAvailablePropertyId};
            for (auto id : ids)
                cache->AddProperty(id);
            handler = new FocusHandler;
            registered = SUCCEEDED(a->AddFocusChangedEventHandler(nullptr, handler));
        }
    }
    HANDLE events[] = {stop_event, focus_event};
    SetEvent(focus_event);
    while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        uint64_t g = generation.load();
        HWND fg = GetForegroundWindow();
        requested_foreground.store(fg);
        ++focus_queries;
        Focus f = query_focus(a.p, cache.p, fg);
        if (g == generation.load() && fg == GetForegroundWindow()) {
            cached_foreground.store(fg);
            resolved_focus.store((LONG)f);
            resolved_generation.store(g);
            PostMessageW(main_window, M_RESOLVE, 0, 0);
        } else
            SetEvent(focus_event);
    }
    if (registered)
        a->RemoveFocusChangedEventHandler(handler);
    if (handler)
        handler->Release();
    if (cache.p) {
        cache.p->Release();
        cache.p = nullptr;
    }
    if (a2.p) {
        a2.p->Release();
        a2.p = nullptr;
    }
    if (a.p) {
        a.p->Release();
        a.p = nullptr;
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return 0;
}
static void save_settings() {
    WritePrivateProfileStringW(L"Settings", L"Transparency", std::to_wstring(transparency).c_str(),
                               settings_path.c_str());
    WritePrivateProfileStringW(L"Settings", L"IdleRestore", policy.idle_ms ? L"1" : L"0",
                               settings_path.c_str());
    WritePrivateProfileStringW(L"Settings", L"ShowTray", show_tray ? L"1" : L"0",
                               settings_path.c_str());
    if (!integration && !backend_test)
        WritePrivateProfileStringW(L"Settings", L"Enabled", policy.enabled ? L"1" : L"0",
                                   settings_path.c_str());
}
static bool startup_enabled() {
    if (!startup_checked_at || now() - startup_checked_at >= 5000) {
        startup_state = hide_startup::inspect(executable);
        startup_checked_at = now();
    }
    return startup_state.enabled && startup_state.matches;
}
static bool set_startup(bool enable) {
    HRESULT error = S_OK;
    bool ok = hide_startup::configure(executable, enable, error);
    startup_checked_at = 0;
    log_event(ok ? (enable ? "startup enabled" : "startup disabled") : "startup update failed",
              (DWORD)error);
    if (ok && enable && FAILED(error)) log_event("startup uses Run fallback", (DWORD)error);
    return ok;
}
static bool create_shortcut() {
    std::wstring path;
    HRESULT error = S_OK;
    bool ok = hide_shell::create_settings_shortcut(executable, path, error);
    log_event(ok ? "settings desktop shortcut created" : "desktop shortcut creation failed", (DWORD)error);
    return ok;
}
static void refresh_settings() {
    if (!settings_window) return;
    CheckDlgButton(settings_window, C_ENABLE, policy.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(settings_window, C_IDLE, policy.idle_ms ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(settings_window, C_STARTUP, startup_enabled() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(settings_window, C_TRAY, show_tray ? BST_CHECKED : BST_UNCHECKED);
    int selected = transparency == 50 ? 0 : transparency == 75 ? 1 : transparency == 90 ? 2 : 3;
    SendDlgItemMessageW(settings_window, 301, CB_SETCURSEL, selected, 0);
    bool guard_alive = guard_process && WaitForSingleObject(guard_process, 0) == WAIT_TIMEOUT;
    std::wstring text = !hooks_ready.load() ? L"输入监听尚未就绪"
                       : !guard_alive ? L"恢复保护未运行"
                       : !policy.enabled ? L"已暂停，指针保持正常显示"
                       : preparing_scheme ? L"正在准备当前鼠标皮肤"
                       : !supported ? (scheme_error.find("displayed cursor differs") != std::string::npos
                                       ? L"外观与配置不一致，请在 Windows 中重新应用当前皮肤"
                                       : L"当前皮肤淡化受限，请重新识别")
                                    : L"正在运行 · 输入与 Backspace 删除时淡化";
    text += L"\r\n";
    text += startup_state.task ? (startup_state.enabled ? L"登录启动：计划任务已启用"
                                                       : L"登录启动：计划任务已禁用")
                              : startup_state.enabled ? L"登录启动：传统启动项"
                                                      : L"登录启动：未开启";
    if (startup_state.registered && !startup_state.matches)
        text += L"（程序路径已变化，请重新开启）";
    SetDlgItemTextW(settings_window, 302, text.c_str());
}
static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_COMMAND: {
        int command = LOWORD(wp);
        if (command == 301 && HIWORD(wp) == CBN_SELCHANGE) {
            LRESULT choice = SendDlgItemMessageW(window, 301, CB_GETCURSEL, 0, 0);
            const UINT options[] = {C_50, C_75, C_90, C_HIDE};
            if (choice >= 0 && choice < 4) SendMessageW(main_window, WM_COMMAND, options[choice], 0);
        } else if (command == C_STARTUP) {
            bool enable = IsDlgButtonChecked(window, C_STARTUP) == BST_CHECKED;
            if (!set_startup(enable))
                MessageBoxW(window, L"启动设置未能完成，原启动渠道已尽量保留。请检查系统启动设置。",
                            L"Hide", MB_OK | MB_ICONERROR);
        } else if (command == C_SHORTCUT) {
            if (create_shortcut())
                MessageBoxW(window, L"桌面入口已创建：Hide 设置。也可按 Ctrl+Alt+H 打开设置。",
                            L"Hide", MB_OK | MB_ICONINFORMATION);
            else
                MessageBoxW(window, L"无法创建桌面快捷方式。仍可双击 Hide.exe 或设置.cmd 打开设置。",
                            L"Hide", MB_OK | MB_ICONERROR);
        } else if (command == C_ENABLE || command == C_IDLE || command == C_TRAY ||
                   command == C_RESCAN || command == C_RESTORE || command == C_EXIT)
            SendMessageW(main_window, WM_COMMAND, command, 0);
        if (settings_window) refresh_settings();
        return 0;
    }
    case WM_TIMER:
        refresh_settings();
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        settings_window = nullptr;
        if (settings_font) { DeleteObject(settings_font); settings_font = nullptr; }
        return 0; // Closing settings never stops the engine.
    }
    return DefWindowProcW(window, message, wp, lp);
}
static void open_settings() {
    if (integration || backend_test || shutting_down) return;
    if (settings_window) {
        ShowWindow(settings_window, SW_RESTORE);
        SetForegroundWindow(settings_window);
        return;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = settings_proc;
    wc.lpszClassName = L"Hide.Settings.v1";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = tray_icon;
    RegisterClassW(&wc);
    settings_window = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Hide 设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        560, 510, nullptr, nullptr, instance, nullptr);
    if (!settings_window) { log_error("settings window creation failed", GetLastError()); return; }
    settings_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
    auto control = [&](const wchar_t *kind, const wchar_t *label, DWORD style, int id,
                       int x, int y, int width, int height) {
        HWND child = CreateWindowExW(0, kind, label, WS_CHILD | WS_VISIBLE | style,
            x, y, width, height, settings_window, (HMENU)(INT_PTR)id, instance, nullptr);
        SendMessageW(child, WM_SETFONT, (WPARAM)settings_font, TRUE);
        return child;
    };
    control(L"STATIC", L"关闭窗口不会退出 Hide。\r\n取消“显示托盘图标”可静默运行，自启仍然有效。", 0, 300, 24, 18, 500, 48);
    control(L"BUTTON", L"启用输入淡化", BS_AUTOCHECKBOX | WS_TABSTOP, C_ENABLE, 24, 79, 240, 26);
    control(L"STATIC", L"输入时透明度", 0, 303, 24, 117, 160, 24);
    HWND alpha = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                         301, 180, 112, 320, 160);
    for (const wchar_t *item : {L"50% 透明", L"75% 透明", L"90% 透明", L"完全隐藏"})
        SendMessageW(alpha, CB_ADDSTRING, 0, (LPARAM)item);
    control(L"BUTTON", L"停止输入 1.5 秒后恢复", BS_AUTOCHECKBOX | WS_TABSTOP,
            C_IDLE, 24, 151, 500, 26);
    control(L"BUTTON", L"随 Windows 登录启动（推荐保持开启）", BS_AUTOCHECKBOX | WS_TABSTOP,
            C_STARTUP, 24, 183, 500, 26);
    control(L"BUTTON", L"显示托盘图标（取消后进入静默模式）", BS_AUTOCHECKBOX | WS_TABSTOP, C_TRAY, 24, 215, 500, 26);
    control(L"STATIC", L"静默后：桌面快捷方式或 Ctrl+Alt+H 可再打开设置。", 0, 304, 24, 252, 500, 24);
    control(L"STATIC", L"", 0, 302, 24, 288, 500, 52);
    control(L"BUTTON", L"创建桌面设置快捷方式", BS_PUSHBUTTON | WS_TABSTOP, C_SHORTCUT, 24, 350, 236, 32);
    control(L"BUTTON", L"重新识别皮肤", BS_PUSHBUTTON | WS_TABSTOP, C_RESCAN, 24, 408, 145, 32);
    control(L"BUTTON", L"恢复并暂停", BS_PUSHBUTTON | WS_TABSTOP, C_RESTORE, 187, 408, 145, 32);
    control(L"BUTTON", L"退出 Hide", BS_PUSHBUTTON | WS_TABSTOP, C_EXIT, 350, 408, 145, 32);
    refresh_settings();
    SetTimer(settings_window, 1, 1000, nullptr);
    // Consume a hidden launcher STARTUPINFO before displaying the first settings window.
    ShowWindow(settings_window, SW_SHOW);
    ShowWindow(settings_window, SW_SHOWNORMAL);
    SetForegroundWindow(settings_window);
}
static void balloon(const wchar_t *title, const wchar_t *message) {
    if (!show_tray || !tray_added) {
        log_event("notice available in settings");
        return;
    }
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = main_window;
    n.uID = 1;
    n.uFlags = NIF_INFO;
    wcscpy_s(n.szInfoTitle, title);
    wcsncpy_s(n.szInfo, message, _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}
static void update_tray() {
    if (!main_window) return;
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = main_window;
    n.uID = 1;
    if (!show_tray) {
        if (tray_added) Shell_NotifyIconW(NIM_DELETE, &n);
        tray_added = false;
        return;
    }
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = M_TRAY;
    n.hIcon = tray_icon ? tray_icon : LoadIconW(nullptr, IDI_APPLICATION);
    auto tip = !policy.enabled    ? std::wstring(L"Hide · 已暂停")
               : preparing_scheme ? std::wstring(L"Hide · 正在准备当前皮肤")
               : !supported       ? std::wstring(L"Hide · 当前皮肤淡化受限")
                                  : L"Hide · 输入时" + std::to_wstring(transparency) + L"%透明";
    wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
    if (Shell_NotifyIconW(tray_added ? NIM_MODIFY : NIM_ADD, &n)) {
        tray_added = true;
        tray_retries = 0;
    } else {
        tray_added = false;
        tray_retry_at = now() + 1000;
        if (tray_retries < 10) ++tray_retries;
        log_error("tray icon registration failed");
    }
}
static void menu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (policy.enabled ? MF_CHECKED : 0), C_ENABLE, L"启用输入淡化");
    HMENU alpha = CreatePopupMenu();
    AppendMenuW(alpha, MF_STRING | (transparency == 50 ? MF_CHECKED : 0), C_50, L"50% 透明");
    AppendMenuW(alpha, MF_STRING | (transparency == 75 ? MF_CHECKED : 0), C_75, L"75% 透明");
    AppendMenuW(alpha, MF_STRING | (transparency == 90 ? MF_CHECKED : 0), C_90, L"90% 透明");
    AppendMenuW(alpha, MF_STRING | (transparency == 100 ? MF_CHECKED : 0), C_HIDE, L"完全隐藏");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)alpha, L"输入时的透明度");
    AppendMenuW(m, MF_STRING | (policy.idle_ms ? MF_CHECKED : 0), C_IDLE, L"停止输入 1.5 秒后恢复");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, C_AUTO, L"当前应用：自动识别");
    AppendMenuW(m, MF_STRING, C_FORCE, L"当前应用：使用键盘检测");
    AppendMenuW(m, MF_STRING, C_EXCLUDE, L"当前应用：暂停淡化");
    AppendMenuW(m, MF_STRING | (startup_enabled() ? MF_CHECKED : 0), C_STARTUP, L"随 Windows 启动");
    AppendMenuW(m, MF_STRING, C_SETTINGS, L"打开 Hide 设置");
    AppendMenuW(m, MF_STRING, C_TRAY, L"隐藏托盘图标（双击程序可设置）");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | MF_CHECKED | MF_GRAYED, 0, L"自动跟随当前鼠标皮肤");
    AppendMenuW(m, MF_STRING, C_RESCAN, L"重新识别当前皮肤");
    AppendMenuW(m, MF_STRING, C_RESTORE, L"立即恢复并暂停  Ctrl+Alt+F12");
    AppendMenuW(m, MF_STRING, C_EXIT, L"退出并恢复皮肤");
    POINT p{};
    GetCursorPos(&p);
    HWND app = GetForegroundWindow();
    SetForegroundWindow(main_window);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, main_window, nullptr);
    DestroyMenu(m);
    PostMessageW(main_window, WM_NULL, 0, 0);
    if (cmd == C_FORCE || cmd == C_EXCLUDE || cmd == C_AUTO) {
        auto name = process_name(app);
        DWORD pid = 0;
        GetWindowThreadProcessId(app, &pid);
        if (!name.empty()) {
            if (cmd == C_AUTO) {
                WritePrivateProfileStringW(L"Forced", name.c_str(), nullptr, settings_path.c_str());
                WritePrivateProfileStringW(L"Excluded", name.c_str(), nullptr,
                                           settings_path.c_str());
                force_pid.store(0);
                exclude_pid.store(0);
                invalidate_focus();
                return;
            }
            bool force = cmd == C_FORCE;
            WritePrivateProfileStringW(force ? L"Forced" : L"Excluded", name.c_str(), L"1",
                                       settings_path.c_str());
            WritePrivateProfileStringW(force ? L"Excluded" : L"Forced", name.c_str(), nullptr,
                                       settings_path.c_str());
            if (force) {
                force_pid.store(pid);
                exclude_pid.store(0);
            } else {
                exclude_pid.store(pid);
                force_pid.store(0);
            }
            invalidate_focus();
        }
        return;
    }
    if (cmd)
        SendMessageW(main_window, WM_COMMAND, cmd, 0);
}
static void status(const wchar_t *filename = L"status.json") {
    bool startup = startup_enabled();
    CURSORINFO cursor{sizeof(cursor)};
    bool cursor_known = GetCursorInfo(&cursor) != FALSE;
    BOOL vanish = FALSE;
    bool vanish_known = SystemParametersInfoW(SPI_GETMOUSEVANISH, 0, &vanish, 0) != FALSE;
    PROCESS_MEMORY_COUNTERS_EX m{};
    m.cb = sizeof(m);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&m, sizeof(m));
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    PROCESS_MEMORY_COUNTERS_EX gm{};
    gm.cb = sizeof(gm);
    if (guard_process)
        GetProcessMemoryInfo(guard_process, (PROCESS_MEMORY_COUNTERS *)&gm, sizeof(gm));
    FILE *f = nullptr;
    _wfopen_s(&f, join(base, L"state\\" + std::wstring(filename)).c_str(), L"wb");
    if (!f)
        return;
    fprintf(f,
            "{\"pid\":%lu,\"enabled\":%s,\"supported\":%s,\"faded\":%s,\"transparency\":%d,"
            "\"keyboard_events\":%llu,\"text_events\":%llu,\"mouse_events\":%llu,\"focus_queries\":"
            "%llu,\"focus_failures\":%llu,\"focus_state\":%d,\"fade_count\":%llu,\"restore_count\":"
            "%llu,\"last_fade_ms\":%.3f,\"max_fade_ms\":%.3f,\"working_set_bytes\":%llu,\"private_"
            "bytes\":%llu,\"guardian_working_set_bytes\":%llu,\"guardian_private_bytes\":%llu,"
            "\"handles\":%lu,\"gdi_handles\":%lu,\"user_handles\":%lu,\"preparing\":%s,"
            "\"scheme_version\":%llu,\"scheme_changes\":%llu,\"queue_ms\":%llu,\"startup\":%s,"
            "\"cursor_showing\":%s,\"windows_typing_hide\":%s,\"typing_hide_lease\":%s,"
            "\"running\":%s,\"sample_utc\":\"%s\",\"uptime_ms\":%llu,\"hooks_ready\":%s,"
            "\"guardian_alive\":%s,\"show_tray\":%s,\"tray_added\":%s,\"settings_open\":%s,"
            "\"guardian_pid\":%lu,\"supervised\":%s,"
            "\"startup_backend\":\"%s\",\"startup_registered\":%s,\"startup_path_matches\":%s,"
            "\"startup_last_result\":%ld,\"exit_code\":%d}\n",
            GetCurrentProcessId(), policy.enabled ? "true" : "false", supported ? "true" : "false",
            applied ? "true" : "false", transparency, keys_seen.load(), text_seen.load(),
            mouse_seen.load(), focus_queries.load(), focus_failures.load(), (int)policy.focus,
            fade_count, restore_count, last_fade_ms, max_fade_ms,
            (unsigned long long)m.WorkingSetSize, (unsigned long long)m.PrivateUsage,
            (unsigned long long)gm.WorkingSetSize, (unsigned long long)gm.PrivateUsage, handles,
            GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
            GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS),
            preparing_scheme ? "true" : "false", scheme_tracker.version(), scheme_changes,
            last_queue_ms, startup ? "true" : "false",
            cursor_known ? (cursor.flags & CURSOR_SHOWING ? "true" : "false") : "null",
            vanish_known ? (vanish ? "true" : "false") : "null",
            GetFileAttributesW(vanish_path.c_str()) != INVALID_FILE_ATTRIBUTES ? "true" : "false",
            shutting_down ? "false" : "true", utc_stamp().c_str(), now() - started_tick,
            hooks_ready.load() ? "true" : "false",
            guard_process && WaitForSingleObject(guard_process, 0) == WAIT_TIMEOUT ? "true" : "false",
            show_tray ? "true" : "false", tray_added ? "true" : "false",
            settings_window ? "true" : "false", guard_process ? GetProcessId(guard_process) : 0,
            supervised_engine ? "true" : "false", startup_state.task ? "task" : startup ? "run" : "none",
            startup_state.registered ? "true" : "false", startup_state.matches ? "true" : "false",
            startup_state.last_result, final_exit_code);
    fclose(f);
}
static bool spawn_guard() {
    if (supervised_engine) return shared && guard_stop && guard_process &&
        WaitForSingleObject(guard_process, 0) == WAIT_TIMEOUT;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    guard_stop = CreateEventW(&sa, TRUE, FALSE, nullptr);
    shared_mapping =
        CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(Shared), nullptr);
    if (!guard_stop || !shared_mapping)
        return false;
    shared = (Shared *)MapViewOfFile(shared_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
    if (!shared)
        return false;
    ZeroMemory(shared, sizeof(Shared));
    InterlockedExchange64(&shared->heartbeat, now());
    HANDLE parent = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &parent,
                         SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                         TRUE, 0))
        return false;
    auto exe = join(base, L"Hide.exe");
    auto cmd = L"\"" + exe + L"\" --guard " + std::to_wstring((uintptr_t)parent) + L" " +
               std::to_wstring((uintptr_t)guard_stop) + L" " +
               std::to_wstring((uintptr_t)shared_mapping);
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<BYTE> list(bytes);
    auto attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)list.data();
    bool initialized = InitializeProcThreadAttributeList(attrs, 1, 0, &bytes) != FALSE;
    HANDLE handles[] = {parent, guard_stop, shared_mapping};
    bool ok = initialized &&
              UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                        sizeof(handles), nullptr, nullptr) != FALSE;
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi{};
    if (ok)
        ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, base.c_str(),
                            &si.StartupInfo, &pi) != FALSE;
    if (initialized)
        DeleteProcThreadAttributeList(attrs);
    CloseHandle(parent);
    if (ok) {
        guard_process = pi.hProcess;
        CloseHandle(pi.hThread);
        SetHandleInformation(guard_stop, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(shared_mapping, HANDLE_FLAG_INHERIT, 0);
    }
    return ok;
}
static int guardian(HANDLE parent, HANDLE done, HANDLE mapping, DWORD *observed_exit = nullptr,
                    bool *intentional = nullptr, bool *session_stop = nullptr) {
    DWORD parent_pid = GetProcessId(parent);
    if (!parent_pid)
        return 7;
    Shared *s = (Shared *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(Shared));
    if (!s)
        return 7;
    HANDLE events[] = {parent, done};
    uint64_t last_poll = now(), resume_grace = now() + 30000;
    log_event("guardian started", parent_pid);
    for (;;) {
        DWORD w = WaitForMultipleObjects(2, events, FALSE, 1000);
        if (w == WAIT_OBJECT_0 || w == WAIT_OBJECT_0 + 1 || w == WAIT_FAILED)
            break;
        uint64_t tick = now();
        if (tick - last_poll > 5000)
            resume_grace = tick + 15000;
        last_poll = tick;
        auto heartbeat = std::max((uint64_t)s->heartbeat, resume_grace);
        if (tick > heartbeat && tick - heartbeat > 12000) {
            log_event("guardian heartbeat timeout", parent_pid);
            TerminateProcess(parent, 8);
            WaitForSingleObject(parent, 2000);
            break;
        }
    }
    DWORD parent_exit = STILL_ACTIVE;
    if (WaitForSingleObject(parent, 0) == WAIT_TIMEOUT && WaitForSingleObject(done, 0) == WAIT_OBJECT_0)
        WaitForSingleObject(parent, 3000);
    GetExitCodeProcess(parent, &parent_exit);
    if (observed_exit) *observed_exit = parent_exit == STILL_ACTIVE ? DWORD(s->exit_status) : parent_exit;
    if (intentional) *intentional = s->intentional_stop != 0;
    if (session_stop) *session_stop = s->session_stop != 0;
    log_event("guardian parent stop observed", parent_exit);
    bool dirty = s->dirty != 0, shadow = s->shadow != 0;
    bool ok = true;
    if (dirty || shadow) {
        for (int i = 0; i < 3; ++i) {
            ok = restore_owned(shadow);
            if (ok) {
                DeleteFileW(active_path.c_str());
                break;
            }
            Sleep(100);
        }
    }
    bool vanish_ok = false;
    for (int i = 0; i < 3; ++i) {
        vanish_ok = restore_mouse_vanish(parent_pid);
        if (vanish_ok)
            break;
        Sleep(100);
    }
    ok = ok && vanish_ok;
    log_event(ok ? "guardian recovery complete" : "guardian recovery failed");
    UnmapViewOfFile(s);
    CloseHandle(parent);
    CloseHandle(done);
    CloseHandle(mapping);
    return ok ? 0 : 9;
}
static bool launch_supervisor(unsigned flags) {
    auto command = L"\"" + executable + L"\" --supervise " + std::to_wstring(flags);
    STARTUPINFOW info{sizeof(info)};
    info.dwFlags = STARTF_USESHOWWINDOW;
    info.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    bool ok = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, base.c_str(), &info, &process) != FALSE;
    if (ok) { CloseHandle(process.hThread); CloseHandle(process.hProcess); }
    return ok;
}
static int supervise(unsigned flags) {
    ensure_directory(join(base, L"state"));
    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\Hide.Supervisor.v1");
    if (!singleton) return 5;
    if (GetLastError() == ERROR_ALREADY_EXISTS || FindWindowW(CLASS_NAME, nullptr)) {
        CloseHandle(singleton);
        return 0;
    }
    log_event("supervisor started");
    if (!hide_startup::health_check(executable, true)) log_error("health trigger resume failed");
    unsigned attempts = 0;
    int result = 0;
    for (;;) {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE stopped = CreateEventW(&security, TRUE, FALSE, nullptr);
        HANDLE memory = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                                           sizeof(Shared), nullptr);
        Shared *state = memory ? (Shared *)MapViewOfFile(memory, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)) : nullptr;
        HANDLE supervisor = nullptr;
        bool ok = state && stopped && DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(),
            GetCurrentProcess(), &supervisor, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0);
        if (state) { ZeroMemory(state, sizeof(Shared)); state->heartbeat = now(); }
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<BYTE> storage(bytes);
        auto attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();
        bool initialized = InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) != FALSE;
        HANDLE inherited[] = {supervisor, stopped, memory};
        ok = ok && initialized && UpdateProcThreadAttribute(attributes, 0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr) != FALSE;
        STARTUPINFOEXW info{};
        info.StartupInfo.cb = sizeof(info);
        info.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        info.StartupInfo.wShowWindow = SW_HIDE;
        info.lpAttributeList = attributes;
        auto command = L"\"" + executable + L"\" --engine " + std::to_wstring((uintptr_t)supervisor) +
            L" " + std::to_wstring((uintptr_t)stopped) + L" " + std::to_wstring((uintptr_t)memory) +
            L" " + std::to_wstring(flags);
        PROCESS_INFORMATION engine{};
        uint64_t launched = now();
        if (ok) ok = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, base.c_str(), &info.StartupInfo, &engine) != FALSE;
        if (initialized) DeleteProcThreadAttributeList(attributes);
        if (supervisor) CloseHandle(supervisor);
        DWORD exit_code = 5;
        bool intentional = false, session_end = false;
        if (ok) {
            CloseHandle(engine.hThread);
            // guardian consumes duplicates, leaving this loop's handles reusable for cleanup.
            HANDLE child = nullptr, stop_copy = nullptr, memory_copy = nullptr;
            HANDLE self = GetCurrentProcess();
            bool copies = DuplicateHandle(self, engine.hProcess, self, &child, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
                DuplicateHandle(self, stopped, self, &stop_copy, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
                DuplicateHandle(self, memory, self, &memory_copy, 0, FALSE, DUPLICATE_SAME_ACCESS);
            if (copies) result = guardian(child, stop_copy, memory_copy, &exit_code, &intentional, &session_end);
            else {
                if (child) CloseHandle(child);
                if (stop_copy) CloseHandle(stop_copy);
                if (memory_copy) CloseHandle(memory_copy);
                TerminateProcess(engine.hProcess, 5);
                result = 5;
            }
            WaitForSingleObject(engine.hProcess, 3000);
            CloseHandle(engine.hProcess);
        } else { log_error("supervisor engine launch failed", GetLastError()); result = 0; }
        if (state) UnmapViewOfFile(state);
        if (memory) CloseHandle(memory);
        if (stopped) CloseHandle(stopped);
        if (intentional || (ok && exit_code == 0)) {
            if (!session_end && !hide_startup::health_check(executable, false))
                log_error("health trigger pause after normal exit failed");
            result = 0;
            break;
        }
        if (result != 0) {
            hide_startup::health_check(executable, false);
            break; // A recovery failure must retain evidence, not relaunch blindly.
        }
        auto startup = hide_startup::inspect(executable);
        if (!startup.enabled || !startup.matches) { result = (int)exit_code; break; }
        if (now() - launched >= 300000) attempts = 0;
        if (++attempts > 3) {
            hide_startup::health_check(executable, false);
            log_error("supervisor restart budget exhausted", exit_code);
            result = (int)exit_code;
            break;
        }
        log_event("supervisor restarting failed engine", exit_code);
        flags &= ~1u; // Recovery stays quiet even if the first launch opened settings.
        Sleep(1000);
    }
    log_event("supervisor stopped", (DWORD)result);
    CloseHandle(singleton);
    return result;
}
static void scheme_changed(bool force) {
    if (!force && !preparing_scheme && supported && prepared_scheme &&
        prepared_scheme->transparency == unsigned(transparency)) {
        try {
            if (prepared_scheme->source == hide_cursor::snapshot()) {
                prepared_scheme->version = scheme_tracker.version();
                return;
            }
        } catch (...) {
        }
    }
    scheme_tracker.invalidate();
    supported = false;
    preparing_scheme = true;
    policy.faded = policy.pending = false;
    policy.mouse(sequence.fetch_add(1) + 1);
    latest_mouse.store(policy.mouse_sequence);
    pending_new_text.store(false);
    if (!restore()) {
        scheme_error = "cursor recovery failed";
        SetTimer(main_window, 3, 500, nullptr);
        return;
    }
    prepared_scheme.reset();
    ++scheme_changes;
    KillTimer(main_window, 3);
    SetTimer(main_window, 3, 100, nullptr);
    update_tray();
}
static void prepare_scheme() {
    KillTimer(main_window, 3);
    if (applied && !restore()) {
        SetTimer(main_window, 3, 500, nullptr);
        return;
    }
    try {
        auto source = hide_cursor::snapshot();
        if (!(hide_cursor::snapshot() == source)) {
            SetTimer(main_window, 3, 100, nullptr);
            return;
        }
        scheme_tracker.prepare(source, unsigned(transparency));
    } catch (const std::exception &e) {
        preparing_scheme = false;
        supported = false;
        scheme_error = e.what();
        log_error("current cursor scheme unavailable");
        update_tray();
    }
}
static void scheme_ready() {
    auto result = scheme_tracker.take();
    if (!result || result->version != scheme_tracker.version())
        return;
    try {
        if (!(result->source == hide_cursor::snapshot())) {
            scheme_changed();
            return;
        }
    } catch (...) {
        scheme_changed();
        return;
    }
    preparing_scheme = false;
    supported = result->valid();
    scheme_error = result->error;
    prepared_scheme = std::move(result);
    policy.faded = policy.pending = false;
    reconcile();
    update_tray();
    log_event(supported ? "cursor scheme ready" : "cursor scheme unsupported");
    if (!supported) {
        log_error(scheme_error.c_str());
        balloon(L"当前皮肤的淡化暂不可用",
                L"已保留当前鼠标外观。请在 Windows 鼠标属性中重新应用你想使用的皮肤，再重新识别。");
    }
}
static int cursor_alpha_max() {
    CURSORINFO ci{sizeof(ci)};
    if (!GetCursorInfo(&ci) || !ci.hCursor)
        return -1;
    ICONINFO ii{};
    if (!GetIconInfo(ci.hCursor, &ii))
        return -1;
    BITMAP bm{};
    GetObjectW(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);
    int w = bm.bmWidth, h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);
    if (ii.hbmMask)
        DeleteObject(ii.hbmMask);
    if (w <= 0 || h <= 0 || w > 256 || h > 256)
        return -1;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    std::vector<BYTE> images[2];
    for (int b = 0; b < 2; ++b) {
        void *p = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
        HDC dc = CreateCompatibleDC(nullptr);
        if (!bitmap || !dc) {
            if (bitmap)
                DeleteObject(bitmap);
            if (dc)
                DeleteDC(dc);
            return -1;
        }
        auto old = SelectObject(dc, bitmap);
        memset(p, b ? 255 : 0, w * h * 4);
        DrawIconEx(dc, 0, 0, ci.hCursor, w, h, 0, nullptr, DI_NORMAL);
        GdiFlush();
        images[b].assign((BYTE *)p, (BYTE *)p + w * h * 4);
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(bitmap);
    }
    int max = 0;
    for (int i = 0; i < w * h * 4; i += 4)
        max = std::max(max, 255 - ((int)images[1][i] - (int)images[0][i]));
    return max;
}
static int test_phase = 0, test_alpha = -1, test_cycles = 0, deletion_repeats = 0,
           deletion_initial_length = 0;
static uint64_t test_start = 0, test_deadline = 0;
static POINT saved_pointer{};
static double test_latency = 0;
static void finish_test(bool ok, const char *reason) {
    restore();
    status(L"integration-status.json");
    FILE *f = nullptr;
    _wfopen_s(&f, join(base, L"state\\integration-result.json").c_str(), L"wb");
    if (f) {
        fprintf(f,
                "{\"passed\":%s,\"reason\":\"%s\",\"synthetic_input_to_fade_ms\":%.3f,\"faded_"
                "cursor_alpha_max\":%d,\"original_cursor_alpha_max\":%d,\"fade_count\":%llu,"
                "\"restore_count\":%llu}\n",
                ok ? "true" : "false", reason, test_latency, test_alpha, cursor_alpha_max(),
                fade_count, restore_count);
        fclose(f);
    }
    SetCursorPos(saved_pointer.x, saved_pointer.y);
    PostMessageW(main_window, WM_CLOSE, 0, 0);
}
static void integration_tick() {
    // Recovery is tested independently of foreground permission and input providers.
    // This mode switches the system cursor directly and kills only its own process.
    if (preparing_scheme) {
        if (now() > test_deadline)
            finish_test(false, "scheme preparation timed out");
        return;
    }
    if (!supported) {
        finish_test(false, "current scheme is unsupported");
        return;
    }
    if (crash_test && test_phase == 0) {
        test_start = now();
        if (!fade()) {
            finish_test(false, "guardian test could not activate fading");
            return;
        }
        test_alpha = cursor_alpha_max();
        if (test_alpha < 0 ||
            std::abs(test_alpha - (int)((255 * (100 - transparency) + 50) / 100)) > 8) {
            finish_test(false, "guardian test system opacity did not change");
            return;
        }
        status(L"crash-before.json");
        TerminateProcess(GetCurrentProcess(), 99);
        return;
    }
    if (test_phase == 0) {
        if (policy.focus != Focus::Editable) {
            if (now() > test_deadline)
                finish_test(false, "editable focus not recognized");
            return;
        }
        if (GetForegroundWindow() != main_window || GetFocus() != test_edit) {
            finish_test(false, "controlled edit not foreground; input not injected");
            return;
        }
        if ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) |
             GetAsyncKeyState(VK_SHIFT)) &
            0x8000)
            return;
        INPUT k[2]{};
        k[0].type = k[1].type = INPUT_KEYBOARD;
        k[0].ki.wVk = k[1].ki.wVk = backspace_test ? VK_BACK : 'A';
        k[1].ki.dwFlags = KEYEVENTF_KEYUP;
        test_start = now();
        if (SendInput(2, k, sizeof(INPUT)) != 2) {
            finish_test(false, "SendInput failed");
            return;
        }
        test_phase = 1;
        test_deadline = now() + 3000;
    } else if (test_phase == 1) {
        if (!applied) {
            if (now() > test_deadline)
                finish_test(false, "first character did not fade");
            return;
        }
        test_latency = (double)(last_apply_tick - test_start);
        test_alpha = cursor_alpha_max();
        CURSORINFO cursor{sizeof(cursor)};
        if (transparency < 100 && (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING))) {
            finish_test(false, "cursor object faded but actual pointer is hidden");
            return;
        }
        if (test_alpha < 0 ||
            std::abs(test_alpha - (int)((255 * (100 - transparency) + 50) / 100)) > 8) {
            finish_test(false, "system cursor opacity did not change");
            return;
        }
        if (backspace_test && deletion_repeats < 8) {
            if (GetForegroundWindow() != main_window || GetFocus() != test_edit) {
                finish_test(false, "deletion test edit lost foreground; input not injected");
                return;
            }
            if ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU) |
                 GetAsyncKeyState(VK_SHIFT)) & 0x8000) {
                finish_test(false, "deletion test interrupted by modifier; input not injected");
                return;
            }
            INPUT keys[2]{};
            keys[0].type = keys[1].type = INPUT_KEYBOARD;
            keys[0].ki.wVk = keys[1].ki.wVk = VK_BACK;
            keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
            if (SendInput(2, keys, sizeof(INPUT)) != 2) {
                finish_test(false, "repeated deletion injection failed");
                return;
            }
            ++deletion_repeats;
            return;
        }
        if (backspace_test && GetWindowTextLengthW(test_edit) != deletion_initial_length - 9) {
            finish_test(false, "Backspace did not delete the controlled fixture");
            return;
        }
        INPUT mouse{};
        mouse.type = INPUT_MOUSE;
        mouse.mi.dx = (test_cycles % 2) ? -8 : 8;
        mouse.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &mouse, sizeof(mouse));
        test_phase = 2;
        test_deadline = now() + 3000;
    } else if (test_phase == 2) {
        if (applied) {
            if (now() > test_deadline)
                finish_test(false, "mouse did not restore");
            return;
        }
        if (cursor_alpha_max() <= 240) {
            finish_test(false, "restored cursor not opaque");
            return;
        }
        ++test_cycles;
        if (stress_test && test_cycles < 30) {
            if (test_cycles == 5)
                status(L"stress-warmup.json");
            test_phase = 0;
            test_deadline = now() + 5000;
        } else
            finish_test(true, stress_test ? "30 live fade and restore cycles"
                               : backspace_test ? "standalone Backspace, repeated deletion, visible fade and mouse restoration"
                                                : "controlled first-character and mouse restoration");
    }
}
static LRESULT CALLBACK window_proc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp) {
    static UINT taskbar = RegisterWindowMessageW(L"TaskbarCreated");
    if (m == taskbar) {
        tray_added = false;
        tray_retries = 0;
        update_tray();
        return 0;
    }
    switch (m) {
    case M_SETTINGS:
        open_settings();
        return 1;
    case M_INPUT_FAILED:
        final_exit_code = 10;
        log_event("input initialization exhausted; stopping for bounded startup retry");
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case M_TEST_FADE:
        if (!backend_test)
            return 0;
        policy.faded = true;
        reconcile();
        return applied ? 1 : 0;
    case M_TEST_RESTORE:
        if (!backend_test)
            return 0;
        policy.faded = policy.pending = false;
        return restore() ? 1 : 0;
    case M_SCHEME:
        scheme_changed(wp != 0);
        return 0;
    case M_PREPARED:
        scheme_ready();
        return 0;
    case WM_SETTINGCHANGE:
        if (wp == SPI_SETCURSORS)
            scheme_changed(true);
        else if (wp == 0)
            scheme_changed();
        return 0;
    case M_TEXT: {
        text_pending.store(0);
        auto s = latest_text.load();
        last_queue_ms = now() - latest_text_tick.load();
        policy.mouse(latest_mouse.load());
        if (requested_foreground.load() != GetForegroundWindow()) {
            invalidate_focus();
            policy.invalidate(generation.load());
        }
        policy.text(latest_text_tick.load(), s, !pending_new_text.exchange(false));
        reconcile();
        if (policy.pending)
            SetEvent(focus_event);
        return 0;
    }
    case M_MOUSE:
        mouse_pending.store(0);
        policy.mouse(latest_mouse.load());
        reconcile();
        return 0;
    case M_FOCUS:
        if (policy.generation != generation.load())
            policy.invalidate(generation.load());
        reconcile();
        return 0;
    case M_RESOLVE:
        policy.resolve((Focus)resolved_focus.load(), resolved_generation.load(), now());
        reconcile();
        return 0;
    case M_PAUSE:
        policy.pause(true);
        reconcile();
        save_settings();
        update_tray();
        return 0;
    case M_STATUS:
        status();
        return 0;
    case M_TRAY:
        if (lp == (LPARAM)-1) {
            show_tray = wp != 0;
            tray_retries = 0;
            update_tray();
        } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)
            menu();
        else if (lp == WM_LBUTTONDBLCLK)
            SendMessageW(hwnd, WM_COMMAND, C_ENABLE, 0);
        return 0;
    case WM_HOTKEY:
        SendMessageW(hwnd, WM_COMMAND, C_RESTORE, 0);
        return 0;
    case WM_COMMAND: {
        int cmd = LOWORD(wp);
        if (cmd < 100 || cmd > 250)
            return DefWindowProcW(hwnd, m, wp, lp);
        if (cmd == C_EXIT) {
            log_event("explicit user exit");
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (cmd == C_SETTINGS) {
            open_settings();
            return 0;
        } else if (cmd == C_TRAY) {
            if (show_tray && !create_shortcut())
                MessageBoxW(settings_window ? settings_window : main_window,
                            L"桌面入口未能创建。静默后仍可双击 Hide.exe 或设置.cmd 打开设置。",
                            L"Hide", MB_OK | MB_ICONINFORMATION);
            show_tray = !show_tray;
            tray_retries = 0;
        } else if (cmd == C_ENABLE) {
            policy.pause(policy.enabled);
        } else if (cmd == C_RESCAN)
            scheme_changed(true);
        else if (cmd == C_RESTORE)
            policy.pause(true);
        else if (cmd == C_IDLE)
            policy.idle_ms = policy.idle_ms ? 0 : 1500;
        else if (cmd == C_STARTUP) {
            if (!set_startup(!startup_enabled()))
                balloon(L"开机启动设置失败", L"无法保存当前用户的启动项，请稍后重试。");
        }
        else if (cmd == C_50 || cmd == C_75 || cmd == C_90 || cmd == C_HIDE) {
            policy.faded = policy.pending = false;
            restore();
            transparency = cmd == C_HIDE ? 100 : cmd - 100;
            scheme_changed();
        }
        save_settings();
        reconcile();
        update_tray();
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            if (shared)
                InterlockedExchange64(&shared->heartbeat, now());
            if (guard_process && WaitForSingleObject(guard_process, 0) != WAIT_TIMEOUT) {
                policy.pause(true);
                supported = false;
                log_error("recovery guardian unavailable");
                final_exit_code = 11;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            if (hooks_ready.load() && input_thread && WaitForSingleObject(input_thread, 0) != WAIT_TIMEOUT) {
                final_exit_code = 10;
                log_error("input thread stopped unexpectedly");
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            if (show_tray && !tray_added && tray_retries < 10 && now() >= tray_retry_at)
                update_tray();
            policy.tick(now());
            reconcile();
            if (requested_foreground.load() != GetForegroundWindow())
                invalidate_focus();
        } else if (wp == 2)
            integration_tick();
        else if (wp == 3)
            prepare_scheme();
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK || wp == WTS_CONSOLE_DISCONNECT || wp == WTS_REMOTE_DISCONNECT) {
            policy.invalidate(generation.fetch_add(1) + 1);
            reconcile();
        } else {
            scheme_changed();
            PostThreadMessageW(input_thread_id.load(), WM_APP + 100, 0, 0);
            invalidate_focus();
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMSUSPEND) {
            policy.invalidate(generation.fetch_add(1) + 1);
            reconcile();
        } else if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            scheme_changed();
            PostThreadMessageW(input_thread_id.load(), WM_APP + 100, 0, 0);
            if (shared)
                InterlockedExchange64(&shared->heartbeat, now());
            invalidate_focus();
        }
        return TRUE;
    case WM_QUERYENDSESSION:
        log_event("Windows session ending");
        if (!session_query_active) enabled_before_session_query = policy.enabled;
        session_query_active = true;
        if (shared) { shared->intentional_stop = 1; shared->session_stop = 1; }
        policy.pause(true);
        reconcile();
        return TRUE;
    case WM_ENDSESSION:
        if (wp) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (session_query_active) {
            session_query_active = false;
            if (shared) { shared->intentional_stop = 0; shared->session_stop = 0; }
            policy.pause(!enabled_before_session_query);
            reconcile();
            log_event("Windows session end cancelled; previous enable state restored");
        }
        return 0;
    case WM_CLOSE:
        if (shared) {
            shared->intentional_stop = final_exit_code == 0 ? 1 : 0;
            shared->session_stop = session_query_active ? 1 : 0;
        }
        shutting_down = true;
        policy.pause(true);
        reconcile();
        if (settings_window) DestroyWindow(settings_window);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, m, wp, lp);
}
static LONG WINAPI exception_filter(EXCEPTION_POINTERS *) {
    try {
        log_event("unhandled exception; restoring cursor preferences");
        if (GetFileAttributesW(active_path.c_str()) != INVALID_FILE_ATTRIBUTES)
            restore_owned(true);
        restore_mouse_vanish(GetCurrentProcessId());
    } catch (...) {
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    wchar_t module[32768];
    GetModuleFileNameW(nullptr, module, _countof(module));
    executable = module;
    started_tick = now();
    base = module;
    base.resize(base.find_last_of(L"\\/"));
    legacy_resources = join(base, L"resources");
    journal_path = join(base, L"state\\recovery.bin");
    settings_path = join(base, L"state\\settings.ini");
    active_path = join(base, L"state\\active.lock");
    vanish_path = join(base, L"state\\mouse-vanish.lock");
    int argc = 0;
    LPWSTR *args = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring mode = argc > 1 ? args[1] : L"";
    unattended = mode == L"--autostart" || mode == L"--quiet";
    settings_requested = mode.empty() || mode == L"--settings";
    if (mode == L"--guard" && argc == 5) {
        auto p = (HANDLE)(uintptr_t)_wcstoui64(args[2], nullptr, 10),
             e = (HANDLE)(uintptr_t)_wcstoui64(args[3], nullptr, 10),
             map = (HANDLE)(uintptr_t)_wcstoui64(args[4], nullptr, 10);
        LocalFree(args);
        return guardian(p, e, map);
    }
    if (mode == L"--supervise") {
        unsigned flags = argc > 2 ? unsigned(_wtoi(args[2])) : 0;
        LocalFree(args);
        return supervise(flags & 3u);
    }
    if (mode == L"--engine" && argc == 6) {
        guard_process = (HANDLE)(uintptr_t)_wcstoui64(args[2], nullptr, 10);
        guard_stop = (HANDLE)(uintptr_t)_wcstoui64(args[3], nullptr, 10);
        shared_mapping = (HANDLE)(uintptr_t)_wcstoui64(args[4], nullptr, 10);
        engine_flags = unsigned(_wtoi(args[5])) & 3u;
        shared = (Shared *)MapViewOfFile(shared_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
        supervised_engine = true;
        settings_requested = (engine_flags & 1u) != 0;
        unattended = !settings_requested;
        if (!shared || !GetProcessId(guard_process)) { LocalFree(args); return 7; }
    }
    int test_transparency = argc > 2 ? _wtoi(args[2]) : 75;
    LocalFree(args);
    if (mode == L"--enable-startup" || mode == L"--disable-startup")
    {
        ensure_directory(join(base, L"state"));
        return set_startup(mode == L"--enable-startup") ? 0 : 1;
    }
    if (mode == L"--create-shortcut") {
        ensure_directory(join(base, L"state"));
        return create_shortcut() ? 0 : 1;
    }
    if (mode == L"--hide-tray" || mode == L"--show-tray") {
        ensure_directory(join(base, L"state"));
        bool visible = mode == L"--show-tray";
        if (!WritePrivateProfileStringW(L"Settings", L"ShowTray", visible ? L"1" : L"0", settings_path.c_str()))
            return 1;
        auto w = FindWindowW(CLASS_NAME, nullptr);
        if (w) {
            DWORD_PTR result = 0;
            SendMessageTimeoutW(w, M_TRAY, visible ? 1 : 0, -1, SMTO_ABORTIFHUNG, 2000, &result);
        }
        return 0;
    }
    if (mode == L"--recover") {
        bool recovering_active = GetFileAttributesW(active_path.c_str()) != INVALID_FILE_ATTRIBUTES;
        auto w = FindWindowW(CLASS_NAME, nullptr);
        DWORD_PTR result = 0;
        if (w)
            SendMessageTimeoutW(w, M_PAUSE, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
        bool ok = restore_owned(recovering_active);
        ok = restore_mouse_vanish() && ok;
        if (ok)
            DeleteFileW(active_path.c_str());
        return ok ? 0 : 1;
    }
    if (mode == L"--exit" || mode == L"--pause") {
        auto w = FindWindowW(CLASS_NAME, nullptr);
        DWORD_PTR result = 0;
        return w && SendMessageTimeoutW(w, mode == L"--exit" ? WM_CLOSE : M_PAUSE, 0, 0,
                                        SMTO_ABORTIFHUNG, 2000, &result)
                   ? 0
                   : 2;
    }
    if (mode == L"--status") {
        auto w = FindWindowW(CLASS_NAME, nullptr);
        DWORD_PTR r = 0;
        if (w && SendMessageTimeoutW(w, M_STATUS, 0, 0, SMTO_ABORTIFHUNG, 2000, &r)) return 0;
        if (!w) {
            ensure_directory(join(base, L"state"));
            FILE *file = nullptr;
            _wfopen_s(&file, join(base, L"state\\status.json").c_str(), L"wb");
            if (file) {
                fprintf(file, "{\"running\":false,\"sample_utc\":\"%s\"}\n", utc_stamp().c_str());
                fclose(file);
            }
        }
        return 2;
    }
    if (mode.empty() || mode == L"--settings" || mode == L"--quiet" || mode == L"--autostart") {
        HWND window = FindWindowW(CLASS_NAME, nullptr);
        if (window) {
            if (settings_requested) {
                DWORD pid = 0; GetWindowThreadProcessId(window, &pid); AllowSetForegroundWindow(pid);
                DWORD_PTR result = 0;
                SendMessageTimeoutW(window, M_SETTINGS, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
            }
            return 0;
        }
        return launch_supervisor((settings_requested ? 1u : 0u) | (mode == L"--quiet" ? 2u : 0u)) ? 0 : 5;
    }
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\Hide.Native.v1");
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (settings_requested) {
            HWND window = FindWindowW(CLASS_NAME, nullptr);
            if (window) {
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                AllowSetForegroundWindow(pid);
                DWORD_PTR result = 0;
                SendMessageTimeoutW(window, M_SETTINGS, 0, 0, SMTO_ABORTIFHUNG, 2000, &result);
            }
        }
        CloseHandle(mutex);
        return 0;
    }
    if (!ensure_directory(join(base, L"state"))) return 4;
    log_event(unattended ? "engine starting unattended" : "engine starting");
    log_environment();
    SetUnhandledExceptionFilter(exception_filter);
    integration =
        mode == L"--integration-test" || mode == L"--crash-test" || mode == L"--stress-test" ||
        mode == L"--backspace-test";
    backspace_test = mode == L"--backspace-test";
    crash_test = mode == L"--crash-test";
    stress_test = mode == L"--stress-test";
    backend_test = mode == L"--backend-test";
    bool fresh_install = GetFileAttributesW(settings_path.c_str()) == INVALID_FILE_ATTRIBUTES;
    show_tray = GetPrivateProfileIntW(L"Settings", L"ShowTray", 1, settings_path.c_str()) != 0;
    if (mode == L"--quiet" || (engine_flags & 2u)) show_tray = false;
    if (!integration && !backend_test)
        policy.pause(GetPrivateProfileIntW(L"Settings", L"Enabled", 1, settings_path.c_str()) == 0);
    transparency = GetPrivateProfileIntW(L"Settings", L"Transparency", 75, settings_path.c_str());
    if (transparency != 50 && transparency != 75 && transparency != 90 && transparency != 100)
        transparency = 75;
    if (integration && (test_transparency == 50 || test_transparency == 75 ||
                        test_transparency == 90 || test_transparency == 100))
        transparency = test_transparency;
    policy.idle_ms =
        GetPrivateProfileIntW(L"Settings", L"IdleRestore", 1, settings_path.c_str()) ? 1500 : 0;
    if (!integration && !backend_test &&
        !GetPrivateProfileIntW(L"Settings", L"SetupComplete", 0, settings_path.c_str())) {
        auto existing_startup = hide_startup::inspect(executable);
        if ((fresh_install && !existing_startup.registered) ||
            (!existing_startup.task && existing_startup.enabled && existing_startup.matches)) {
            if (!set_startup(true)) log_error("first setup could not enable sign-in startup");
        }
        if (fresh_install && !(engine_flags & 2u)) settings_requested = true;
        save_settings();
        WritePrivateProfileStringW(L"Settings", L"SetupComplete", L"1", settings_path.c_str());
    }
    if (backend_test) {
        policy.pause(true);
        policy.idle_ms = 0;
    }
    // A prior journal is recovered before a new baseline is saved. Recovery never
    // overwrites another theme's paths because only our resource paths are owned.
    if (GetFileAttributesW(active_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!restore_owned(true)) {
            log_error("startup cursor recovery failed");
            if (!unattended) MessageBoxW(nullptr, L"无法恢复上一次的指针状态。请先运行 恢复鼠标.cmd。", L"Hide",
                        MB_OK | MB_ICONERROR);
            CloseHandle(mutex);
            return 3;
        }
        DeleteFileW(active_path.c_str());
    }
    if (!restore_mouse_vanish()) {
        log_error("startup typing-hide recovery failed");
        if (!unattended) MessageBoxW(nullptr, L"无法恢复上一次的打字隐藏设置。请先运行恢复鼠标.cmd。", L"Hide",
                    MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 3;
    }
    BOOL shadow = FALSE;
    SystemParametersInfoW(SPI_GETCURSORSHADOW, 0, &shadow, 0);
    shadow_was_on = shadow != FALSE;
    if (!spawn_guard()) {
        log_error("startup guardian creation failed", GetLastError());
        if (!unattended) MessageBoxW(nullptr, L"无法准备指针恢复保护，程序已停止。", L"Hide", MB_OK | MB_ICONERROR);
        CloseHandle(mutex);
        return 5;
    }
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    focus_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    input_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event || !focus_event || !input_ready) {
        log_error("startup event creation failed", GetLastError());
        SetEvent(guard_stop);
        CloseHandle(mutex);
        return 6;
    }
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = window_proc;
    wc.lpszClassName = CLASS_NAME;
    tray_icon = (HICON)LoadImageW(nullptr, join(base, L"Hide.ico").c_str(), IMAGE_ICON, 32, 32,
                                  LR_LOADFROMFILE);
    if (!tray_icon)
        tray_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, 0);
    wc.hIcon = tray_icon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    main_window = CreateWindowExW(0, CLASS_NAME, integration ? L"Hide 自动输入验证" : L"Hide",
                                  integration ? WS_OVERLAPPEDWINDOW : 0, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 560, 220, nullptr, nullptr, instance, nullptr);
    if (!main_window) {
        log_error("startup engine window creation failed", GetLastError());
        SetEvent(guard_stop);
        CloseHandle(mutex);
        return 6;
    }
    if (!scheme_tracker.start(main_window, M_SCHEME, M_PREPARED, join(base, L"state\\cache-v2"))) {
        final_exit_code = 12;
        log_error("scheme monitor initialization failed");
        PostMessageW(main_window, WM_CLOSE, 0, 0);
    } else
        PostMessageW(main_window, M_SCHEME, 0, 0);
    update_tray();
    if (!SetTimer(main_window, 1, 250, nullptr)) {
        final_exit_code = 13;
        log_error("engine heartbeat timer creation failed", GetLastError());
        PostMessageW(main_window, WM_CLOSE, 0, 0);
    }
    if (!RegisterHotKey(main_window, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12))
        log_error("emergency hotkey registration failed", GetLastError());
    WTSRegisterSessionNotification(main_window, NOTIFY_FOR_THIS_SESSION);
    worker_thread = CreateThread(nullptr, 0, focus_main, nullptr, 0, nullptr);
    input_thread = CreateThread(nullptr, 0, input_main, nullptr, 0, nullptr);
    if (!worker_thread || !input_thread) {
        final_exit_code = 10;
        log_error("input worker thread creation failed", GetLastError());
        PostMessageW(main_window, WM_CLOSE, 0, 0);
    }
    if (integration) {
        GetCursorPos(&saved_pointer);
        test_edit =
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", backspace_test ? L"Hide deletion fixture" : L"",
                            WS_CHILD | WS_VISIBLE | ES_MULTILINE,
                            24, 30, 490, 100, main_window, nullptr, instance, nullptr);
        if (backspace_test) {
            deletion_initial_length = GetWindowTextLengthW(test_edit);
            SendMessageW(test_edit, EM_SETSEL, deletion_initial_length, deletion_initial_length);
        }
        // The first ShowWindow honors STARTUPINFO (including hidden launch).
        // Explicitly show the controlled test window after consuming that state.
        ShowWindow(main_window, SW_SHOW);
        ShowWindow(main_window, SW_SHOWNORMAL);
        DWORD foreground_thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        DWORD current_thread = GetCurrentThreadId();
        bool attached = foreground_thread && foreground_thread != current_thread &&
                        AttachThreadInput(current_thread, foreground_thread, TRUE);
        SetForegroundWindow(main_window);
        SetFocus(test_edit);
        if (attached)
            AttachThreadInput(current_thread, foreground_thread, FALSE);
        POINT p{160, 60};
        ClientToScreen(main_window, &p);
        SetCursorPos(p.x, p.y);
        test_deadline = now() + 8000;
        SetTimer(main_window, 2, 25, nullptr);
        invalidate_focus();
    } else if (settings_requested)
        PostMessageW(main_window, M_SETTINGS, 0, 0);
    MSG msg{};
    BOOL message_result = 0;
    while ((message_result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (settings_window && IsDialogMessageW(settings_window, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (message_result == -1) { final_exit_code = 14; log_error("engine message loop failed", GetLastError()); }
    shutting_down = true;
    scheme_tracker.stop();
    stopping.store(1);
    SetEvent(stop_event);
    PostThreadMessageW(input_thread_id.load(), WM_QUIT, 0, 0);
    if (input_thread)
        WaitForSingleObject(input_thread, 1000);
    if (worker_thread)
        WaitForSingleObject(worker_thread, 1500);
    restore();
    if (!restore_mouse_vanish(GetCurrentProcessId()))
        log_error("Windows typing-hide recovery retained for guardian", GetLastError());
    prepared_scheme.reset();
    if (guard_stop)
    {
        if (shared) shared->exit_status = final_exit_code;
        SetEvent(guard_stop);
    }
    if (guard_process && !supervised_engine)
        WaitForSingleObject(guard_process, 2000);
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = main_window;
    n.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &n);
    WTSUnRegisterSessionNotification(main_window);
    UnregisterHotKey(main_window, 1);
    if (tray_icon)
        DestroyIcon(tray_icon);
    for (HANDLE h : {worker_thread, input_thread, stop_event, focus_event, input_ready,
                     guard_process, guard_stop, shared_mapping, mutex})
        if (h)
            CloseHandle(h);
    if (shared)
        UnmapViewOfFile(shared);
    guard_process = nullptr;
    status();
    log_event("engine stopped", (DWORD)final_exit_code);
    return final_exit_code;
}
