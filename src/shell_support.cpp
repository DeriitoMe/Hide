#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "shell_support.hpp"
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>

namespace hide_shell {
bool create_settings_shortcut(const std::wstring &exe, std::wstring &saved, HRESULT &error) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) { error = init; return false; }
    IShellLinkW *link = nullptr;
    IPersistFile *file = nullptr;
    PWSTR desktop = nullptr;
    error = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop);
    if (SUCCEEDED(error)) saved = std::wstring(desktop) + L"\\Hide 设置.lnk";
    CoTaskMemFree(desktop);
    if (SUCCEEDED(error)) error = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                                 IID_IShellLinkW, (void **)&link);
    if (SUCCEEDED(error)) error = link->QueryInterface(IID_IPersistFile, (void **)&file);
    if (SUCCEEDED(error) && GetFileAttributesW(saved.c_str()) != INVALID_FILE_ATTRIBUTES) {
        error = file->Load(saved.c_str(), STGM_READ);
        wchar_t previous[32768]{};
        if (SUCCEEDED(error)) error = link->GetPath(previous, _countof(previous), nullptr, SLGP_RAWPATH);
        auto leaf = std::wstring(previous);
        leaf = leaf.substr(leaf.find_last_of(L"\\/") + 1);
        if (SUCCEEDED(error) && _wcsicmp(leaf.c_str(), L"Hide.exe") != 0)
            error = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    if (SUCCEEDED(error)) error = link->SetPath(exe.c_str());
    if (SUCCEEDED(error)) error = link->SetArguments(L"--settings");
    auto directory = exe.substr(0, exe.find_last_of(L"\\/"));
    if (SUCCEEDED(error)) error = link->SetWorkingDirectory(directory.c_str());
    if (SUCCEEDED(error)) error = link->SetIconLocation(exe.c_str(), 0);
    if (SUCCEEDED(error)) error = link->SetDescription(L"打开 Hide 设置；关闭设置窗口后继续运行");
    if (SUCCEEDED(error)) error = link->SetHotkey(MAKEWORD('H', HOTKEYF_CONTROL | HOTKEYF_ALT));
    if (SUCCEEDED(error)) error = link->SetShowCmd(SW_SHOWNORMAL);
    if (SUCCEEDED(error)) error = file->Save(saved.c_str(), TRUE);
    if (SUCCEEDED(error)) SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, saved.c_str(), nullptr);
    if (file) file->Release();
    if (link) link->Release();
    if (SUCCEEDED(init)) CoUninitialize();
    return SUCCEEDED(error);
}
}
