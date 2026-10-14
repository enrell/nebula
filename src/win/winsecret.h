#pragma once
// Secrets in the Windows Credential Manager (per user, encrypted by the OS). Plain Win32, no Qt.
#include <string>

namespace winsecret {

// A generic credential named `target` (shown in the Credential Manager as that name). Values are UTF-8 text.
bool set(const std::wstring &target, const std::string &value);
bool get(const std::wstring &target, std::string *value);
bool remove(const std::wstring &target);

} // namespace winsecret
