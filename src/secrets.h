#pragma once
#include <QString>

// API-key storage. Uses the freedesktop Secret Service (gnome-keyring, kwallet, keepassxc...) through `secret-tool`.
// Falls back to a 0600 file in ~/.config/nebula when no keyring is available (or NEBULA_SECRETS=file).
namespace SecretStore {
QString backend();                                                    // "keyring" or "file"
bool set(const QString &profile, const QString &var, const QString &value);
QString get(const QString &profile, const QString &var);
void remove(const QString &profile, const QString &var);
bool has(const QString &profile, const QString &var);
}
