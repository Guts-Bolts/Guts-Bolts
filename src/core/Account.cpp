#include "Account.h"
#include "OfficialKey.h"
#include "Log.h"
#include "Paths.h"
#include "Version.h"

#include <monocypher.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace Account {

namespace {

namespace fs = std::filesystem;

struct Keys {
    uint8_t     secret[64] = {};
    uint8_t     pub[32] = {};
    std::string id;
    bool        ready = false;
};

std::string toHex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; ++i) { s[2 * i] = d[p[i] >> 4]; s[2 * i + 1] = d[p[i] & 15]; }
    return s;
}

bool fromHex(const std::string& s, uint8_t* out, size_t n) {
    if (s.size() != n * 2) return false;
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < n; ++i) {
        int hi = v(s[2 * i]), lo = v(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return true;
}

bool osRandom(uint8_t* out, size_t n) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    std::ifstream f("/dev/urandom", std::ios::binary);
    return f && f.read(reinterpret_cast<char*>(out), (std::streamsize)n);
#endif
}

fs::path keyFile() { return folder() / "account.key"; }

Keys& keys() {
    static Keys k;
    if (k.ready) return k;
    // Load the saved key...
    std::ifstream in(keyFile());
    std::string hex;
    if (in && (in >> hex) && fromHex(hex, k.secret, 64)) {
        std::memcpy(k.pub, k.secret + 32, 32);   // Monocypher keeps the public half at the end
    } else {
        // ...or make a new one.
        uint8_t seed[32];
        if (!osRandom(seed, 32)) Log::error("Couldn't get random numbers for your account key.");
        crypto_eddsa_key_pair(k.secret, k.pub, seed);   // wipes the seed
        std::error_code ec;
        fs::create_directories(folder(), ec);
        std::ofstream out(keyFile(), std::ios::trunc);
        if (out) out << toHex(k.secret, 64) << "\n";
        out.close();
#ifndef _WIN32
        fs::permissions(keyFile(), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
#endif
    }
    k.id = toHex(k.pub, 32);
    k.ready = true;
    return k;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// A key written by --create-staff-account on this computer (before it's
// been added to OfficialKey.h for everyone).
fs::path localOfficialFile() { return fs::path(GB_SOURCE_DIR) / "official_key.local.txt"; }

} // namespace

fs::path folder() {
    if (const char* o = std::getenv("GB_ACCOUNT_DIR"); o && *o) return o;   // tests: several accounts on one PC
#if defined(__ANDROID__)
    return Paths::appFolder() / "account";   // the app's private storage
#elif defined(__EMSCRIPTEN__)
    return "/save/account";   // a browser: kept in its storage by web/index.html (so you stay logged in)
#elif defined(_WIN32)
    if (const char* a = std::getenv("APPDATA")) return fs::path(a) / "GutsAndBolts";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) return fs::path(h) / "Library/Application Support/GutsAndBolts";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return fs::path(x) / "gutsandbolts";
    if (const char* h = std::getenv("HOME")) return fs::path(h) / ".config/gutsandbolts";
#endif
    return fs::current_path();
}

const std::string& id() { return keys().id; }
std::string shortId() { return id().substr(0, 8); }

std::string sign(const std::string& message) {
    uint8_t sig[64];
    crypto_eddsa_sign(sig, keys().secret, reinterpret_cast<const uint8_t*>(message.data()), message.size());
    return toHex(sig, 64);
}

bool verify(const std::string& idHex, const std::string& message, const std::string& sigHex) {
    uint8_t pub[32], sig[64];
    if (!fromHex(idHex, pub, 32) || !fromHex(sigHex, sig, 64)) return false;
    return crypto_eddsa_check(sig, pub, reinterpret_cast<const uint8_t*>(message.data()), message.size()) == 0;
}

namespace {
std::string& officialCache() {
    static std::string cached = [] {
        std::string k = lower(kOfficialKey);
        std::ifstream f(localOfficialFile());
        std::string local;
        if (f && (f >> local)) {
            uint8_t tmp[32];
            if (fromHex(local, tmp, 32)) k = lower(local);
        }
        return k;
    }();
    return cached;
}
} // namespace

std::string officialId() { return officialCache(); }
void setOfficialId(const std::string& idHex) { officialCache() = lower(idHex); }

bool isOfficial(const std::string& idHex) {
    std::string o = officialId();
    return !o.empty() && lower(idHex) == o;
}

bool iAmStaff() { return isOfficial(id()); }

bool nameIsReserved(const std::string& name) {
    std::string n = lower(name);
    n.erase(std::remove_if(n.begin(), n.end(), [](char c) { return c == ' ' || c == '_' || c == '.'; }), n.end());
    return n == lower(kStaffName);
}

std::string randomHex(int bytes) {
    std::vector<uint8_t> b((size_t)bytes);
    osRandom(b.data(), b.size());
    return toHex(b.data(), b.size());
}

namespace {
void writeKey(const Keys& k) {
    std::error_code ec;
    fs::create_directories(folder(), ec);
    std::ofstream out(keyFile(), std::ios::trunc);
    if (out) out << toHex(k.secret, 64) << "\n";
    out.close();
#ifndef _WIN32
    fs::permissions(keyFile(), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
#endif
}
} // namespace

bool passwordKeys(const std::string& password, const std::string& saltHex, std::string& lockKeyHex, std::string& authHex) {
    uint8_t salt[16];
    if (password.empty() || !fromHex(saltHex, salt, 16)) return false;
    // Argon2id, 32 MB, 3 passes: slow enough to make guessing passwords expensive,
    // quick enough for a phone.
    crypto_argon2_config cfg = {CRYPTO_ARGON2_ID, 32 * 1024, 3, 1};
    std::vector<uint8_t> work((size_t)cfg.nb_blocks * 1024);
    crypto_argon2_inputs in = {reinterpret_cast<const uint8_t*>(password.data()), salt, (uint32_t)password.size(), 16};
    uint8_t out[64];
    crypto_argon2(out, 64, work.data(), cfg, in, crypto_argon2_no_extras);
    lockKeyHex = toHex(out, 32);
    authHex = toHex(out + 32, 32);
    crypto_wipe(out, 64);
    crypto_wipe(work.data(), work.size());
    return true;
}

std::string backupKey(const std::string& lockKeyHex) {
    uint8_t key[32], nonce[24], mac[16], cipher[64];
    if (!fromHex(lockKeyHex, key, 32) || !osRandom(nonce, 24)) return "";
    crypto_aead_lock(cipher, mac, key, nonce, nullptr, 0, keys().secret, 64);
    crypto_wipe(key, 32);
    return toHex(nonce, 24) + toHex(mac, 16) + toHex(cipher, 64);
}

bool restoreKey(const std::string& lockKeyHex, const std::string& blobHex) {
    uint8_t key[32], nonce[24], mac[16], cipher[64];
    if (blobHex.size() != (24 + 16 + 64) * 2 || !fromHex(lockKeyHex, key, 32) ||
        !fromHex(blobHex.substr(0, 48), nonce, 24) || !fromHex(blobHex.substr(48, 32), mac, 16) ||
        !fromHex(blobHex.substr(80), cipher, 64))
        return false;
    Keys k;
    bool ok = crypto_aead_unlock(k.secret, mac, key, nonce, nullptr, 0, cipher, 64) == 0;
    crypto_wipe(key, 32);
    if (!ok) return false;
    std::memcpy(k.pub, k.secret + 32, 32);
    writeKey(k);
    Keys& live = keys();
    std::memcpy(live.secret, k.secret, 64);
    std::memcpy(live.pub, k.pub, 32);
    live.id = toHex(k.pub, 32);
    crypto_wipe(k.secret, 64);
    return true;
}

void newKey() {
    Keys& live = keys();
    uint8_t seed[32];
    if (!osRandom(seed, 32)) Log::error("Couldn't get random numbers for your account key.");
    crypto_eddsa_key_pair(live.secret, live.pub, seed);
    live.id = toHex(live.pub, 32);
    writeKey(live);
}

std::string hashHex(const std::string& data) {
    uint8_t h[32];
    crypto_blake2b(h, 32, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    return toHex(h, 32);
}

std::string createStaffAccount() {
    const std::string& me = id();
    std::string msg;
    std::string current = lower(kOfficialKey);
    if (!current.empty() && current != me) {
        return "This computer's account is not the official staff account, and one already exists.\n"
               "Only the computer that made it (with its account.key) can be Guts.";
    }
    std::ofstream f(localOfficialFile(), std::ios::trunc);
    if (!f) {
        return "Couldn't write " + localOfficialFile().string() + ".\n"
               "Your account ID is:\n" + me;
    }
    f << me << "\n";
    officialCache() = me;
    msg = "You are now the official staff account (Guts) on this computer!\n\n"
          "Your secret key is in:\n  " + keyFile().string() + "\n"
          "Back that file up and never share it: it IS your staff account.\n\n"
          "To make every copy of Guts&Bolts recognise you, put this public ID into\n"
          "src/core/OfficialKey.h (it's safe to share):\n  " + me;
    return msg;
}

} // namespace Account
