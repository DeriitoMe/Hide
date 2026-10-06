#pragma once
#include <windows.h>
#include <string>

namespace hide_shell {
bool create_settings_shortcut(const std::wstring &executable, std::wstring &saved_path, HRESULT &error);
}
