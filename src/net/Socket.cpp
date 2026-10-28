#include "Socket.h"

#include <csignal>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <vector>

#ifdef GB_WEBSOCKETS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>
#include "CaCerts.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/websocket.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#define GB_CLOSE closesocket
#define GB_WOULDBLOCK (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAEINPROGRESS)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define GB_CLOSE ::close
#define GB_WOULDBLOCK (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS)
#endif

namespace Net {

namespace {

constexpr size_t kMaxMessage = 64u * 1024u * 1024u;

bool initSockets() {
#ifdef _WIN32
    static bool ok = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
#else
    static bool ok = [] {
        signal(SIGPIPE, SIG_IGN);   // a dropped connection must not kill the game
        return true;
    }();
    return ok;
#endif
}

void setNonBlocking(intptr_t s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &on);
#else
    int flags = fcntl((int)s, F_GETFL, 0);
    fcntl((int)s, F_SETFL, flags | O_NONBLOCK);
#endif
    int one = 1;
    setsockopt((decltype(socket(0, 0, 0)))s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

// Open a TCP connection (non-blocking). Returns -1 and the reason on failure.
intptr_t tcpConnect(const std::string& host, int port, std::string& error, int timeoutMs) {
    if (!initSockets()) { error = "Networking isn't available on this computer."; return -1; }

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        error = "Couldn't find \"" + host + "\". Check the address.";
        return -1;
    }

    auto s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if ((intptr_t)s < 0) { freeaddrinfo(res); error = "Couldn't create a network socket."; return -1; }
    setNonBlocking((intptr_t)s);

    int rc = ::connect(s, res->ai_addr, (socklen_t)res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0 && !GB_WOULDBLOCK) {
        GB_CLOSE(s);
        error = "Couldn't connect to " + host + ":" + portStr + ".";
        return -1;
    }
    if (rc != 0) {
        fd_set wr;
        FD_ZERO(&wr);
        FD_SET(s, &wr);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        if (select((int)s + 1, nullptr, &wr, nullptr, &tv) <= 0) {
            GB_CLOSE(s);
            error = "Nobody answered at " + host + ":" + portStr + ". Is the game hosted and the port open?";
            return -1;
        }
        int soErr = 0;
        socklen_t len = sizeof(soErr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soErr, &len);
        if (soErr != 0) {
            GB_CLOSE(s);
            error = "The connection to " + host + ":" + portStr + " was refused.";
            return -1;
        }
    }
    return (intptr_t)s;
}


// ---------------------------------------------------------------------------
// WebSockets over TLS, for Guts&Bolts servers on the web (wss://)
// ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

// Wait until the socket can be read (or written). false = the time ran out.
bool waitFor(intptr_t sock, bool write, Clock::time_point deadline) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    if (left <= 0) return false;
    auto s = (decltype(socket(0, 0, 0)))sock;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{};
    tv.tv_sec  = (decltype(tv.tv_sec))(left / 1000);
    tv.tv_usec = (decltype(tv.tv_usec))((left % 1000) * 1000);
    return select((int)s + 1, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv) > 0;
}

// "wss://example.com" -> ("example.com", encrypted)
std::string webHostName(const std::string& host, bool& tls) {
    tls = host.rfind("wss://", 0) == 0;
    return host.substr(tls ? 6 : 5);
}

#ifdef GB_WEBSOCKETS
// mbedTLS is built without thread support, and TLS 1.3 keeps its keys in one
// shared store, so two connections on different threads (the app sends each
// request on its own thread) can break each other's handshake. Only one
// thread uses it at a time; each call is short (the sockets don't block).
std::mutex& tlsMutex() {
    static std::mutex m;
    return m;
}
#endif

} // namespace

bool isWebAddress(const std::string& host) { return host.rfind("wss://", 0) == 0 || host.rfind("ws://", 0) == 0; }

struct Connection::Web {
    intptr_t    sock = -1;
    bool        tls = false;
    std::string in;        // bytes received, not yet whole WebSocket frames
    std::string partial;   // a message arriving in several frames
    std::mt19937 rng{std::random_device{}()};
#ifdef __EMSCRIPTEN__
    // In a browser there are no sockets: the page's own WebSocket carries the messages.
    EMSCRIPTEN_WEBSOCKET_T   ws = 0;
    bool                     open = false, closed = false;
    std::vector<std::string> outbox;   // whole messages, waiting until it's open
    std::string              arrived;  // length-prefixed messages, like m_in
#endif
#ifdef GB_WEBSOCKETS
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context drbg;
    bool                     sslReady = false;
#endif
    ~Web() {
#ifdef __EMSCRIPTEN__
        if (ws) { emscripten_websocket_close(ws, 1000, ""); emscripten_websocket_delete(ws); }
#endif
#ifdef GB_WEBSOCKETS
        if (sslReady) {
            std::lock_guard<std::mutex> lock(tlsMutex());
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ctr_drbg_free(&drbg);
            mbedtls_entropy_free(&entropy);
        }
#endif
    }

    // Write / read some bytes (through TLS if it's on). >0 done, 0 try later, -1 closed or broken.
    int write(const char* data, size_t n);
    int read(char* data, size_t n);
    // Set up TLS and do the handshake. false + error on failure.
    bool startTls(const std::string& hostName, std::string& error, Clock::time_point deadline);
    // Blocking helpers for the start of a connection.
    bool writeAll(const std::string& data, Clock::time_point deadline);
    bool readHeaders(std::string& headers, Clock::time_point deadline);   // up to the blank line
};

#ifdef GB_WEBSOCKETS
namespace {

int bioSend(void* ctx, const unsigned char* buf, size_t len) {
    auto s = (decltype(socket(0, 0, 0))) * static_cast<intptr_t*>(ctx);
    int n = ::send(s, (const char*)buf, (int)std::min<size_t>(len, 1 << 16), 0);
    if (n >= 0) return n;
    return GB_WOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}
int bioRecv(void* ctx, unsigned char* buf, size_t len) {
    auto s = (decltype(socket(0, 0, 0))) * static_cast<intptr_t*>(ctx);
    int n = ::recv(s, (char*)buf, (int)std::min<size_t>(len, 1 << 16), 0);
    if (n >= 0) return n;
    return GB_WOULDBLOCK ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
}

// The trusted certificate authorities (built in; see assets/certs), read once.
mbedtls_x509_crt* trustedCerts(std::string& error) {
    static mbedtls_x509_crt certs;
    static bool ok = false;
    static std::once_flag once;
    std::call_once(once, [] {
        psa_crypto_init();   // TLS 1.3 needs it
        mbedtls_x509_crt_init(&certs);
        ok = mbedtls_x509_crt_parse(&certs, kCaCerts, kCaCertsSize + 1) >= 0;   // (+1: the ending 0 PEM needs)
        // Tests: trust one more certificate (like a local test server's own).
        if (const char* extra = std::getenv("GB_EXTRA_CA"); extra && *extra) mbedtls_x509_crt_parse_file(&certs, extra);
    });
    if (!ok) error = "Couldn't load the list of trusted certificates.";
    return ok ? &certs : nullptr;
}

std::string tlsError(int code) {
    char buf[160], hex[16];
    mbedtls_strerror(code, buf, sizeof(buf));
    std::snprintf(hex, sizeof(hex), " [-0x%04X]", (unsigned)-code);
    return std::string(buf) + hex;
}

} // namespace
#endif

int Connection::Web::write(const char* data, size_t n) {
#ifdef GB_WEBSOCKETS
    if (tls) {
        std::lock_guard<std::mutex> lock(tlsMutex());
        int r = mbedtls_ssl_write(&ssl, (const unsigned char*)data, n);
        if (r >= 0) return r;
        return r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ? 0 : -1;
    }
#endif
    auto s = (decltype(socket(0, 0, 0)))sock;
    int r = ::send(s, data, (int)std::min<size_t>(n, 1 << 16), 0);
    if (r >= 0) return r;
    return GB_WOULDBLOCK ? 0 : -1;
}

int Connection::Web::read(char* data, size_t n) {
#ifdef GB_WEBSOCKETS
    if (tls) {
        std::lock_guard<std::mutex> lock(tlsMutex());
        int r = mbedtls_ssl_read(&ssl, (unsigned char*)data, n);
        if (r > 0) return r;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
        if (r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) return 0;   // TLS 1.3 housekeeping, not data
#endif
        return -1;   // 0 or PEER_CLOSE_NOTIFY: closed; anything else: broken
    }
#endif
    auto s = (decltype(socket(0, 0, 0)))sock;
    int r = ::recv(s, data, (int)n, 0);
    if (r > 0) return r;
    if (r < 0 && GB_WOULDBLOCK) return 0;
    return -1;
}

bool Connection::Web::startTls(const std::string& hostName, std::string& error, Clock::time_point deadline) {
#ifdef GB_WEBSOCKETS
    mbedtls_x509_crt* certs = trustedCerts(error);
    if (!certs) return false;
    std::unique_lock<std::mutex> lock(tlsMutex());
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    sslReady = true;
    const char* who = "gutsandbolts";
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)who, std::strlen(who)) != 0 ||
        mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        error = "Couldn't start a secure connection.";
        return false;
    }
    // TLS 1.2: every server speaks it, and mbedTLS's TLS 1.3 client failed
    // against Cloudflare on some computers ("SSL - Internal error").
    mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, certs, nullptr);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    if (mbedtls_ssl_setup(&ssl, &conf) != 0 || mbedtls_ssl_set_hostname(&ssl, hostName.c_str()) != 0) {
        error = "Couldn't start a secure connection.";
        return false;
    }
    mbedtls_ssl_set_bio(&ssl, &sock, bioSend, bioRecv, nullptr);
    tls = true;
    for (;;) {
        int r = mbedtls_ssl_handshake(&ssl);
        if (r == 0) return true;
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            lock.unlock();   // let other connections go while this one waits
            bool ready = waitFor(sock, r == MBEDTLS_ERR_SSL_WANT_WRITE, deadline);
            lock.lock();
            if (!ready) { error = hostName + " didn't answer in time."; return false; }
            continue;
        }
        uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
        if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags) {
            char why[256];
            mbedtls_x509_crt_verify_info(why, sizeof(why), "", flags);
            std::string reason = why;
            while (!reason.empty() && (reason.back() == '\n' || reason.back() == ' ')) reason.pop_back();
            error = "Couldn't make sure " + hostName + " is really that server (" + reason + ").";
        } else {
            error = "Couldn't make a secure connection to " + hostName + " (" + tlsError(r) + ").";
        }
        return false;
    }
#else
    (void)deadline;
    error = "This app can't connect to " + hostName + " (no secure connections in this build).";
    return false;
#endif
}

bool Connection::Web::writeAll(const std::string& data, Clock::time_point deadline) {
    size_t at = 0;
    while (at < data.size()) {
        int n = write(data.data() + at, data.size() - at);
        if (n < 0) return false;
        if (n == 0 && !waitFor(sock, true, deadline)) return false;
        at += (size_t)n;
    }
    return true;
}

bool Connection::Web::readHeaders(std::string& headers, Clock::time_point deadline) {
    char buf[4096];
    for (;;) {
        size_t end = in.find("\r\n\r\n");
        if (end != std::string::npos) {
            headers = in.substr(0, end + 4);
            in.erase(0, end + 4);
            return true;
        }
        if (in.size() > 65536) return false;
        int n = read(buf, sizeof(buf));
        if (n < 0) return false;
        if (n == 0) { if (!waitFor(sock, false, deadline)) return false; continue; }
        in.append(buf, (size_t)n);
    }
}

namespace {

std::string base64(const unsigned char* p, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += i + 1 < n ? t[v >> 6 & 63] : '=';
        out += i + 2 < n ? t[v & 63] : '=';
    }
    return out;
}

int statusCode(const std::string& headers) {
    size_t sp = headers.find(' ');
    return sp == std::string::npos ? 0 : std::atoi(headers.c_str() + sp + 1);
}

std::string lowerCase(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// One WebSocket frame, as the client sends it (masked).
std::string wsFrame(int opcode, const std::string& payload, std::mt19937& rng) {
    std::string f;
    f += (char)(0x80 | opcode);
    size_t n = payload.size();
    if (n < 126) {
        f += (char)(0x80 | n);
    } else if (n < 65536) {
        f += (char)(0x80 | 126);
        f += (char)(n >> 8); f += (char)n;
    } else {
        f += (char)(0x80 | 127);
        for (int i = 7; i >= 0; --i) f += (char)((uint64_t)n >> (i * 8));
    }
    unsigned char mask[4];
    for (unsigned char& m : mask) m = (unsigned char)(rng() & 0xFF);
    f.append((const char*)mask, 4);
    size_t at = f.size();
    f += payload;
    for (size_t i = 0; i < n; ++i) f[at + i] = (char)(f[at + i] ^ mask[i & 3]);
    return f;
}

} // namespace

// ---------------------------------------------------------------------------

Connection::Connection(intptr_t sock, std::string address)
    : m_sock(sock), m_address(std::move(address)) {
    if (m_sock >= 0) setNonBlocking(m_sock);
}

Connection::~Connection() {
    m_web.reset();   // (TLS first: it still points at the socket)
    if (m_sock >= 0) GB_CLOSE((decltype(socket(0, 0, 0)))m_sock);
}

std::unique_ptr<Connection> Connection::connectTo(const std::string& host, int port,
                                                  std::string& error, int timeoutMs) {
    if (!isWebAddress(host)) {
        intptr_t s = tcpConnect(host, port, error, timeoutMs);
        if (s < 0) return nullptr;
        return std::make_unique<Connection>(s, host + ":" + std::to_string(port));
    }
    // A server on the web: a WebSocket to its /ws (through TLS for wss://).
    bool tls = false;
    std::string name = webHostName(host, tls);
#ifdef __EMSCRIPTEN__
    // The browser's WebSocket. It opens in the background; send() waits for it.
    // The main server is the website the Player was loaded from (so a test copy talks to itself).
    (void)timeoutMs;
    std::string url = (tls ? "wss://" : "ws://") + name + ((tls && port == 443) || (!tls && port == 80) ? "" : ":" + std::to_string(port)) + "/ws";
    if (name == "gutsandbolts.net")
        url = emscripten_run_script_string("(location.protocol == 'https:' ? 'wss://' : 'ws://') + location.host + '/ws'");
    if (!emscripten_websocket_is_supported()) { error = "This browser can't make WebSocket connections."; return nullptr; }
    EmscriptenWebSocketCreateAttributes attr;
    emscripten_websocket_init_create_attributes(&attr);
    attr.url = url.c_str();
    EMSCRIPTEN_WEBSOCKET_T ws = emscripten_websocket_new(&attr);
    if (ws <= 0) { error = "Couldn't connect to " + url + "."; return nullptr; }
    auto c = std::make_unique<Connection>(-1, host + ":" + std::to_string(port));
    c->m_web = std::make_unique<Web>();
    Web* w = c->m_web.get();
    w->ws = ws;
    // (named first: these set_..._callback calls are macros, so no commas inside them)
    em_websocket_open_callback_func opened = [](int, const EmscriptenWebSocketOpenEvent*, void* u) -> EM_BOOL {
        static_cast<Web*>(u)->open = true;
        return EM_TRUE;
    };
    em_websocket_message_callback_func got = [](int, const EmscriptenWebSocketMessageEvent* e, void* u) -> EM_BOOL {
        Web& web = *static_cast<Web*>(u);
        uint32_t n = e->numBytes - (e->isText && e->numBytes ? 1 : 0);   // (text arrives with an ending 0)
        unsigned char hdr[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
        web.arrived.append((const char*)hdr, 4);
        web.arrived.append((const char*)e->data, n);
        return EM_TRUE;
    };
    em_websocket_error_callback_func broke = [](int, const EmscriptenWebSocketErrorEvent*, void* u) -> EM_BOOL {
        static_cast<Web*>(u)->closed = true;
        return EM_TRUE;
    };
    em_websocket_close_callback_func ended = [](int, const EmscriptenWebSocketCloseEvent*, void* u) -> EM_BOOL {
        static_cast<Web*>(u)->closed = true;
        return EM_TRUE;
    };
    emscripten_websocket_set_onopen_callback(ws, w, opened);
    emscripten_websocket_set_onmessage_callback(ws, w, got);
    emscripten_websocket_set_onerror_callback(ws, w, broke);
    emscripten_websocket_set_onclose_callback(ws, w, ended);
    return c;
#else
    auto deadline = Clock::now() + std::chrono::milliseconds(std::max(timeoutMs, 8000));
    intptr_t s = tcpConnect(name, port, error, timeoutMs);
    if (s < 0) return nullptr;
    auto c = std::make_unique<Connection>(s, host + ":" + std::to_string(port));
    c->m_web = std::make_unique<Web>();
    Web& w = *c->m_web;
    w.sock = s;
    if (tls && !w.startTls(name, error, deadline)) return nullptr;
    unsigned char key[16];
    for (unsigned char& k : key) k = (unsigned char)(w.rng() & 0xFF);
    std::string hostHeader = name + ((tls && port == 443) || (!tls && port == 80) ? "" : ":" + std::to_string(port));
    std::string hello = "GET /ws HTTP/1.1\r\nHost: " + hostHeader + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Key: " + base64(key, 16) + "\r\nSec-WebSocket-Version: 13\r\n"
                        "User-Agent: GutsAndBolts\r\n\r\n";
    std::string headers;
    if (!w.writeAll(hello, deadline) || !w.readHeaders(headers, deadline)) {
        error = "The server at " + name + " didn't answer.";
        return nullptr;
    }
    if (statusCode(headers) != 101) {
        error = "The server at " + name + " isn't a Guts&Bolts server (it answered " + std::to_string(statusCode(headers)) + ").";
        return nullptr;
    }
    return c;
#endif
}


bool httpPost(const std::string& host, int port, const std::string& path, const std::string& body,
              std::string& reply, std::string& error, int timeoutMs, const std::string& extraHeaders, int* statusOut) {
    bool tls = false;
    std::string name = isWebAddress(host) ? webHostName(host, tls) : host;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    intptr_t s = tcpConnect(name, port, error, std::min(timeoutMs, 8000));
    if (s < 0) return false;
    Connection c(s, name);   // (closes the socket when done)
    c.m_web = std::make_unique<Connection::Web>();
    Connection::Web& w = *c.m_web;
    w.sock = s;
    if (tls && !w.startTls(name, error, deadline)) return false;
    std::string hostHeader = name + ((tls && port == 443) || (!tls && port == 80) ? "" : ":" + std::to_string(port));
    std::string request = "POST " + path + " HTTP/1.1\r\nHost: " + hostHeader + "\r\nUser-Agent: GutsAndBolts\r\n"
                          "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                          "\r\n" + extraHeaders + "Connection: close\r\n\r\n";
    std::string headers;
    if (!w.writeAll(request, deadline) || !w.writeAll(body, deadline) || !w.readHeaders(headers, deadline)) {
        error = "The server at " + name + " didn't answer in time.";
        return false;
    }
    // The body: all of it, until the server closes (we asked it to).
    std::string all = std::move(w.in);
    char buf[65536];
    const std::string low = lowerCase(headers);
    size_t lenAt = low.find("\ncontent-length:");
    long long want = lenAt == std::string::npos ? -1 : std::atoll(low.c_str() + lenAt + 16);
    const bool chunked = low.find("\ntransfer-encoding: chunked") != std::string::npos;
    // Chunked answers end with an empty chunk (the server may keep the line open after it).
    auto chunkedDone = [&]() {
        size_t at = 0;
        for (;;) {
            size_t eol = all.find("\r\n", at);
            if (eol == std::string::npos) return false;
            size_t n = std::strtoul(all.c_str() + at, nullptr, 16);
            if (n == 0) return all.find("\r\n", eol + 2) != std::string::npos;
            at = eol + 2 + n + 2;
            if (at > all.size()) return false;
        }
    };
    for (;;) {
        if (want >= 0 && (long long)all.size() >= want) break;
        if (chunked && chunkedDone()) break;
        if (all.size() > kMaxMessage) { error = "The answer was too big."; return false; }
        int n = w.read(buf, sizeof(buf));
        if (n > 0) { all.append(buf, (size_t)n); continue; }
        if (n < 0) break;   // closed: that's everything
        if (!waitFor(s, false, deadline)) { error = "The server at " + name + " didn't finish answering in time."; return false; }
    }
    if (chunked) {
        std::string out;
        size_t at = 0;
        for (;;) {
            size_t eol = all.find("\r\n", at);
            if (eol == std::string::npos) break;
            size_t n = std::strtoul(all.c_str() + at, nullptr, 16);
            if (n == 0) break;
            out.append(all, eol + 2, n);
            at = eol + 2 + n + 2;
        }
        all.swap(out);
    } else if (want >= 0 && (long long)all.size() > want) {
        all.resize((size_t)want);
    }
    int status = statusCode(headers);
    if (statusOut) *statusOut = status;
    if (status != 200 && all.empty()) {
        error = "The server at " + name + " answered " + std::to_string(status) + ".";
        return false;
    }
    reply = std::move(all);
    return true;
}

void Connection::send(const std::string& message) {
    if (!m_alive) return;
#ifdef __EMSCRIPTEN__
    if (m_web) { m_web->outbox.push_back(message); return; }
#endif
    if (m_web) { m_out += wsFrame(0x2, message, m_web->rng); return; }   // one binary WebSocket message
    uint32_t n = (uint32_t)message.size();
    unsigned char hdr[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16),
                            (unsigned char)(n >> 8), (unsigned char)n};
    m_out.append((const char*)hdr, 4);
    m_out += message;
}

bool Connection::poll() {
    if (!m_alive) return false;
    if (m_web) return webPoll();
    auto s = (decltype(socket(0, 0, 0)))m_sock;

    // Send as much as the network will take right now.
    while (!m_out.empty()) {
        int sent = ::send(s, m_out.data(), (int)std::min<size_t>(m_out.size(), 1 << 16), 0);
        if (sent > 0) { m_out.erase(0, (size_t)sent); continue; }
        if (sent < 0 && GB_WOULDBLOCK) break;
        m_alive = false;
        return false;
    }

    // Read everything that has arrived.
    char buf[65536];
    for (;;) {
        int got = ::recv(s, buf, sizeof(buf), 0);
        if (got > 0) { m_in.append(buf, (size_t)got); continue; }
        if (got < 0 && GB_WOULDBLOCK) break;
        m_alive = false;   // 0 = closed by the other side, <0 = error
        return false;
    }
    return true;
}

// WebSocket version of poll(): send what's queued, read what arrived, and
// turn whole WebSocket messages into the same length-prefixed messages as TCP.
bool Connection::webPoll() {
    Web& w = *m_web;
#ifdef __EMSCRIPTEN__
    if (w.open) {
        for (std::string& m : w.outbox)
            if (emscripten_websocket_send_binary(w.ws, m.data(), (uint32_t)m.size()) != EMSCRIPTEN_RESULT_SUCCESS) w.closed = true;
        w.outbox.clear();
    }
    m_in += w.arrived;
    w.arrived.clear();
    if (w.closed) { m_alive = false; return false; }
    return true;
#endif
    while (!m_out.empty()) {
        int n = w.write(m_out.data(), m_out.size());
        if (n < 0) { m_alive = false; return false; }
        if (n == 0) break;
        m_out.erase(0, (size_t)n);
    }
    char buf[65536];
    bool closed = false;
    for (;;) {
        int n = w.read(buf, sizeof(buf));
        if (n > 0) { w.in.append(buf, (size_t)n); continue; }
        if (n < 0) closed = true;
        break;
    }
    // Whole frames.
    for (;;) {
        const unsigned char* p = (const unsigned char*)w.in.data();
        size_t have = w.in.size();
        if (have < 2) break;
        bool fin = p[0] & 0x80, masked = p[1] & 0x80;
        int op = p[0] & 0x0F;
        uint64_t len = p[1] & 0x7F;
        size_t at = 2;
        if (len == 126) { if (have < 4) break; len = (uint64_t)p[2] << 8 | p[3]; at = 4; }
        else if (len == 127) {
            if (have < 10) break;
            len = 0;
            for (int i = 0; i < 8; ++i) len = len << 8 | p[2 + i];
            at = 10;
        }
        if (len > kMaxMessage) { m_alive = false; return false; }
        size_t maskAt = at;
        if (masked) at += 4;
        if (have < at + len) break;
        std::string payload = w.in.substr(at, (size_t)len);
        if (masked) for (size_t i = 0; i < payload.size(); ++i) payload[i] = (char)(payload[i] ^ p[maskAt + (i & 3)]);
        w.in.erase(0, at + (size_t)len);
        if (op == 0x8) {   // the server is closing: say goodbye back
            std::string bye = wsFrame(0x8, "", w.rng);
            w.write(bye.data(), bye.size());
            closed = true;
            break;
        }
        if (op == 0x9) { m_out += wsFrame(0xA, payload, w.rng); continue; }   // ping -> pong
        if (op == 0xA) continue;
        if (op == 0x1 || op == 0x2) w.partial = payload;
        else if (op == 0x0) w.partial += payload;
        else continue;
        if (!fin) continue;
        uint32_t n = (uint32_t)w.partial.size();
        unsigned char hdr[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
        m_in.append((const char*)hdr, 4);
        m_in += w.partial;
        w.partial.clear();
    }
    if (closed) { m_alive = false; return false; }
    return true;
}

bool Connection::pop(std::string& message) {
    if (m_in.size() < 4) return false;
    const unsigned char* p = (const unsigned char*)m_in.data();
    uint32_t n = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    if (n > kMaxMessage) { m_alive = false; return false; }
    if (m_in.size() < 4 + (size_t)n) return false;
    message.assign(m_in, 4, n);
    m_in.erase(0, 4 + (size_t)n);
    return true;
}

// ---------------------------------------------------------------------------

Listener::~Listener() { close(); }

bool Listener::open(int port, std::string& error) {
    close();
    if (!initSockets()) { error = "Networking isn't available on this computer."; return false; }
    auto s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ((intptr_t)s < 0) { error = "Couldn't create a network socket."; return false; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 16) != 0) {
        GB_CLOSE(s);
        error = "Port " + std::to_string(port) + " is already in use (is another game hosted?).";
        return false;
    }
    setNonBlocking((intptr_t)s);
    m_sock = (intptr_t)s;
    return true;
}

void Listener::close() {
    if (m_sock >= 0) GB_CLOSE((decltype(socket(0, 0, 0)))m_sock);
    m_sock = -1;
}

std::unique_ptr<Connection> Listener::accept() {
    if (m_sock < 0) return nullptr;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    auto c = ::accept((decltype(socket(0, 0, 0)))m_sock, (sockaddr*)&addr, &len);
    if ((intptr_t)c < 0) return nullptr;
    char ip[64] = "?";
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::make_unique<Connection>((intptr_t)c, ip);
}

std::string localAddresses() {
    std::string out;
#ifdef _WIN32
    initSockets();
    char name[256];
    if (gethostname(name, sizeof(name)) != 0) return "";
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &res) != 0) return "";
    for (addrinfo* a = res; a; a = a->ai_next) {
        char ip[64];
        inet_ntop(AF_INET, &((sockaddr_in*)a->ai_addr)->sin_addr, ip, sizeof(ip));
        if (std::strncmp(ip, "127.", 4) == 0) continue;
        if (!out.empty()) out += ", ";
        out += ip;
    }
    freeaddrinfo(res);
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return "";
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        char ip[64];
        inet_ntop(AF_INET, &((sockaddr_in*)a->ifa_addr)->sin_addr, ip, sizeof(ip));
        if (std::strncmp(ip, "127.", 4) == 0) continue;
        if (!out.empty()) out += ", ";
        out += ip;
    }
    freeifaddrs(list);
#endif
    return out;
}

// ---------------------------------------------------------------------------
// HttpServer (this computer only)
// ---------------------------------------------------------------------------

HttpServer::~HttpServer() { close(); }

bool HttpServer::open(int port, std::string& error) {
    close();
    if (!initSockets()) { error = "Networking isn't available on this computer."; return false; }
    auto s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if ((intptr_t)s < 0) { error = "Couldn't create a network socket."; return false; }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1 only
    addr.sin_port = htons((unsigned short)port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 8) != 0) {
        GB_CLOSE(s);
        error = "Port " + std::to_string(port) + " is already in use (is another Studio open?).";
        return false;
    }
    setNonBlocking((intptr_t)s);
    m_sock = (intptr_t)s;
    return true;
}

void HttpServer::close() {
    for (auto& c : m_clients) GB_CLOSE((decltype(socket(0, 0, 0)))c.sock);
    m_clients.clear();
    if (m_sock >= 0) GB_CLOSE((decltype(socket(0, 0, 0)))m_sock);
    m_sock = -1;
}

void HttpServer::poll() {
    if (m_sock < 0) return;
    const double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
    for (;;) {
        auto c = ::accept((decltype(socket(0, 0, 0)))m_sock, nullptr, nullptr);
        if ((intptr_t)c < 0) break;
        setNonBlocking((intptr_t)c);
        m_clients.push_back({m_nextId++, (intptr_t)c, {}, {}, false, false, now});
    }
    char buf[16384];
    for (size_t i = 0; i < m_clients.size();) {
        Client& c = m_clients[i];
        bool dead = false;
        if (!c.answered) {
            for (;;) {
                auto n = recv((decltype(socket(0, 0, 0)))c.sock, buf, sizeof(buf), 0);
                if (n > 0) { c.in.append(buf, (size_t)n); if (c.in.size() > 32u * 1024u * 1024u) { dead = true; break; } continue; }
                if (n == 0 || !GB_WOULDBLOCK) dead = !c.handed;   // closed before asking anything
                break;
            }
        } else if (!c.out.empty()) {
            auto n = ::send((decltype(socket(0, 0, 0)))c.sock, c.out.data(), (int)c.out.size(), 0);
            if (n > 0) c.out.erase(0, (size_t)n);
            else if (!GB_WOULDBLOCK) dead = true;
        }
        if (c.answered && c.out.empty()) dead = true;       // sent everything: done
        if (now - c.since > 120.0) dead = true;             // stuck
        if (dead) { GB_CLOSE((decltype(socket(0, 0, 0)))c.sock); m_clients.erase(m_clients.begin() + (long)i); continue; }
        ++i;
    }
}

bool HttpServer::next(Request& out) {
    for (auto& c : m_clients) {
        if (c.handed || c.answered) continue;
        size_t end = c.in.find("\r\n\r\n");
        if (end == std::string::npos) continue;
        std::string head = lowerCase(c.in.substr(0, end + 2));
        size_t lenAt = head.find("\ncontent-length:");
        size_t len = lenAt == std::string::npos ? 0 : (size_t)std::atoll(head.c_str() + lenAt + 16);
        if (c.in.size() < end + 4 + len) continue;           // body still arriving
        std::string first = c.in.substr(0, c.in.find("\r\n"));
        size_t sp1 = first.find(' '), sp2 = first.find(' ', sp1 + 1);
        out.id = c.id;
        out.method = first.substr(0, sp1);
        out.path = sp1 == std::string::npos ? "/" : first.substr(sp1 + 1, sp2 - sp1 - 1);
        out.headers = head;
        out.body = c.in.substr(end + 4, len);
        c.handed = true;
        return true;
    }
    return false;
}

void HttpServer::respond(int id, int status, const std::string& contentType, const std::string& body) {
    for (auto& c : m_clients) {
        if (c.id != id || c.answered) continue;
        const char* text = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 400 ? "Bad Request"
                         : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : "Error";
        c.out = "HTTP/1.1 " + std::to_string(status) + " " + text + "\r\n";
        if (!contentType.empty()) c.out += "Content-Type: " + contentType + "\r\n";
        c.out += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        c.answered = true;
        return;
    }
}

} // namespace Net
