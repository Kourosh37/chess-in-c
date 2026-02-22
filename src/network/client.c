#include "network.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET net_socket_t;
#define NET_INVALID_SOCKET INVALID_SOCKET
#define NET_SOCK_ERR SOCKET_ERROR
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
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

/* Tracks process-level network runtime init (WSAStartup on Windows). */
static bool g_network_runtime_initialized = false;

/* Initializes platform networking runtime. */
static bool network_runtime_init(void) {
#ifdef _WIN32
    if (!g_network_runtime_initialized) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
        g_network_runtime_initialized = true;
    }
#else
    g_network_runtime_initialized = true;
#endif

    return true;
}

/* Shuts down platform networking runtime. */
static void network_runtime_shutdown(void) {
#ifdef _WIN32
    if (g_network_runtime_initialized) {
        WSACleanup();
        g_network_runtime_initialized = false;
    }
#else
    g_network_runtime_initialized = false;
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

/* Enables non-blocking mode on a socket. */
static bool socket_set_nonblocking(net_socket_t socket_fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

/* Compares two sockaddr endpoints (IPv4 only in this baseline implementation). */
static bool sockaddr_equals(const struct sockaddr* a, const struct sockaddr* b) {
    if (a == NULL || b == NULL || a->sa_family != b->sa_family) {
        return false;
    }

    if (a->sa_family == AF_INET) {
        const struct sockaddr_in* aa = (const struct sockaddr_in*)a;
        const struct sockaddr_in* bb = (const struct sockaddr_in*)b;
        return aa->sin_addr.s_addr == bb->sin_addr.s_addr && aa->sin_port == bb->sin_port;
    }

    return false;
}

/* Detects local IPv4 by probing outward route. Falls back to loopback. */
static bool get_local_ipv4(uint32_t* out_ipv4_be) {
    net_socket_t probe;
    struct sockaddr_in remote;
    bool ok = false;

    if (out_ipv4_be == NULL) {
        return false;
    }

    probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == NET_INVALID_SOCKET) {
        return false;
    }

    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    remote.sin_addr.s_addr = inet_addr("8.8.8.8");

    if (connect(probe, (const struct sockaddr*)&remote, (socklen_t)sizeof(remote)) == 0) {
        struct sockaddr_in local;
        socklen_t len = (socklen_t)sizeof(local);

        if (getsockname(probe, (struct sockaddr*)&local, &len) == 0) {
            *out_ipv4_be = local.sin_addr.s_addr;
            ok = true;
        }
    }

    socket_close(probe);

    if (!ok) {
        *out_ipv4_be = htonl(0x7F000001UL);
        ok = true;
    }

    return ok;
}

/* Sends one packed packet to a target endpoint. */
static bool send_packet(NetworkClient* client, const NetPacket* packet, const struct sockaddr* addr, int addr_len) {
    net_socket_t socket_fd = (net_socket_t)client->socket_handle;
    NetPacket wire = *packet;
    int sent;

    wire.sequence = htonl(packet->sequence);
    sent = sendto(socket_fd, (const char*)&wire, (int)sizeof(wire), 0, addr, (socklen_t)addr_len);
    return sent == (int)sizeof(wire);
}

/* Stores peer endpoint into client state. */
static bool set_peer(NetworkClient* client, const struct sockaddr* addr, int addr_len) {
    if (addr_len <= 0 || addr_len > (int)sizeof(client->peer_addr_storage)) {
        return false;
    }

    memcpy(client->peer_addr_storage, addr, (size_t)addr_len);
    client->peer_addr_len = addr_len;
    return true;
}

/* Initializes UDP socket and binds local listen port. */
bool network_client_init(NetworkClient* client, uint16_t listen_port) {
    net_socket_t socket_fd;
    struct sockaddr_in local;

    if (client == NULL) {
        return false;
    }

    memset(client, 0, sizeof(*client));

    if (!network_runtime_init()) {
        return false;
    }

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd == NET_INVALID_SOCKET) {
        network_runtime_shutdown();
        return false;
    }

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(listen_port);

    if (bind(socket_fd, (const struct sockaddr*)&local, (socklen_t)sizeof(local)) == NET_SOCK_ERR) {
        socket_close(socket_fd);
        network_runtime_shutdown();
        return false;
    }

    if (!socket_set_nonblocking(socket_fd)) {
        socket_close(socket_fd);
        network_runtime_shutdown();
        return false;
    }

    client->socket_handle = (intptr_t)socket_fd;
    client->initialized = true;
    return true;
}

/* Releases socket and runtime resources. */
void network_client_shutdown(NetworkClient* client) {
    if (client == NULL || !client->initialized) {
        return;
    }

    {
        net_socket_t socket_fd = (net_socket_t)client->socket_handle;
        if (socket_fd != NET_INVALID_SOCKET) {
            socket_close(socket_fd);
        }
    }

    memset(client, 0, sizeof(*client));
    network_runtime_shutdown();
}

/* Configures this client as host and generates invite code from local endpoint. */
bool network_client_host(NetworkClient* client, const char* username, char out_code[INVITE_CODE_LEN + 1]) {
    net_socket_t socket_fd;
    struct sockaddr_in bound;
    socklen_t bound_len = (socklen_t)sizeof(bound);
    uint32_t ip_be;
    uint16_t port;

    (void)username;

    if (client == NULL || !client->initialized || out_code == NULL) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;

    if (getsockname(socket_fd, (struct sockaddr*)&bound, &bound_len) != 0) {
        return false;
    }

    port = ntohs(bound.sin_port);
    if (!get_local_ipv4(&ip_be)) {
        return false;
    }

    if (!matchmaker_encode_endpoint(ip_be, port, out_code)) {
        return false;
    }

    strncpy(client->invite_code, out_code, INVITE_CODE_LEN);
    client->invite_code[INVITE_CODE_LEN] = '\0';

    client->host_side = (rand() & 1) ? SIDE_WHITE : SIDE_BLACK;
    client->is_host = true;
    client->connected = false;
    client->peer_addr_len = 0;
    return true;
}

/* Decodes invite code and sends join request to host endpoint. */
bool network_client_join(NetworkClient* client, const char* username, const char* invite_code) {
    uint32_t ip_be;
    uint16_t port;
    struct sockaddr_in peer;
    NetPacket join_request;

    if (client == NULL || !client->initialized || username == NULL || invite_code == NULL) {
        return false;
    }

    if (!matchmaker_decode_endpoint(invite_code, &ip_be, &port)) {
        return false;
    }

    memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = ip_be;
    peer.sin_port = htons(port);

    if (!set_peer(client, (const struct sockaddr*)&peer, (int)sizeof(peer))) {
        return false;
    }

    memset(&join_request, 0, sizeof(join_request));
    join_request.type = NET_MSG_JOIN_REQUEST;
    join_request.sequence = ++client->sequence;
    strncpy(join_request.username, username, PLAYER_NAME_MAX);
    strncpy(join_request.invite_code, invite_code, INVITE_CODE_LEN);

    client->is_host = false;
    client->connected = false;

    return send_packet(client,
                       &join_request,
                       (const struct sockaddr*)client->peer_addr_storage,
                       client->peer_addr_len);
}

/* Sends a move packet to currently connected peer. */
bool network_client_send_move(NetworkClient* client, Move move) {
    NetPacket packet;

    if (client == NULL || !client->initialized || !client->connected || client->peer_addr_len <= 0) {
        return false;
    }

    memset(&packet, 0, sizeof(packet));
    packet.type = NET_MSG_MOVE;
    packet.from = move.from;
    packet.to = move.to;
    packet.promotion = move.promotion;
    packet.flags = move.flags;
    packet.sequence = ++client->sequence;

    return send_packet(client, &packet, (const struct sockaddr*)client->peer_addr_storage, client->peer_addr_len);
}

/* Sends a control packet with optional flag payload. */
static bool network_client_send_control(NetworkClient* client, uint8_t type, uint8_t flags) {
    NetPacket packet;

    if (client == NULL || !client->initialized || !client->connected || client->peer_addr_len <= 0) {
        return false;
    }

    memset(&packet, 0, sizeof(packet));
    packet.type = type;
    packet.flags = flags;
    packet.sequence = ++client->sequence;

    return send_packet(client, &packet, (const struct sockaddr*)client->peer_addr_storage, client->peer_addr_len);
}

/* Sends a leave packet to peer before local user exits an online match. */
bool network_client_send_leave(NetworkClient* client) {
    NetPacket packet;

    if (client == NULL || !client->initialized || client->peer_addr_len <= 0) {
        return false;
    }

    memset(&packet, 0, sizeof(packet));
    packet.type = NET_MSG_LEAVE;
    packet.sequence = ++client->sequence;

    return send_packet(client,
                       &packet,
                       (const struct sockaddr*)client->peer_addr_storage,
                       client->peer_addr_len);
}

/* Sends local ready/unready state for lobby synchronization. */
bool network_client_send_ready(NetworkClient* client, bool ready) {
    return network_client_send_control(client, NET_MSG_READY, ready ? 1U : 0U);
}

/* Sends start command from host to guest once both sides are ready. */
bool network_client_send_start(NetworkClient* client) {
    return network_client_send_control(client, NET_MSG_START, 0U);
}

/* Polls one incoming packet and updates host/guest session state machine. */
bool network_client_poll(NetworkClient* client, NetPacket* out_packet) {
    net_socket_t socket_fd;
    struct sockaddr_storage from;
    socklen_t from_len = (socklen_t)sizeof(from);
    NetPacket packet;
    int bytes;

    if (client == NULL || !client->initialized) {
        return false;
    }

    socket_fd = (net_socket_t)client->socket_handle;
    bytes = recvfrom(socket_fd, (char*)&packet, (int)sizeof(packet), 0, (struct sockaddr*)&from, &from_len);
    if (bytes <= 0 || bytes < (int)sizeof(packet)) {
        return false;
    }

    packet.sequence = ntohl(packet.sequence);

    if (client->is_host && packet.type == NET_MSG_JOIN_REQUEST) {
        if (client->invite_code[0] != '\0' && client->peer_addr_len == 0) {
            NetPacket ack;

            set_peer(client, (const struct sockaddr*)&from, (int)from_len);
            client->connected = true;

            memset(&ack, 0, sizeof(ack));
            ack.type = NET_MSG_JOIN_ACCEPT;
            ack.sequence = ++client->sequence;
            ack.flags = (client->host_side == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
            strncpy(ack.invite_code, client->invite_code, INVITE_CODE_LEN);
            send_packet(client, &ack, (const struct sockaddr*)&from, (int)from_len);
        } else {
            NetPacket reject;

            memset(&reject, 0, sizeof(reject));
            reject.type = NET_MSG_JOIN_REJECT;
            reject.sequence = ++client->sequence;
            send_packet(client, &reject, (const struct sockaddr*)&from, (int)from_len);
        }
    }

    if (!client->is_host && packet.type == NET_MSG_JOIN_ACCEPT) {
        if (sockaddr_equals((const struct sockaddr*)client->peer_addr_storage, (const struct sockaddr*)&from)) {
            client->connected = true;
        }
    }

    if ((packet.type == NET_MSG_MOVE ||
         packet.type == NET_MSG_LEAVE ||
         packet.type == NET_MSG_READY ||
         packet.type == NET_MSG_START) &&
        client->peer_addr_len > 0) {
        if (!sockaddr_equals((const struct sockaddr*)client->peer_addr_storage, (const struct sockaddr*)&from)) {
            return false;
        }
    }

    if (packet.type == NET_MSG_LEAVE && client->peer_addr_len > 0) {
        client->connected = false;
        if (client->is_host) {
            client->peer_addr_len = 0;
        }
    }

    if (out_packet != NULL) {
        *out_packet = packet;
    }

    return true;
}
