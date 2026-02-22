#include "network.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET net_socket_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
#define NET_SOCK_ERR SOCKET_ERROR
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int net_socket_t;
#define NET_INVALID_SOCKET (-1)
#define NET_SOCK_ERR (-1)
#endif

#ifdef _WIN32
typedef int socklen_t;
#endif

#define CONNECT_TIMEOUT_MS 3000
#define HANDSHAKE_TIMEOUT_MS 3000
#define IO_TIMEOUT_MS 700
#define HTTP_TIMEOUT_MS 2500

/* External public HTTP endpoints used only to detect internet/public IP. */
#define PUBLIC_IP_HOST_PRIMARY "checkip.amazonaws.com"
#define PUBLIC_IP_HOST_FALLBACK "api.ipify.org"

/* Tracks process-level network runtime init (WSAStartup on Windows). */
static int g_network_runtime_refcount = 0;
static char g_last_error[256] = "No error.";

/* Stores latest network-layer error for UI-facing diagnostics. */
static void set_last_error(const char* fmt, ...) {
    va_list args;

    if (fmt == NULL) {
        g_last_error[0] = '\0';
        return;
    }

    va_start(args, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, args);
    va_end(args);
    g_last_error[sizeof(g_last_error) - 1] = '\0';
}

/* Returns textual description for latest network failure. */
const char* network_last_error(void) {
    return g_last_error;
}

/* Initializes platform networking runtime. */
static bool network_runtime_init(void) {
#ifdef _WIN32
    if (g_network_runtime_refcount == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            set_last_error("WSAStartup failed.");
            return false;
        }
    }
#endif

    g_network_runtime_refcount++;
    return true;
}

/* Shuts down platform networking runtime. */
static void network_runtime_shutdown(void) {
    if (g_network_runtime_refcount <= 0) {
        g_network_runtime_refcount = 0;
        return;
    }

    g_network_runtime_refcount--;

#ifdef _WIN32
    if (g_network_runtime_refcount == 0) {
        WSACleanup();
    }
#endif
}

/* Closes a socket handle on the current platform. */
static void socket_close(net_socket_t socket_fd) {
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

/* Returns whether latest socket error means operation would block. */
static bool socket_error_would_block(void) {
#ifdef _WIN32
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEALREADY;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EALREADY;
#endif
}

/* Enables or disables non-blocking mode on a socket. */
static bool socket_set_nonblocking(net_socket_t socket_fd, bool enabled) {
#ifdef _WIN32
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(socket_fd, F_SETFL, flags) == 0;
#endif
}

/* Waits for socket writability/readability with timeout. */
static bool socket_wait(net_socket_t socket_fd, bool write_mode, int timeout_ms) {
    fd_set fds;
    struct timeval tv;
    int rc;

    FD_ZERO(&fds);
    FD_SET(socket_fd, &fds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    rc = select((int)socket_fd + 1,
                write_mode ? NULL : &fds,
                write_mode ? &fds : NULL,
                NULL,
                &tv);
    return rc > 0;
}

/* Sends exact bytes on blocking socket with timeout guard. */
static bool socket_send_all(net_socket_t socket_fd, const void* data, int bytes, int timeout_ms);

/* Receives exact bytes on blocking socket with timeout guard. */
static bool socket_recv_all(net_socket_t socket_fd, void* data, int bytes, int timeout_ms);

/* Connects TCP socket to one host:port endpoint with explicit timeout. */
static bool tcp_connect_endpoint(net_socket_t* out_socket, const char* host, uint16_t port) {
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    struct addrinfo* it;
    char port_text[16];

    if (out_socket == NULL || host == NULL || host[0] == '\0') {
        return false;
    }

    *out_socket = NET_INVALID_SOCKET;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port_text, &hints, &result) != 0 || result == NULL) {
        set_last_error("Could not resolve endpoint host: %s", host);
        return false;
    }

    for (it = result; it != NULL; it = it->ai_next) {
        net_socket_t socket_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        bool connected = false;

        if (socket_fd == NET_INVALID_SOCKET) {
            continue;
        }

        if (!socket_set_nonblocking(socket_fd, true)) {
            socket_close(socket_fd);
            continue;
        }

        if (connect(socket_fd, it->ai_addr, (socklen_t)it->ai_addrlen) == 0) {
            connected = true;
        } else if (socket_error_would_block() && socket_wait(socket_fd, true, CONNECT_TIMEOUT_MS)) {
            int so_error = 0;
            socklen_t so_len = (socklen_t)sizeof(so_error);
            if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, (char*)&so_error, &so_len) == 0 && so_error == 0) {
                connected = true;
            }
        }

        if (connected) {
            if (!socket_set_nonblocking(socket_fd, false)) {
                socket_close(socket_fd);
                continue;
            }
            *out_socket = socket_fd;
            freeaddrinfo(result);
            return true;
        }

        socket_close(socket_fd);
    }

    freeaddrinfo(result);
    set_last_error("Could not connect to online service.");
    return false;
}

/* Creates one non-blocking listen socket on requested port (0 = OS-allocated). */
static bool create_listen_socket(net_socket_t* out_socket, uint16_t requested_port, uint16_t* out_bound_port) {
    net_socket_t listen_socket;
    struct sockaddr_in addr;
    struct sockaddr_in bound;
    socklen_t bound_len = (socklen_t)sizeof(bound);

    if (out_socket == NULL || out_bound_port == NULL) {
        return false;
    }

    *out_socket = NET_INVALID_SOCKET;
    *out_bound_port = 0;

    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == NET_INVALID_SOCKET) {
        return false;
    }

#ifdef _WIN32
    {
        BOOL exclusive = TRUE;
        setsockopt(listen_socket,
                   SOL_SOCKET,
                   SO_EXCLUSIVEADDRUSE,
                   (const char*)&exclusive,
                   (socklen_t)sizeof(exclusive));
    }
#else
    {
        int opt = 1;
        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, (socklen_t)sizeof(opt));
    }
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(requested_port);

    if (bind(listen_socket, (const struct sockaddr*)&addr, (socklen_t)sizeof(addr)) == NET_SOCK_ERR) {
        socket_close(listen_socket);
        return false;
    }

    if (listen(listen_socket, 8) == NET_SOCK_ERR) {
        socket_close(listen_socket);
        return false;
    }

    if (!socket_set_nonblocking(listen_socket, true)) {
        socket_close(listen_socket);
        return false;
    }

    if (getsockname(listen_socket, (struct sockaddr*)&bound, &bound_len) != 0) {
        socket_close(listen_socket);
        return false;
    }

    *out_socket = listen_socket;
    *out_bound_port = ntohs(bound.sin_port);
    return true;
}

/* Extracts first valid IPv4 token from arbitrary response text. */
static bool parse_first_ipv4(const char* text, uint32_t* out_ipv4_be) {
    char token[32];
    int token_len = 0;

    if (text == NULL || out_ipv4_be == NULL) {
        return false;
    }

    for (int i = 0;; ++i) {
        char ch = text[i];
        bool is_part = ((ch >= '0' && ch <= '9') || ch == '.');

        if (is_part) {
            if (token_len < (int)sizeof(token) - 1) {
                token[token_len++] = ch;
            }
        } else {
            if (token_len >= 7 && token_len <= 15) {
                struct in_addr addr;
                token[token_len] = '\0';
                if (inet_pton(AF_INET, token, &addr) == 1) {
                    *out_ipv4_be = addr.s_addr;
                    return true;
                }
            }
            token_len = 0;
        }

        if (ch == '\0') {
            break;
        }
    }

    return false;
}

/* Uses plain HTTP endpoint to discover public IPv4 for invite-code generation. */
static bool fetch_public_ipv4_from_host(const char* host, uint32_t* out_ipv4_be) {
    net_socket_t socket_fd = NET_INVALID_SOCKET;
    char request[256];
    char response[1024];
    int response_len = 0;
    bool ok = false;

    if (host == NULL || out_ipv4_be == NULL) {
        return false;
    }

    if (!tcp_connect_endpoint(&socket_fd, host, 80)) {
        return false;
    }

    snprintf(request,
             sizeof(request),
             "GET / HTTP/1.1\r\nHost: %s\r\nUser-Agent: chess-app\r\nConnection: close\r\n\r\n",
             host);

    if (!socket_send_all(socket_fd, request, (int)strlen(request), HTTP_TIMEOUT_MS)) {
        socket_close(socket_fd);
        return false;
    }

    while (response_len < (int)sizeof(response) - 1) {
        int rc;
        int cap = (int)sizeof(response) - 1 - response_len;

        if (!socket_wait(socket_fd, false, HTTP_TIMEOUT_MS)) {
            break;
        }

        rc = recv(socket_fd, response + response_len, cap, 0);
        if (rc > 0) {
            response_len += rc;
            continue;
        }
        break;
    }

    response[response_len] = '\0';
    ok = parse_first_ipv4(response, out_ipv4_be);
    socket_close(socket_fd);
    return ok;
}

/* Tries multiple public endpoints to detect host public IPv4. */
static bool fetch_public_ipv4(uint32_t* out_ipv4_be) {
    if (out_ipv4_be == NULL) {
        return false;
    }

    if (fetch_public_ipv4_from_host(PUBLIC_IP_HOST_PRIMARY, out_ipv4_be)) {
        return true;
    }
    if (fetch_public_ipv4_from_host(PUBLIC_IP_HOST_FALLBACK, out_ipv4_be)) {
        return true;
    }

    set_last_error("Could not detect public IP. Check internet connection.");
    return false;
}

/* Closes active peer socket and resets socket-read buffer state. */
static void close_active_socket(NetworkClient* client) {
    net_socket_t socket_fd;

    if (client == NULL) {
        return;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    if (socket_fd != NET_INVALID_SOCKET) {
        socket_close(socket_fd);
    }

    client->socket_handle = (intptr_t)NET_INVALID_SOCKET;
    client->rx_bytes = 0;
}

/* Closes host-side listen socket. */
static void close_listen_socket(NetworkClient* client) {
    net_socket_t listen_fd;

    if (client == NULL) {
        return;
    }

    listen_fd = (net_socket_t)client->listen_socket_handle;
    if (listen_fd != NET_INVALID_SOCKET) {
        socket_close(listen_fd);
    }

    client->listen_socket_handle = (intptr_t)NET_INVALID_SOCKET;
}

/* Handles peer disconnection while preserving any local listener for reconnects. */
static void on_peer_socket_lost(NetworkClient* client, const char* message) {
    if (client == NULL) {
        return;
    }

    close_active_socket(client);
    client->connected = false;
    client->has_pending_packet = false;

    if ((net_socket_t)client->listen_socket_handle != NET_INVALID_SOCKET) {
        client->relay_connected = true;
    } else {
        client->relay_connected = false;
    }

    if (message != NULL && message[0] != '\0') {
        set_last_error("%s", message);
    }
}

/* Accepts one pending inbound peer connection if any (non-blocking). */
static void accept_pending_peer(NetworkClient* client) {
    net_socket_t listen_fd;
    struct sockaddr_storage addr;
    socklen_t addr_len = (socklen_t)sizeof(addr);
    net_socket_t accepted;

    if (client == NULL) {
        return;
    }

    listen_fd = (net_socket_t)client->listen_socket_handle;
    if (listen_fd == NET_INVALID_SOCKET) {
        return;
    }

    accepted = accept(listen_fd, (struct sockaddr*)&addr, &addr_len);
    if (accepted == NET_INVALID_SOCKET) {
        return;
    }

    if (!socket_set_nonblocking(accepted, true)) {
        socket_close(accepted);
        return;
    }

    if ((net_socket_t)client->socket_handle != NET_INVALID_SOCKET) {
        socket_close(accepted);
        return;
    }

    client->socket_handle = (intptr_t)accepted;
    client->rx_bytes = 0;
}

/* Sends all bytes with timeout over blocking socket. */
static bool socket_send_all(net_socket_t socket_fd, const void* data, int bytes, int timeout_ms) {
    const char* cursor = (const char*)data;
    int sent = 0;

    while (sent < bytes) {
        int rc;

        if (!socket_wait(socket_fd, true, timeout_ms)) {
            set_last_error("Send timeout.");
            return false;
        }

        rc = send(socket_fd, cursor + sent, bytes - sent, 0);
        if (rc <= 0) {
            set_last_error("Failed to send packet.");
            return false;
        }

        sent += rc;
    }

    return true;
}

/* Receives exact bytes with timeout over blocking socket. */
static bool socket_recv_all(net_socket_t socket_fd, void* data, int bytes, int timeout_ms) {
    char* cursor = (char*)data;
    int received = 0;

    while (received < bytes) {
        int rc;

        if (!socket_wait(socket_fd, false, timeout_ms)) {
            set_last_error("Response timeout.");
            return false;
        }

        rc = recv(socket_fd, cursor + received, bytes - received, 0);
        if (rc <= 0) {
            set_last_error("Connection closed unexpectedly.");
            return false;
        }

        received += rc;
    }

    return true;
}

/* Sends one packet in blocking handshake mode. */
static bool send_packet_blocking(NetworkClient* client, const NetPacket* packet) {
    NetPacket wire = *packet;
    net_socket_t socket_fd;

    if (client == NULL || packet == NULL) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    if (socket_fd == NET_INVALID_SOCKET) {
        return false;
    }
    wire.sequence = htonl(packet->sequence);
    return socket_send_all(socket_fd, &wire, (int)sizeof(wire), HANDSHAKE_TIMEOUT_MS);
}

/* Receives one packet in blocking handshake mode. */
static bool recv_packet_blocking(NetworkClient* client, NetPacket* out_packet) {
    NetPacket wire;
    net_socket_t socket_fd;

    if (client == NULL || out_packet == NULL) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    if (socket_fd == NET_INVALID_SOCKET) {
        return false;
    }
    if (!socket_recv_all(socket_fd, &wire, (int)sizeof(wire), HANDSHAKE_TIMEOUT_MS)) {
        return false;
    }

    wire.sequence = ntohl(wire.sequence);
    *out_packet = wire;
    return true;
}

/* Sends one packet in non-blocking runtime mode. */
static bool send_packet_runtime(NetworkClient* client, const NetPacket* packet) {
    NetPacket wire = *packet;
    net_socket_t socket_fd;
    int sent = 0;
    const char* raw = (const char*)&wire;

    if (client == NULL || packet == NULL) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    if (socket_fd == NET_INVALID_SOCKET) {
        on_peer_socket_lost(client, "Peer socket is not available.");
        return false;
    }
    wire.sequence = htonl(packet->sequence);

    while (sent < (int)sizeof(wire)) {
        int rc = send(socket_fd, raw + sent, (int)sizeof(wire) - sent, 0);
        if (rc > 0) {
            sent += rc;
            continue;
        }

        if (rc == 0) {
            on_peer_socket_lost(client, "Peer disconnected.");
            return false;
        }

        if (socket_error_would_block()) {
            if (!socket_wait(socket_fd, true, IO_TIMEOUT_MS)) {
                set_last_error("Send timed out.");
                return false;
            }
            continue;
        }

        on_peer_socket_lost(client, "Send failed.");
        return false;
    }

    return true;
}

/* Copies local username into outgoing packet for peer-side UI metadata. */
static void packet_set_sender_username(NetworkClient* client, NetPacket* packet) {
    if (client == NULL || packet == NULL || client->local_username[0] == '\0') {
        return;
    }
    strncpy(packet->username, client->local_username, PLAYER_NAME_MAX);
    packet->username[PLAYER_NAME_MAX] = '\0';
}

/* Finalizes handshake and switches socket into non-blocking runtime mode. */
static bool finalize_runtime_socket(NetworkClient* client) {
    net_socket_t socket_fd;

    if (client == NULL || !client->relay_connected) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    if (!socket_set_nonblocking(socket_fd, true)) {
        set_last_error("Failed to switch socket to non-blocking mode.");
        return false;
    }
    return true;
}

/* Pushes one packet as pending synthetic event for next poll call. */
static void queue_pending_packet(NetworkClient* client, const NetPacket* packet) {
    if (client == NULL || packet == NULL) {
        return;
    }
    client->pending_packet = *packet;
    client->has_pending_packet = true;
}

/* Reads one packet from active socket buffer in non-blocking mode. */
static bool pop_socket_packet(NetworkClient* client, NetPacket* out_packet) {
    net_socket_t socket_fd;

    if (client == NULL || out_packet == NULL) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    if (socket_fd == NET_INVALID_SOCKET) {
        return false;
    }

    while (client->rx_bytes < (int)sizeof(NetPacket)) {
        int capacity = (int)sizeof(client->rx_buffer) - client->rx_bytes;
        int rc;

        if (capacity <= 0) {
            on_peer_socket_lost(client, "Input buffer overflow.");
            return false;
        }

        rc = recv(socket_fd, (char*)client->rx_buffer + client->rx_bytes, capacity, 0);
        if (rc > 0) {
            client->rx_bytes += rc;
            continue;
        }

        if (rc == 0) {
            on_peer_socket_lost(client, "Peer disconnected.");
            return false;
        }

        if (socket_error_would_block()) {
            return false;
        }

        on_peer_socket_lost(client, "Receive failed.");
        return false;
    }

    memcpy(out_packet, client->rx_buffer, sizeof(NetPacket));
    memmove(client->rx_buffer,
            client->rx_buffer + sizeof(NetPacket),
            (size_t)(client->rx_bytes - (int)sizeof(NetPacket)));
    client->rx_bytes -= (int)sizeof(NetPacket);
    out_packet->sequence = ntohl(out_packet->sequence);
    return true;
}

/* Initializes direct-network runtime state. */
bool network_client_init(NetworkClient* client, uint16_t listen_port) {
    (void)listen_port;

    if (client == NULL) {
        return false;
    }

    memset(client, 0, sizeof(*client));

    if (!network_runtime_init()) {
        return false;
    }

    client->initialized = true;
    client->socket_handle = (intptr_t)NET_INVALID_SOCKET;
    client->listen_socket_handle = (intptr_t)NET_INVALID_SOCKET;
    client->relay_connected = false;
    return true;
}

/* Releases socket and runtime resources. */
void network_client_shutdown(NetworkClient* client) {
    if (client == NULL || !client->initialized) {
        return;
    }

    close_active_socket(client);
    close_listen_socket(client);

    memset(client, 0, sizeof(*client));
    network_runtime_shutdown();
}

/* Configures this client as direct host and returns invite code (public IPv4 + port). */
bool network_client_host(NetworkClient* client, const char* username, char out_code[INVITE_CODE_LEN + 1]) {
    net_socket_t listen_fd = NET_INVALID_SOCKET;
    uint16_t bound_port = 0;
    uint32_t public_ip_be = 0U;
    char invite_code[INVITE_CODE_LEN + 1];

    if (client == NULL || !client->initialized || username == NULL || username[0] == '\0' || out_code == NULL) {
        set_last_error("Host username is invalid.");
        return false;
    }

    close_active_socket(client);
    close_listen_socket(client);

    if (!create_listen_socket(&listen_fd, 0, &bound_port)) {
        set_last_error("Could not open host listen socket.");
        return false;
    }

    if (!fetch_public_ipv4(&public_ip_be)) {
        socket_close(listen_fd);
        return false;
    }

    if (!matchmaker_encode_endpoint(public_ip_be, bound_port, invite_code)) {
        socket_close(listen_fd);
        set_last_error("Could not generate invite code.");
        return false;
    }

    client->listen_socket_handle = (intptr_t)listen_fd;
    client->socket_handle = (intptr_t)NET_INVALID_SOCKET;
    client->is_host = true;
    client->connected = false;
    client->relay_connected = true;
    client->host_side = (rand() & 1) ? SIDE_WHITE : SIDE_BLACK;
    client->sequence = 0;
    client->rx_bytes = 0;
    client->has_pending_packet = false;

    strncpy(client->local_username, username, PLAYER_NAME_MAX);
    client->local_username[PLAYER_NAME_MAX] = '\0';
    client->peer_username[0] = '\0';
    strncpy(client->invite_code, invite_code, INVITE_CODE_LEN);
    client->invite_code[INVITE_CODE_LEN] = '\0';

    strncpy(out_code, invite_code, INVITE_CODE_LEN);
    out_code[INVITE_CODE_LEN] = '\0';
    set_last_error("No error.");
    return true;
}

/* Reopens host room on the same invite-code port for saved-session reconnect. */
bool network_client_host_reconnect(NetworkClient* client, const char* username, const char* invite_code) {
    uint32_t code_ip_be = 0U;
    uint16_t code_port = 0;
    uint32_t current_public_ip = 0U;
    net_socket_t listen_fd = NET_INVALID_SOCKET;
    uint16_t bound_port = 0;

    if (client == NULL || !client->initialized || username == NULL || username[0] == '\0') {
        set_last_error("Host username is invalid.");
        return false;
    }
    if (!matchmaker_is_valid_code(invite_code) ||
        !matchmaker_decode_endpoint(invite_code, &code_ip_be, &code_port) ||
        code_port == 0) {
        set_last_error("Saved room code is invalid.");
        return false;
    }

    close_active_socket(client);
    close_listen_socket(client);

    if (!create_listen_socket(&listen_fd, code_port, &bound_port) || bound_port != code_port) {
        if (listen_fd != NET_INVALID_SOCKET) {
            socket_close(listen_fd);
        }
        set_last_error("Could not reopen room on saved port.");
        return false;
    }

    if (fetch_public_ipv4(&current_public_ip) && current_public_ip != code_ip_be) {
        struct in_addr old_addr;
        struct in_addr new_addr;
        char old_ip[64];
        char new_ip[64];

        old_addr.s_addr = code_ip_be;
        new_addr.s_addr = current_public_ip;
        old_ip[0] = '\0';
        new_ip[0] = '\0';
        inet_ntop(AF_INET, &old_addr, old_ip, sizeof(old_ip));
        inet_ntop(AF_INET, &new_addr, new_ip, sizeof(new_ip));

        socket_close(listen_fd);
        set_last_error("Public IP changed (%s -> %s). Old invite code is no longer valid.", old_ip, new_ip);
        return false;
    }

    client->listen_socket_handle = (intptr_t)listen_fd;
    client->socket_handle = (intptr_t)NET_INVALID_SOCKET;
    client->is_host = true;
    client->connected = false;
    client->relay_connected = true;
    client->sequence = 0;
    client->rx_bytes = 0;
    client->has_pending_packet = false;
    if (client->host_side != SIDE_WHITE && client->host_side != SIDE_BLACK) {
        client->host_side = SIDE_WHITE;
    }

    strncpy(client->local_username, username, PLAYER_NAME_MAX);
    client->local_username[PLAYER_NAME_MAX] = '\0';
    client->peer_username[0] = '\0';
    strncpy(client->invite_code, invite_code, INVITE_CODE_LEN);
    client->invite_code[INVITE_CODE_LEN] = '\0';

    set_last_error("No error.");
    return true;
}

/* Joins one host directly by invite code (no relay). */
bool network_client_join(NetworkClient* client, const char* username, const char* invite_code) {
    uint32_t ip_be = 0U;
    uint16_t port = 0;
    struct in_addr addr;
    char host_ip[64];
    net_socket_t local_listen_fd = NET_INVALID_SOCKET;
    uint16_t local_bound_port = 0;
    uint32_t local_public_ip = 0U;
    char local_invite_code[INVITE_CODE_LEN + 1];
    NetPacket request;
    NetPacket response;
    net_socket_t socket_fd = NET_INVALID_SOCKET;

    if (client == NULL || !client->initialized || username == NULL || username[0] == '\0' || invite_code == NULL) {
        set_last_error("Join parameters are invalid.");
        return false;
    }
    if (!matchmaker_decode_endpoint(invite_code, &ip_be, &port) || port == 0) {
        set_last_error("Invite code is invalid.");
        return false;
    }

    addr.s_addr = ip_be;
    if (inet_ntop(AF_INET, &addr, host_ip, sizeof(host_ip)) == NULL) {
        set_last_error("Could not decode host IP from invite code.");
        return false;
    }

    close_active_socket(client);
    close_listen_socket(client);

    /* Optional local listener so both peers can act as client/server simultaneously. */
    if (create_listen_socket(&local_listen_fd, 0, &local_bound_port) &&
        fetch_public_ipv4(&local_public_ip) &&
        matchmaker_encode_endpoint(local_public_ip, local_bound_port, local_invite_code)) {
        client->listen_socket_handle = (intptr_t)local_listen_fd;
    } else {
        if (local_listen_fd != NET_INVALID_SOCKET) {
            socket_close(local_listen_fd);
        }
        client->listen_socket_handle = (intptr_t)NET_INVALID_SOCKET;
        local_invite_code[0] = '\0';
    }

    if (!tcp_connect_endpoint(&socket_fd, host_ip, port)) {
        close_listen_socket(client);
        set_last_error("Could not reach host. Host may be offline or blocked by NAT/firewall.");
        return false;
    }

    client->socket_handle = (intptr_t)socket_fd;
    client->relay_connected = true;
    client->is_host = false;
    client->connected = false;
    client->sequence = 0;
    client->rx_bytes = 0;
    client->has_pending_packet = false;

    memset(&request, 0, sizeof(request));
    request.type = NET_MSG_JOIN_REQUEST;
    request.sequence = ++client->sequence;
    strncpy(request.username, username, PLAYER_NAME_MAX);
    request.username[PLAYER_NAME_MAX] = '\0';
    if (local_invite_code[0] != '\0') {
        strncpy(request.invite_code, local_invite_code, INVITE_CODE_LEN);
        request.invite_code[INVITE_CODE_LEN] = '\0';
    }

    if (!send_packet_blocking(client, &request) || !recv_packet_blocking(client, &response)) {
        network_client_shutdown(client);
        set_last_error("Host did not accept join request.");
        return false;
    }

    if (response.type != NET_MSG_JOIN_ACCEPT) {
        const char* msg = (response.username[0] != '\0') ? response.username : "Join request rejected by host.";
        network_client_shutdown(client);
        set_last_error("%s", msg);
        return false;
    }

    if (!finalize_runtime_socket(client)) {
        network_client_shutdown(client);
        return false;
    }

    client->connected = true;
    client->host_side = (response.flags == SIDE_BLACK) ? SIDE_WHITE : SIDE_BLACK;
    strncpy(client->local_username, username, PLAYER_NAME_MAX);
    client->local_username[PLAYER_NAME_MAX] = '\0';
    strncpy(client->peer_username, response.username, PLAYER_NAME_MAX);
    client->peer_username[PLAYER_NAME_MAX] = '\0';
    strncpy(client->invite_code, invite_code, INVITE_CODE_LEN);
    client->invite_code[INVITE_CODE_LEN] = '\0';

    queue_pending_packet(client, &response);
    set_last_error("No error.");
    return true;
}

/* Sends a move packet to currently connected peer. */
bool network_client_send_move(NetworkClient* client, Move move) {
    NetPacket packet;

    if (client == NULL || !client->initialized || !client->relay_connected || !client->connected) {
        set_last_error("Match peer is not connected.");
        return false;
    }

    memset(&packet, 0, sizeof(packet));
    packet.type = NET_MSG_MOVE;
    packet.from = move.from;
    packet.to = move.to;
    packet.promotion = move.promotion;
    packet.flags = move.flags;
    packet.sequence = ++client->sequence;
    packet_set_sender_username(client, &packet);

    return send_packet_runtime(client, &packet);
}

/* Sends a control packet with optional flag payload. */
static bool network_client_send_control(NetworkClient* client, uint8_t type, uint8_t flags) {
    NetPacket packet;

    if (client == NULL || !client->initialized || !client->relay_connected || !client->connected) {
        set_last_error("Match peer is not connected.");
        return false;
    }

    memset(&packet, 0, sizeof(packet));
    packet.type = type;
    packet.flags = flags;
    packet.sequence = ++client->sequence;
    packet_set_sender_username(client, &packet);

    return send_packet_runtime(client, &packet);
}

/* Sends a leave packet before local user exits match. */
bool network_client_send_leave(NetworkClient* client) {
    NetPacket packet;

    if (client == NULL || !client->initialized || !client->relay_connected) {
        set_last_error("Connection is not available.");
        return false;
    }

    memset(&packet, 0, sizeof(packet));
    packet.type = NET_MSG_LEAVE;
    packet.sequence = ++client->sequence;
    packet_set_sender_username(client, &packet);

    return send_packet_runtime(client, &packet);
}

/* Sends local ready/unready state for lobby synchronization. */
bool network_client_send_ready(NetworkClient* client, bool ready) {
    return network_client_send_control(client, NET_MSG_READY, ready ? 1U : 0U);
}

/* Sends start command from host to guest once both sides are ready. */
bool network_client_send_start(NetworkClient* client) {
    return network_client_send_control(client, NET_MSG_START, 0U);
}

/* Polls one incoming packet and updates session state machine. */
bool network_client_poll(NetworkClient* client, NetPacket* out_packet) {
    NetPacket packet;

    if (client == NULL || !client->initialized || !client->relay_connected) {
        return false;
    }

    if (client->has_pending_packet) {
        packet = client->pending_packet;
        client->has_pending_packet = false;
    } else {
        if (!client->connected) {
            accept_pending_peer(client);
        }

        if (!pop_socket_packet(client, &packet)) {
            return false;
        }
    }

    if (packet.type == NET_MSG_JOIN_REQUEST) {
        if (!client->connected) {
            NetPacket ack;

            client->connected = true;
            if (packet.username[0] != '\0') {
                strncpy(client->peer_username, packet.username, PLAYER_NAME_MAX);
                client->peer_username[PLAYER_NAME_MAX] = '\0';
            }

            memset(&ack, 0, sizeof(ack));
            ack.type = NET_MSG_JOIN_ACCEPT;
            ack.flags = client->is_host
                            ? ((client->host_side == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE)
                            : (uint8_t)client->host_side;
            ack.sequence = ++client->sequence;
            strncpy(ack.invite_code, client->invite_code, INVITE_CODE_LEN);
            ack.invite_code[INVITE_CODE_LEN] = '\0';
            packet_set_sender_username(client, &ack);

            if (!send_packet_runtime(client, &ack)) {
                return false;
            }
        }
    } else if (packet.type == NET_MSG_JOIN_ACCEPT) {
        client->connected = true;
        if (packet.username[0] != '\0') {
            strncpy(client->peer_username, packet.username, PLAYER_NAME_MAX);
            client->peer_username[PLAYER_NAME_MAX] = '\0';
        }
        if (packet.invite_code[0] != '\0') {
            strncpy(client->invite_code, packet.invite_code, INVITE_CODE_LEN);
            client->invite_code[INVITE_CODE_LEN] = '\0';
        }
    } else if (packet.type == NET_MSG_LEAVE) {
        on_peer_socket_lost(client, "Peer left the match.");
    } else if (packet.type == NET_MSG_ERROR) {
        const char* msg = (packet.username[0] != '\0') ? packet.username : "Peer reported an unknown network error.";
        set_last_error("%s", msg);
        if ((packet.flags & 1U) != 0U) {
            on_peer_socket_lost(client, msg);
        }
    } else if (packet.type == NET_MSG_PONG) {
        /* Keepalive response: no-op. */
    }

    if (out_packet != NULL) {
        *out_packet = packet;
    }

    return true;
}

/* Checks basic internet availability before entering online mode. */
bool network_relay_probe(void) {
    net_socket_t socket_fd = NET_INVALID_SOCKET;

    if (!network_runtime_init()) {
        return false;
    }

    if (!tcp_connect_endpoint(&socket_fd, PUBLIC_IP_HOST_PRIMARY, 80) &&
        !tcp_connect_endpoint(&socket_fd, PUBLIC_IP_HOST_FALLBACK, 80)) {
        network_runtime_shutdown();
        set_last_error("Internet connection is not available.");
        return false;
    }

    socket_close(socket_fd);
    network_runtime_shutdown();

    set_last_error("No error.");
    return true;
}
