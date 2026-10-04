#pragma once
#include "cursor_codec.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hide_cursor {
inline constexpr const wchar_t *roles[] = {
    L"Arrow",   L"Help",    L"AppStarting", L"Wait",   L"Crosshair", L"IBeam",
    L"NWPen",   L"No",      L"SizeNS",      L"SizeWE", L"SizeNWSE",  L"SizeNESW",
    L"SizeAll", L"UpArrow", L"Hand",        L"Pin",    L"Person"};
inline constexpr DWORD ids[] = {32512, 32651, 32650, 32514, 32515, 32513, 32631, 32648, 32645,
                                32644, 32642, 32643, 32646, 32516, 32649, 32671, 32672};
struct Value {
    DWORD type = 0;
    bool exists = false;
    Bytes bytes;
    bool operator==(const Value &o) const {
        return type == o.type && exists == o.exists && bytes == o.bytes;
    }
};
struct Snapshot {
    std::vector<Value> values;
    std::vector<std::wstring> sources;
    std::vector<uint64_t> stamps;
    bool operator==(const Snapshot &o) const {
        return values == o.values && sources == o.sources && stamps == o.stamps;
    }
};
Snapshot snapshot();
// Restore the latest role paths, including roles SPI_SETCURSORS may leave unchanged.
// This never writes cursor preferences or selects a named saved scheme.
bool restore_current_cursors();
std::vector<uint64_t> cursor_appearance(HCURSOR cursor);
struct Prepared {
    uint64_t version = 0;
    unsigned transparency = 0;
    Snapshot source;
    std::vector<HCURSOR> cursors;
    std::vector<SIZE> sizes;
    std::vector<std::wstring> files;
    std::vector<std::vector<uint64_t>> original_appearances;
    std::wstring directory;
    std::string error;
    ~Prepared();
    bool valid() const { return error.empty() && cursors.size() == _countof(roles); }
};
bool live_cursors_match(const Prepared &prepared);
class SchemeTracker {
    HANDLE stop_ = nullptr, request_ = nullptr, thread_ = nullptr;
    HWND window_ = nullptr;
    UINT changed_ = 0, ready_ = 0;
    std::wstring cache_;
    std::mutex mutex_;
    Snapshot requested_source_;
    unsigned requested_transparency_ = 75;
    std::atomic<uint64_t> version_{0};
    std::unique_ptr<Prepared> result_;
    static DWORD WINAPI run(void *self);
    void loop();

  public:
    ~SchemeTracker();
    bool start(HWND window, UINT changed, UINT ready, const std::wstring &cache);
    uint64_t prepare(const Snapshot &source, unsigned transparency);
    void invalidate();
    std::unique_ptr<Prepared> take();
    uint64_t version() const { return version_.load(); }
    void stop();
};
} // namespace hide_cursor
