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

#ifndef CHESS_RELAY_PRIMARY_HOST
#define CHESS_RELAY_PRIMARY_HOST "127.0.0.1"
#endif

#ifndef CHESS_RELAY_PRIMARY_PORT
#define CHESS_RELAY_PRIMARY_PORT 5050
#endif

#define RELAY_LOCAL_HOST "127.0.0.1"
#define RELAY_LOCAL_PORT_BASE 5050
#define RELAY_LOCAL_PORT_SPAN 6
#define CONNECT_TIMEOUT_MS 3000
#define HANDSHAKE_TIMEOUT_MS 3000
#define IO_TIMEOUT_MS 700

typedef struct RelayEndpoint {
    const char* host;
    uint16_t port;
    bool is_local;
} RelayEndpoint;

/* Tracks process-level network runtime init (WSAStartup on Windows). */
static int g_network_runtime_refcount = 0;
static char g_last_error[256] = "No error.";
static bool g_local_relay_launch_attempted = false;

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

/* Builds one preferred endpoint list (managed internally, no user config). */
static int build_relay_endpoints(RelayEndpoint* out_list, int capacity) {
    int count = 0;

    if (out_list == NULL || capacity <= 0) {
        return 0;
    }

    if (count < capacity) {
        out_list[count].host = CHESS_RELAY_PRIMARY_HOST;
        out_list[count].port = (uint16_t)CHESS_RELAY_PRIMARY_PORT;
        out_list[count].is_local = false;
        count++;
    }

    for (int i = 0; i < RELAY_LOCAL_PORT_SPAN && count < capacity; ++i) {
        out_list[count].host = RELAY_LOCAL_HOST;
        out_list[count].port = (uint16_t)(RELAY_LOCAL_PORT_BASE + i);
        out_list[count].is_local = true;
        count++;
    }

    return count;
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
        set_last_error("Could not resolve relay host: %s", host);
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

/* Verifies endpoint is a compatible chess relay using ping/pong handshake. */
static bool verify_relay_endpoint(net_socket_t socket_fd) {
    NetPacket ping;
    NetPacket pong;

    memset(&ping, 0, sizeof(ping));
    ping.type = NET_MSG_PING;
    ping.sequence = htonl(1U);

    if (!socket_send_all(socket_fd, &ping, (int)sizeof(ping), HANDSHAKE_TIMEOUT_MS)) {
        return false;
    }
    if (!socket_recv_all(socket_fd, &pong, (int)sizeof(pong), HANDSHAKE_TIMEOUT_MS)) {
        return false;
    }

    pong.sequence = ntohl(pong.sequence);
    if (pong.type != NET_MSG_PONG) {
        set_last_error("Connected endpoint is not a compatible relay.");
        return false;
    }

    return true;
}

/* Starts local relay server automatically so user never handles ports manually. */
static void launch_local_relay_server(void) {
    if (g_local_relay_launch_attempted) {
        return;
    }
    g_local_relay_launch_attempted = true;

#ifdef _WIN32
    {
        const char* commands[] = {
            "chess_relay_server.exe",
            ".\\chess_relay_server.exe",
            "build\\chess_relay_server.exe",
            ".\\build\\chess_relay_server.exe"
        };

        for (int i = 0; i < (int)(sizeof(commands) / sizeof(commands[0])); ++i) {
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            char cmdline[260];
            BOOL ok;

            memset(&si, 0, sizeof(si));
            memset(&pi, 0, sizeof(pi));
            si.cb = sizeof(si);
            snprintf(cmdline, sizeof(cmdline), "%s", commands[i]);

            ok = CreateProcessA(NULL,
                                cmdline,
                                NULL,
                                NULL,
                                FALSE,
                                DETACHED_PROCESS | CREATE_NO_WINDOW,
                                NULL,
                                NULL,
                                &si,
                                &pi);
            if (ok) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                Sleep(600);
                return;
            }
        }
    }
#else
    {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./chess_relay_server", "chess_relay_server", (char*)NULL);
            execl("build/chess_relay_server", "chess_relay_server", (char*)NULL);
            _exit(1);
        }
        if (pid > 0) {
            usleep(600000);
        }
    }
#endif
}

/* Connects to managed relay endpoints (cloud first, local auto-fallback). */
static bool tcp_connect_relay(net_socket_t* out_socket) {
    RelayEndpoint endpoints[1 + RELAY_LOCAL_PORT_SPAN];
    int endpoint_count = build_relay_endpoints(endpoints, (int)(sizeof(endpoints) / sizeof(endpoints[0])));

    if (out_socket == NULL) {
        return false;
    }
    *out_socket = NET_INVALID_SOCKET;

    for (int i = 0; i < endpoint_count; ++i) {
        const RelayEndpoint* ep = &endpoints[i];

        if (tcp_connect_endpoint(out_socket, ep->host, ep->port)) {
            if (verify_relay_endpoint(*out_socket)) {
                set_last_error("No error.");
                return true;
            }
            socket_close(*out_socket);
            *out_socket = NET_INVALID_SOCKET;
        }

        if (ep->is_local && !g_local_relay_launch_attempted) {
            launch_local_relay_server();
            if (tcp_connect_endpoint(out_socket, ep->host, ep->port)) {
                if (verify_relay_endpoint(*out_socket)) {
                    set_last_error("No error.");
                    return true;
                }
                socket_close(*out_socket);
                *out_socket = NET_INVALID_SOCKET;
            }
        }
    }

    set_last_error("Online service is not reachable right now.");
    return false;
}

/* Sends all bytes with timeout over blocking relay socket. */
static bool socket_send_all(net_socket_t socket_fd, const void* data, int bytes, int timeout_ms) {
    const char* cursor = (const char*)data;
    int sent = 0;

    while (sent < bytes) {
        int rc;

        if (!socket_wait(socket_fd, true, timeout_ms)) {
            set_last_error("Send timeout while contacting relay.");
            return false;
        }

        rc = send(socket_fd, cursor + sent, bytes - sent, 0);
        if (rc <= 0) {
            set_last_error("Failed to send packet to relay.");
            return false;
        }

        sent += rc;
    }

    return true;
}

/* Receives exact bytes with timeout over blocking relay socket. */
static bool socket_recv_all(net_socket_t socket_fd, void* data, int bytes, int timeout_ms) {
    char* cursor = (char*)data;
    int received = 0;

    while (received < bytes) {
        int rc;

        if (!socket_wait(socket_fd, false, timeout_ms)) {
            set_last_error("Relay response timeout.");
            return false;
        }

        rc = recv(socket_fd, cursor + received, bytes - received, 0);
        if (rc <= 0) {
            set_last_error("Relay connection closed unexpectedly.");
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

    if (client == NULL || packet == NULL || !client->relay_connected) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    wire.sequence = htonl(packet->sequence);
    return socket_send_all(socket_fd, &wire, (int)sizeof(wire), HANDSHAKE_TIMEOUT_MS);
}

/* Receives one packet in blocking handshake mode. */
static bool recv_packet_blocking(NetworkClient* client, NetPacket* out_packet) {
    NetPacket wire;
    net_socket_t socket_fd;

    if (client == NULL || out_packet == NULL || !client->relay_connected) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
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

    if (client == NULL || packet == NULL || !client->relay_connected) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    wire.sequence = htonl(packet->sequence);

    while (sent < (int)sizeof(wire)) {
        int rc = send(socket_fd, raw + sent, (int)sizeof(wire) - sent, 0);
        if (rc > 0) {
            sent += rc;
            continue;
        }

        if (rc == 0) {
            set_last_error("Relay connection closed.");
            client->relay_connected = false;
            client->connected = false;
            return false;
        }

        if (socket_error_would_block()) {
            if (!socket_wait(socket_fd, true, IO_TIMEOUT_MS)) {
                set_last_error("Relay send timed out.");
                return false;
            }
            continue;
        }

        set_last_error("Relay send failed.");
        client->relay_connected = false;
        client->connected = false;
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
        set_last_error("Failed to switch relay socket to non-blocking mode.");
        return false;
    }
    return true;
}

/* Establishes TCP connection to relay server if needed. */
static bool ensure_relay_connected(NetworkClient* client) {
    net_socket_t socket_fd;

    if (client == NULL || !client->initialized) {
        return false;
    }
    if (client->relay_connected) {
        return true;
    }

    if (!tcp_connect_relay(&socket_fd)) {
        return false;
    }

    client->socket_handle = (intptr_t)socket_fd;
    client->relay_connected = true;
    client->rx_bytes = 0;
    client->has_pending_packet = false;
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

/* Reads one packet from relay socket buffer in non-blocking mode. */
static bool pop_socket_packet(NetworkClient* client, NetPacket* out_packet) {
    net_socket_t socket_fd;

    if (client == NULL || out_packet == NULL || !client->relay_connected) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;

    while (client->rx_bytes < (int)sizeof(NetPacket)) {
        int capacity = (int)sizeof(client->rx_buffer) - client->rx_bytes;
        int rc;

        if (capacity <= 0) {
            set_last_error("Relay input buffer overflow.");
            client->relay_connected = false;
            client->connected = false;
            return false;
        }

        rc = recv(socket_fd, (char*)client->rx_buffer + client->rx_bytes, capacity, 0);
        if (rc > 0) {
            client->rx_bytes += rc;
            continue;
        }

        if (rc == 0) {
            client->relay_connected = false;
            client->connected = false;
            set_last_error("Relay disconnected.");
            return false;
        }

        if (socket_error_would_block()) {
            return false;
        }

        client->relay_connected = false;
        client->connected = false;
        set_last_error("Relay receive failed.");
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

/* Initializes relay client runtime state (socket is connected on host/join). */
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
    return true;
}

/* Releases socket and runtime resources. */
void network_client_shutdown(NetworkClient* client) {
    if (client == NULL || !client->initialized) {
        return;
    }

    if (client->relay_connected) {
        net_socket_t socket_fd = (net_socket_t)client->socket_handle;
        if (socket_fd != NET_INVALID_SOCKET) {
            socket_close(socket_fd);
        }
    }

    memset(client, 0, sizeof(*client));
    network_runtime_shutdown();
}

/* Common host handshake path (new room or reconnect to existing code). */
static bool host_handshake(NetworkClient* client,
                           const char* username,
                           const char* requested_code,
                           char out_code[INVITE_CODE_LEN + 1]) {
    NetPacket request;
    NetPacket response;

    if (client == NULL || !client->initialized || username == NULL || username[0] == '\0') {
        set_last_error("Host username is invalid.");
        return false;
    }

    if (!ensure_relay_connected(client)) {
        return false;
    }

    memset(&request, 0, sizeof(request));
    request.type = NET_MSG_RELAY_HOST;
    request.sequence = ++client->sequence;
    strncpy(request.username, username, PLAYER_NAME_MAX);
    request.username[PLAYER_NAME_MAX] = '\0';
    if (requested_code != NULL && requested_code[0] != '\0') {
        strncpy(request.invite_code, requested_code, INVITE_CODE_LEN);
        request.invite_code[INVITE_CODE_LEN] = '\0';
    }

    if (!send_packet_blocking(client, &request) || !recv_packet_blocking(client, &response)) {
        network_client_shutdown(client);
        return false;
    }

    if (response.type != NET_MSG_RELAY_HOST_ACK) {
        const char* msg = (response.username[0] != '\0') ? response.username : "Relay rejected host request.";
        set_last_error("%s", msg);
        network_client_shutdown(client);
        return false;
    }

    client->is_host = true;
    client->connected = false;
    client->host_side = (response.flags == SIDE_BLACK) ? SIDE_BLACK : SIDE_WHITE;
    strncpy(client->local_username, username, PLAYER_NAME_MAX);
    client->local_username[PLAYER_NAME_MAX] = '\0';
    client->peer_username[0] = '\0';
    strncpy(client->invite_code, response.invite_code, INVITE_CODE_LEN);
    client->invite_code[INVITE_CODE_LEN] = '\0';

    if (out_code != NULL) {
        strncpy(out_code, client->invite_code, INVITE_CODE_LEN);
        out_code[INVITE_CODE_LEN] = '\0';
    }

    if (!finalize_runtime_socket(client)) {
        network_client_shutdown(client);
        return false;
    }

    return true;
}

/* Configures this client as host and requests room code from relay server. */
bool network_client_host(NetworkClient* client, const char* username, char out_code[INVITE_CODE_LEN + 1]) {
    return host_handshake(client, username, NULL, out_code);
}

/* Reconnects host session to an already existing room code on relay server. */
bool network_client_host_reconnect(NetworkClient* client, const char* username, const char* invite_code) {
    if (!matchmaker_is_valid_code(invite_code)) {
        set_last_error("Saved room code is invalid.");
        return false;
    }
    return host_handshake(client, username, invite_code, NULL);
}

/* Joins one relay room by invite code and receives side assignment. */
bool network_client_join(NetworkClient* client, const char* username, const char* invite_code) {
    NetPacket request;
    NetPacket response;

    if (client == NULL || !client->initialized || username == NULL || username[0] == '\0' || invite_code == NULL) {
        set_last_error("Join parameters are invalid.");
        return false;
    }
    if (!matchmaker_is_valid_code(invite_code)) {
        set_last_error("Invite code is invalid.");
        return false;
    }

    if (!ensure_relay_connected(client)) {
        return false;
    }

    memset(&request, 0, sizeof(request));
    request.type = NET_MSG_RELAY_JOIN;
    request.sequence = ++client->sequence;
    strncpy(request.username, username, PLAYER_NAME_MAX);
    request.username[PLAYER_NAME_MAX] = '\0';
    strncpy(request.invite_code, invite_code, INVITE_CODE_LEN);
    request.invite_code[INVITE_CODE_LEN] = '\0';

    if (!send_packet_blocking(client, &request) || !recv_packet_blocking(client, &response)) {
        network_client_shutdown(client);
        return false;
    }

    if (response.type != NET_MSG_JOIN_ACCEPT) {
        const char* msg = (response.username[0] != '\0') ? response.username : "Join request rejected by relay.";
        set_last_error("%s", msg);
        network_client_shutdown(client);
        return false;
    }

    client->is_host = false;
    client->connected = true;
    client->host_side = (response.flags == SIDE_BLACK) ? SIDE_WHITE : SIDE_BLACK;
    strncpy(client->local_username, username, PLAYER_NAME_MAX);
    client->local_username[PLAYER_NAME_MAX] = '\0';
    strncpy(client->peer_username, response.username, PLAYER_NAME_MAX);
    client->peer_username[PLAYER_NAME_MAX] = '\0';
    strncpy(client->invite_code, invite_code, INVITE_CODE_LEN);
    client->invite_code[INVITE_CODE_LEN] = '\0';

    queue_pending_packet(client, &response);

    if (!finalize_runtime_socket(client)) {
        network_client_shutdown(client);
        return false;
    }

    return true;
}

/* Sends a move packet to currently connected peer through relay. */
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

/* Sends a leave packet through relay before local user exits match. */
bool network_client_send_leave(NetworkClient* client) {
    NetPacket packet;

    if (client == NULL || !client->initialized || !client->relay_connected) {
        set_last_error("Relay connection is not available.");
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
    } else if (!pop_socket_packet(client, &packet)) {
        return false;
    }

    if (packet.type == NET_MSG_JOIN_REQUEST) {
        client->connected = true;
        if (packet.username[0] != '\0') {
            strncpy(client->peer_username, packet.username, PLAYER_NAME_MAX);
            client->peer_username[PLAYER_NAME_MAX] = '\0';
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
        client->connected = false;
    } else if (packet.type == NET_MSG_ERROR) {
        const char* msg = (packet.username[0] != '\0') ? packet.username : "Relay reported an unknown error.";
        set_last_error("%s", msg);
        if ((packet.flags & 1U) != 0U) {
            client->connected = false;
        }
    } else if (packet.type == NET_MSG_PONG) {
        /* Keepalive response: no-op. */
    }

    if (out_packet != NULL) {
        *out_packet = packet;
    }

    return true;
}

/* Checks relay availability before entering online mode. */
bool network_relay_probe(void) {
    net_socket_t socket_fd = NET_INVALID_SOCKET;
    NetPacket ping;
    NetPacket pong;

    if (!network_runtime_init()) {
        return false;
    }

    if (!tcp_connect_relay(&socket_fd)) {
        network_runtime_shutdown();
        return false;
    }

    memset(&ping, 0, sizeof(ping));
    ping.type = NET_MSG_PING;
    ping.sequence = 1;

    ping.sequence = htonl(ping.sequence);
    if (!socket_send_all(socket_fd, &ping, (int)sizeof(ping), HANDSHAKE_TIMEOUT_MS)) {
        socket_close(socket_fd);
        network_runtime_shutdown();
        return false;
    }

    if (!socket_recv_all(socket_fd, &pong, (int)sizeof(pong), HANDSHAKE_TIMEOUT_MS)) {
        socket_close(socket_fd);
        network_runtime_shutdown();
        return false;
    }
    pong.sequence = ntohl(pong.sequence);

    socket_close(socket_fd);
    network_runtime_shutdown();

    if (pong.type != NET_MSG_PONG) {
        set_last_error("Relay is reachable but not responding to handshake.");
        return false;
    }

    set_last_error("No error.");
    return true;
}
