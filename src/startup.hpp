#pragma once
#include <windows.h>
#include <string>

namespace hide_startup {
struct Status {
    bool registered = false, enabled = false, matches = false;
    bool task = false;
    LONG last_result = 0;
    HRESULT error = S_OK;
};
Status inspect(const std::wstring &executable);
// Configuration requests and fresh-install setup mutate startup registration;
// inspection never enables a task disabled by the user.
bool configure(const std::wstring &executable, bool enable, HRESULT &error);
bool health_check(const std::wstring &executable, bool enable);
} // namespace hide_startup
