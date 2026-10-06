#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "startup.hpp"
#include <taskschd.h>
#include <sddl.h>
#include <oleauto.h>
#include <vector>

namespace hide_startup {
namespace {
template<class T> struct Ptr {
    T *p = nullptr;
    ~Ptr() { if (p) p->Release(); }
    T **out() { return &p; }
    T *operator->() { return p; }
};
struct Text {
    BSTR p;
    explicit Text(const wchar_t *value) : p(SysAllocString(value)) {}
    ~Text() { SysFreeString(p); }
    operator BSTR() const { return p; }
};
struct Session {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Ptr<ITaskService> service;
    Ptr<ITaskFolder> folder;
    std::wstring sid, name;
    ~Session() {
        // Release COM pointers before balancing this apartment.
        if (folder.p) { folder.p->Release(); folder.p = nullptr; }
        if (service.p) { service.p->Release(); service.p = nullptr; }
        if (SUCCEEDED(init)) CoUninitialize();
    }
    HRESULT connect() {
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) return init;
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return HRESULT_FROM_WIN32(GetLastError());
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<BYTE> bytes(size);
        bool ok = size && GetTokenInformation(token, TokenUser, bytes.data(), size, &size);
        DWORD error = GetLastError();
        CloseHandle(token);
        if (!ok) return HRESULT_FROM_WIN32(error);
        LPWSTR value = nullptr;
        if (!ConvertSidToStringSidW(((TOKEN_USER *)bytes.data())->User.Sid, &value))
            return HRESULT_FROM_WIN32(GetLastError());
        sid = value;
        LocalFree(value);
        name = L"Hide-" + sid;
        HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_ITaskService, (void **)service.out());
        VARIANT empty{};
        if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
        if (SUCCEEDED(hr)) hr = service->GetFolder(Text(L"\\"), folder.out());
        return hr;
    }
};
bool same(const wchar_t *a, const std::wstring &b) {
    return a && _wcsicmp(a, b.c_str()) == 0;
}
bool legacy(const std::wstring &exe) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    wchar_t text[32768]{};
    DWORD bytes = sizeof(text), type = 0;
    LONG result = RegQueryValueExW(key, L"Hide", nullptr, &type, (BYTE *)text, &bytes);
    RegCloseKey(key);
    if (result || type != REG_SZ || bytes < 2 || bytes % 2 || text[bytes / 2 - 1]) return false;
    return same(text, L"\"" + exe + L"\"") || same(text, L"\"" + exe + L"\" --autostart");
}
LONG write_legacy(const std::wstring &exe, bool enable) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr);
    if (result) return result;
    if (enable) {
        auto command = L"\"" + exe + L"\" --autostart";
        result = RegSetValueExW(key, L"Hide", 0, REG_SZ, (BYTE *)command.c_str(),
                              (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, L"Hide");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result;
}
HRESULT register_task(Session &s, const std::wstring &exe) {
    Ptr<ITaskDefinition> definition;
    Ptr<IPrincipal> principal;
    Ptr<IRegistrationInfo> info;
    Ptr<ITaskSettings> settings;
    Ptr<ITriggerCollection> triggers;
    Ptr<ITrigger> trigger;
    Ptr<ILogonTrigger> logon;
    Ptr<ITrigger> health;
    Ptr<IRepetitionPattern> repetition;
    Ptr<IActionCollection> actions;
    Ptr<IAction> action;
    Ptr<IExecAction> exec;
    Ptr<IRegisteredTask> registered;
    HRESULT hr = s.service->NewTask(0, definition.out());
    if (SUCCEEDED(hr)) hr = definition->get_RegistrationInfo(info.out());
    if (SUCCEEDED(hr)) hr = info->put_Author(Text(L"Hide"));
    if (SUCCEEDED(hr)) hr = info->put_Description(Text(L"Hide cursor fading at user sign-in"));
    if (SUCCEEDED(hr)) hr = definition->get_Principal(principal.out());
    if (SUCCEEDED(hr)) hr = principal->put_UserId(Text(s.sid.c_str()));
    if (SUCCEEDED(hr)) hr = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
    if (SUCCEEDED(hr)) hr = principal->put_RunLevel(TASK_RUNLEVEL_LUA);
    if (SUCCEEDED(hr)) hr = definition->get_Settings(settings.out());
    if (SUCCEEDED(hr)) hr = settings->put_Enabled(VARIANT_TRUE);
    if (SUCCEEDED(hr)) hr = settings->put_AllowDemandStart(VARIANT_TRUE);
    if (SUCCEEDED(hr)) hr = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    if (SUCCEEDED(hr)) hr = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = settings->put_ExecutionTimeLimit(Text(L"PT0S"));
    if (SUCCEEDED(hr)) hr = settings->put_RestartCount(3);
    if (SUCCEEDED(hr)) hr = settings->put_RestartInterval(Text(L"PT1M"));
    if (SUCCEEDED(hr)) hr = definition->get_Triggers(triggers.out());
    if (SUCCEEDED(hr)) hr = triggers->Create(TASK_TRIGGER_LOGON, trigger.out());
    if (SUCCEEDED(hr)) hr = trigger->QueryInterface(IID_ILogonTrigger, (void **)logon.out());
    if (SUCCEEDED(hr)) hr = logon->put_UserId(Text(s.sid.c_str()));
    if (SUCCEEDED(hr)) hr = logon->put_Delay(Text(L"PT10S"));
    if (SUCCEEDED(hr)) hr = triggers->Create(TASK_TRIGGER_TIME, health.out());
    if (SUCCEEDED(hr)) hr = health->put_Id(Text(L"HealthCheck"));
    FILETIME clock{};
    GetSystemTimeAsFileTime(&clock);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = clock.dwLowDateTime; ticks.HighPart = clock.dwHighDateTime;
    ticks.QuadPart += 600000000ull;
    clock.dwLowDateTime = ticks.LowPart; clock.dwHighDateTime = ticks.HighPart;
    SYSTEMTIME boundary{};
    FileTimeToSystemTime(&clock, &boundary);
    wchar_t time[32]{};
    swprintf_s(time, L"%04u-%02u-%02uT%02u:%02u:%02uZ", boundary.wYear, boundary.wMonth,
               boundary.wDay, boundary.wHour, boundary.wMinute, boundary.wSecond);
    if (SUCCEEDED(hr)) hr = health->put_StartBoundary(Text(time));
    if (SUCCEEDED(hr)) hr = health->get_Repetition(repetition.out());
    if (SUCCEEDED(hr)) hr = repetition->put_Interval(Text(L"PT1M"));
    if (SUCCEEDED(hr)) hr = definition->get_Actions(actions.out());
    if (SUCCEEDED(hr)) hr = actions->Create(TASK_ACTION_EXEC, action.out());
    if (SUCCEEDED(hr)) hr = action->QueryInterface(IID_IExecAction, (void **)exec.out());
    if (SUCCEEDED(hr)) hr = exec->put_Path(Text(exe.c_str()));
    if (SUCCEEDED(hr)) hr = exec->put_Arguments(Text(L"--supervise"));
    auto directory = exe.substr(0, exe.find_last_of(L"\\/"));
    if (SUCCEEDED(hr)) hr = exec->put_WorkingDirectory(Text(directory.c_str()));
    VARIANT user{}, empty{};
    user.vt = VT_BSTR;
    user.bstrVal = SysAllocString(s.sid.c_str());
    if (SUCCEEDED(hr)) hr = s.folder->RegisterTaskDefinition(Text(s.name.c_str()), definition.p,
        TASK_CREATE_OR_UPDATE, user, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, registered.out());
    VariantClear(&user);
    return hr;
}
} // namespace
Status inspect(const std::wstring &exe) {
    Status result;
    Session session;
    HRESULT hr = session.connect();
    Ptr<IRegisteredTask> registered;
    if (SUCCEEDED(hr)) hr = session.folder->GetTask(Text(session.name.c_str()), registered.out());
    if (SUCCEEDED(hr)) {
        result.task = result.registered = true;
        VARIANT_BOOL enabled = VARIANT_FALSE;
        hr = registered->get_Enabled(&enabled);
        result.enabled = SUCCEEDED(hr) && enabled != VARIANT_FALSE;
        registered->get_LastTaskResult(&result.last_result);
        Ptr<ITaskDefinition> definition;
        Ptr<IActionCollection> actions;
        Ptr<IAction> action;
        Ptr<IExecAction> exec;
        LONG count = 0;
        if (SUCCEEDED(hr)) hr = registered->get_Definition(definition.out());
        if (SUCCEEDED(hr)) hr = definition->get_Actions(actions.out());
        if (SUCCEEDED(hr)) hr = actions->get_Count(&count);
        if (SUCCEEDED(hr) && count == 1) hr = actions->get_Item(1, action.out());
        else if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
        if (SUCCEEDED(hr)) hr = action->QueryInterface(IID_IExecAction, (void **)exec.out());
        BSTR path = nullptr, arguments = nullptr;
        if (SUCCEEDED(hr)) hr = exec->get_Path(&path);
        if (SUCCEEDED(hr)) hr = exec->get_Arguments(&arguments);
        result.matches = SUCCEEDED(hr) && same(path, exe) && same(arguments, L"--supervise");
        SysFreeString(path);
        SysFreeString(arguments);
        result.error = hr;
        return result; // A disabled task is never bypassed by the legacy channel.
    }
    result.error = hr;
    result.registered = result.enabled = result.matches = legacy(exe);
    return result;
}
bool configure(const std::wstring &exe, bool enable, HRESULT &error) {
    Session session;
    error = session.connect();
    if (!enable) {
        if (SUCCEEDED(error)) {
            error = session.folder->DeleteTask(Text(session.name.c_str()), 0);
            if (error == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
                error == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) error = S_OK;
        }
        LONG legacy_error = write_legacy(exe, false);
        if (legacy_error) error = HRESULT_FROM_WIN32(legacy_error);
        return SUCCEEDED(error);
    }
    if (SUCCEEDED(error)) error = register_task(session, exe);
    if (SUCCEEDED(error)) {
        // The working task is committed before removing the previous registration.
        LONG remove_error = write_legacy(exe, false);
        if (remove_error) error = HRESULT_FROM_WIN32(remove_error);
        return !remove_error;
    }
    // Keep the old channel usable when Task Scheduler is unavailable. Do not
    // bypass an existing task (particularly one disabled by the user).
    auto existing = inspect(exe);
    if (existing.task) return false;
    LONG fallback_error = write_legacy(exe, true);
    if (fallback_error) error = HRESULT_FROM_WIN32(fallback_error);
    return fallback_error == ERROR_SUCCESS;
}
bool health_check(const std::wstring &exe, bool enable) {
    auto current = inspect(exe);
    if (!current.task || !current.enabled || !current.matches) return true;
    Session s;
    HRESULT hr = s.connect();
    Ptr<IRegisteredTask> registered;
    Ptr<ITaskDefinition> definition;
    Ptr<ITriggerCollection> triggers;
    if (SUCCEEDED(hr)) hr = s.folder->GetTask(Text(s.name.c_str()), registered.out());
    if (SUCCEEDED(hr)) hr = registered->get_Definition(definition.out());
    if (SUCCEEDED(hr)) hr = definition->get_Triggers(triggers.out());
    LONG count = 0;
    if (SUCCEEDED(hr)) hr = triggers->get_Count(&count);
    for (LONG index = 1; index <= count && SUCCEEDED(hr); ++index) {
        Ptr<ITrigger> trigger;
        hr = triggers->get_Item(index, trigger.out());
        BSTR id = nullptr;
        if (SUCCEEDED(hr)) hr = trigger->get_Id(&id);
        bool match = same(id, L"HealthCheck");
        SysFreeString(id);
        if (!match) continue;
        VARIANT_BOOL enabled = VARIANT_FALSE;
        if (SUCCEEDED(hr)) hr = trigger->get_Enabled(&enabled);
        if (SUCCEEDED(hr) && (enabled != VARIANT_FALSE) == enable) return true;
        if (SUCCEEDED(hr)) hr = trigger->put_Enabled(enable ? VARIANT_TRUE : VARIANT_FALSE);
        VARIANT user{}, empty{};
        user.vt = VT_BSTR; user.bstrVal = SysAllocString(s.sid.c_str());
        Ptr<IRegisteredTask> updated;
        if (SUCCEEDED(hr)) hr = s.folder->RegisterTaskDefinition(Text(s.name.c_str()), definition.p,
            TASK_UPDATE, user, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, updated.out());
        VariantClear(&user);
        return SUCCEEDED(hr);
    }
    return false;
}
} // namespace hide_startup
