#include "winsecret.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>

namespace winsecret {

bool set(const std::wstring &target, const std::string &value) {
    if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) return false;
    std::wstring name = target;   // CREDENTIALW wants non-const strings
    std::wstring user = L"nebula";
    CREDENTIALW c = {};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = name.data();
    c.UserName = user.data();
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.CredentialBlobSize = DWORD(value.size());
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.data()));
    return CredWriteW(&c, 0) != 0;
}

bool get(const std::wstring &target, std::string *value) {
    PCREDENTIALW c = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &c) || !c) return false;
    value->assign(reinterpret_cast<const char *>(c->CredentialBlob), c->CredentialBlobSize);
    CredFree(c);
    return true;
}

bool remove(const std::wstring &target) { return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != 0; }

} // namespace winsecret
