#include "CredentialStore.h"
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#elif defined(GB_HAVE_LIBSECRET)
#include <libsecret/secret.h>
#endif

namespace CredentialStore {
#ifdef _WIN32
namespace {
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out((size_t)n, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), out.data(), n) != n) return {};
    return out;
}
}
bool persistentAvailable() { return true; }
bool store(const std::string& key, const std::string& secret, std::string& error) {
    auto target = wide("GutsAndBolts/Fyxa/" + key);
    if (target.empty() || secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) { error = "Credential name or value is invalid or too large."; return false; }
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = target.data();
    c.CredentialBlobSize = (DWORD)secret.size();
    c.CredentialBlob = (LPBYTE)secret.data();
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&c, 0)) { error = "Windows Credential Manager couldn't save this credential."; return false; }
    return true;
}
bool retrieve(const std::string& key, std::string& secret, std::string& error) {
    auto target = wide("GutsAndBolts/Fyxa/" + key);
    PCREDENTIALW c = nullptr;
    if (target.empty() || !CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &c)) { error = "Credential isn't available in Windows Credential Manager."; return false; }
    secret.assign((const char*)c->CredentialBlob, c->CredentialBlobSize);
    CredFree(c);
    return true;
}
bool erase(const std::string& key, std::string& error) {
    auto target = wide("GutsAndBolts/Fyxa/" + key);
    if (target.empty()) { error = "Credential name is invalid."; return false; }
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) { error = "Windows Credential Manager couldn't remove this credential."; return false; }
    return true;
}
#elif defined(__APPLE__)
namespace {
const char* kService = "GutsAndBolts Fyxa";
}
bool persistentAvailable() { return true; }
bool store(const std::string& key, const std::string& secret, std::string& error) {
    SecKeychainItemRef item = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(nullptr, (UInt32)std::strlen(kService), kService,
        (UInt32)key.size(), key.data(), nullptr, nullptr, &item);
    if (status == errSecSuccess && item) {
        status = SecKeychainItemModifyAttributesAndData(item, nullptr, (UInt32)secret.size(), secret.data());
        CFRelease(item);
    } else {
        status = SecKeychainAddGenericPassword(nullptr, (UInt32)std::strlen(kService), kService,
            (UInt32)key.size(), key.data(), (UInt32)secret.size(), secret.data(), nullptr);
    }
    if (status != errSecSuccess) { error = "macOS Keychain couldn't save this credential."; return false; }
    return true;
}
bool retrieve(const std::string& key, std::string& secret, std::string& error) {
    UInt32 length = 0; void* data = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(nullptr, (UInt32)std::strlen(kService), kService,
        (UInt32)key.size(), key.data(), &length, &data, nullptr);
    if (status != errSecSuccess) { error = "Credential isn't available in macOS Keychain."; return false; }
    secret.assign((const char*)data, length);
    SecKeychainItemFreeContent(nullptr, data);
    return true;
}
bool erase(const std::string& key, std::string& error) {
    SecKeychainItemRef item = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(nullptr, (UInt32)std::strlen(kService), kService,
        (UInt32)key.size(), key.data(), nullptr, nullptr, &item);
    if (status == errSecItemNotFound) return true;
    if (status != errSecSuccess || !item) { error = "macOS Keychain couldn't find this credential."; return false; }
    status = SecKeychainItemDelete(item);
    CFRelease(item);
    if (status != errSecSuccess) { error = "macOS Keychain couldn't remove this credential."; return false; }
    return true;
}
#elif defined(GB_HAVE_LIBSECRET)
namespace {
const SecretSchema kFyxaSchema = {
    "com.gutsandbolts.Fyxa", SECRET_SCHEMA_NONE,
    { { "key", SECRET_SCHEMA_ATTRIBUTE_STRING }, { nullptr, (SecretSchemaAttributeType)0 } }
};
}
bool persistentAvailable() { return true; }
bool store(const std::string& key, const std::string& secret, std::string& error) {
    GError* e = nullptr;
    const gboolean ok = secret_password_store_sync(&kFyxaSchema, SECRET_COLLECTION_DEFAULT,
        "Guts & Bolts Fyxa credential", secret.c_str(), nullptr, &e, "key", key.c_str(), nullptr);
    if (!ok) {
        error = e ? e->message : "Linux Secret Service couldn't save this credential.";
        if (e) g_error_free(e);
        return false;
    }
    return true;
}
bool retrieve(const std::string& key, std::string& secret, std::string& error) {
    GError* e = nullptr;
    gchar* value = secret_password_lookup_sync(&kFyxaSchema, nullptr, &e, "key", key.c_str(), nullptr);
    if (!value) {
        error = e ? e->message : "Credential isn't available in Linux Secret Service.";
        if (e) g_error_free(e);
        return false;
    }
    secret.assign(value);
    secret_password_free(value);
    return true;
}
bool erase(const std::string& key, std::string& error) {
    GError* e = nullptr;
    const gboolean ok = secret_password_clear_sync(&kFyxaSchema, nullptr, &e, "key", key.c_str(), nullptr);
    if (!ok) {
        error = e ? e->message : "Linux Secret Service couldn't remove this credential.";
        if (e) g_error_free(e);
        return false;
    }
    return true;
}
#else
bool persistentAvailable() { return false; }
bool store(const std::string&, const std::string&, std::string& error) { error = "No supported OS credential vault is available in this build; the credential is session-only."; return false; }
bool retrieve(const std::string&, std::string&, std::string& error) { error = "No persistent credential vault is available in this build."; return false; }
bool erase(const std::string&, std::string&) { return true; }
#endif
}
