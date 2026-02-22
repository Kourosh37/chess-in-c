#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#include <errno.h>
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

#define RELAY_MAX_CLIENTS 256
#define RELAY_MAX_ROOMS 128
#define RELAY_CLEANUP_SECONDS 3600
#define RELAY_PORT_BASE 5050
#define RELAY_PORT_SPAN 6

typedef struct RelayClient {
    bool used;
    net_socket_t socket_fd;
    int room_index;
    bool is_host;
    char username[PLAYER_NAME_MAX + 1];
    uint8_t rx_buffer[sizeof(NetPacket) * 8];
    int rx_bytes;
} RelayClient;

typedef struct RelayRoom {
    bool used;
    char code[INVITE_CODE_LEN + 1];
    Side host_side;
    int host_client;
    int guest_client;
    char host_username[PLAYER_NAME_MAX + 1];
    char guest_username[PLAYER_NAME_MAX + 1];
    time_t updated_at;
} RelayRoom;

static RelayClient g_clients[RELAY_MAX_CLIENTS];
static RelayRoom g_rooms[RELAY_MAX_ROOMS];

/* Closes platform socket. */
static void socket_close(net_socket_t socket_fd) {
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

/* Returns true when recv/accept failed only because no data is available. */
static bool socket_would_block(void) {
#ifdef _WIN32
    int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
}

/* Enables non-blocking mode on one socket. */
static bool socket_set_nonblocking(net_socket_t socket_fd) {
#ifdef _WIN32
    u_long mode = 1UL;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

/* Sends one packet (best effort) to one connected relay client. */
static bool send_packet_to_client(int client_index, const NetPacket* packet) {
    NetPacket wire;
    RelayClient* client;
    int sent;

    if (packet == NULL || client_index < 0 || client_index >= RELAY_MAX_CLIENTS) {
        return false;
    }

    client = &g_clients[client_index];
    if (!client->used) {
        return false;
    }

    wire = *packet;
    wire.sequence = htonl(packet->sequence);

    sent = send(client->socket_fd, (const char*)&wire, (int)sizeof(wire), 0);
    return sent == (int)sizeof(wire);
}

/* Sends textual error packet to one client. */
static void send_error_to_client(int client_index, const char* message) {
    NetPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = NET_MSG_ERROR;
    if (message != NULL) {
        strncpy(packet.username, message, PLAYER_NAME_MAX);
        packet.username[PLAYER_NAME_MAX] = '\0';
    }
    send_packet_to_client(client_index, &packet);
}

/* Finds room index by invite code. */
static int room_find_by_code(const char* code) {
    if (code == NULL || code[0] == '\0') {
        return -1;
    }

    for (int i = 0; i < RELAY_MAX_ROOMS; ++i) {
        if (g_rooms[i].used && strncmp(g_rooms[i].code, code, INVITE_CODE_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

/* Finds a free room slot. */
static int room_find_free(void) {
    for (int i = 0; i < RELAY_MAX_ROOMS; ++i) {
        if (!g_rooms[i].used) {
            return i;
        }
    }
    return -1;
}

/* Destroys one room and unlinks attached clients. */
static void room_destroy(int room_index) {
    RelayRoom* room;

    if (room_index < 0 || room_index >= RELAY_MAX_ROOMS) {
        return;
    }

    room = &g_rooms[room_index];
    if (!room->used) {
        return;
    }

    if (room->host_client >= 0 && room->host_client < RELAY_MAX_CLIENTS && g_clients[room->host_client].used) {
        g_clients[room->host_client].room_index = -1;
        g_clients[room->host_client].is_host = false;
    }
    if (room->guest_client >= 0 && room->guest_client < RELAY_MAX_CLIENTS && g_clients[room->guest_client].used) {
        g_clients[room->guest_client].room_index = -1;
        g_clients[room->guest_client].is_host = false;
    }

    memset(room, 0, sizeof(*room));
}

/* Returns peer client index from one room and sender role. */
static int room_peer_client(const RelayRoom* room, bool sender_is_host) {
    if (room == NULL || !room->used) {
        return -1;
    }
    return sender_is_host ? room->guest_client : room->host_client;
}

/* Forwards one game packet to room peer if connected. */
static void room_forward_packet(int room_index, bool sender_is_host, const NetPacket* packet) {
    RelayRoom* room;
    int peer_index;

    if (room_index < 0 || room_index >= RELAY_MAX_ROOMS || packet == NULL) {
        return;
    }
    room = &g_rooms[room_index];
    if (!room->used) {
        return;
    }

    peer_index = room_peer_client(room, sender_is_host);
    if (peer_index < 0 || peer_index >= RELAY_MAX_CLIENTS || !g_clients[peer_index].used) {
        return;
    }

    send_packet_to_client(peer_index, packet);
}

/* Handles client disconnect while keeping room available for reconnects. */
static void client_disconnect(int client_index) {
    RelayClient* client;
    RelayRoom* room;

    if (client_index < 0 || client_index >= RELAY_MAX_CLIENTS) {
        return;
    }

    client = &g_clients[client_index];
    if (!client->used) {
        return;
    }

    if (client->room_index >= 0 && client->room_index < RELAY_MAX_ROOMS) {
        room = &g_rooms[client->room_index];
        if (room->used) {
            int peer_index;
            NetPacket notice;

            if (client->is_host) {
                room->host_client = -1;
                peer_index = room->guest_client;
                room->updated_at = time(NULL);

                if (peer_index >= 0 && peer_index < RELAY_MAX_CLIENTS && g_clients[peer_index].used) {
                    memset(&notice, 0, sizeof(notice));
                    notice.type = NET_MSG_ERROR;
                    notice.flags = 1U;
                    strncpy(notice.username, "Host disconnected. Waiting reconnect.", PLAYER_NAME_MAX);
                    notice.username[PLAYER_NAME_MAX] = '\0';
                    send_packet_to_client(peer_index, &notice);
                }
            } else {
                room->guest_client = -1;
                peer_index = room->host_client;
                room->updated_at = time(NULL);

                if (peer_index >= 0 && peer_index < RELAY_MAX_CLIENTS && g_clients[peer_index].used) {
                    memset(&notice, 0, sizeof(notice));
                    notice.type = NET_MSG_ERROR;
                    notice.flags = 1U;
                    strncpy(notice.username, "Guest disconnected. Waiting reconnect.", PLAYER_NAME_MAX);
                    notice.username[PLAYER_NAME_MAX] = '\0';
                    send_packet_to_client(peer_index, &notice);
                }
            }

            if (room->host_client < 0 && room->guest_client < 0) {
                room->updated_at = time(NULL);
            }
        }
    }

    socket_close(client->socket_fd);
    memset(client, 0, sizeof(*client));
    client->room_index = -1;
}

/* Creates a unique room code and initializes room state. */
static bool room_create_for_host(int room_index, int host_client, const char* host_username, char out_code[INVITE_CODE_LEN + 1]) {
    RelayRoom* room;
    char code[INVITE_CODE_LEN + 1];
    int tries = 64;

    if (room_index < 0 || room_index >= RELAY_MAX_ROOMS || host_username == NULL || out_code == NULL) {
        return false;
    }

    while (tries-- > 0) {
        matchmaker_generate_code(code);
        if (room_find_by_code(code) < 0) {
            break;
        }
    }
    if (tries <= 0) {
        return false;
    }

    room = &g_rooms[room_index];
    memset(room, 0, sizeof(*room));
    room->used = true;
    room->host_side = (rand() & 1) ? SIDE_WHITE : SIDE_BLACK;
    room->host_client = host_client;
    room->guest_client = -1;
    room->updated_at = time(NULL);
    strncpy(room->code, code, INVITE_CODE_LEN);
    room->code[INVITE_CODE_LEN] = '\0';
    strncpy(room->host_username, host_username, PLAYER_NAME_MAX);
    room->host_username[PLAYER_NAME_MAX] = '\0';

    strncpy(out_code, code, INVITE_CODE_LEN);
    out_code[INVITE_CODE_LEN] = '\0';
    return true;
}

/* Handles RELAY_HOST command for room creation or host reconnect. */
static void handle_host_request(int client_index, const NetPacket* packet) {
    RelayClient* client;
    NetPacket response;
    int room_index = -1;
    bool reconnect = false;

    if (client_index < 0 || client_index >= RELAY_MAX_CLIENTS || packet == NULL) {
        return;
    }

    client = &g_clients[client_index];

    if (packet->username[0] == '\0') {
        send_error_to_client(client_index, "Username is required.");
        return;
    }
    if (client->room_index >= 0) {
        send_error_to_client(client_index, "Client already attached to room.");
        return;
    }

    if (packet->invite_code[0] != '\0') {
        room_index = room_find_by_code(packet->invite_code);
        if (room_index >= 0) {
            RelayRoom* room = &g_rooms[room_index];
            if (room->host_client < 0 && strncmp(room->host_username, packet->username, PLAYER_NAME_MAX) == 0) {
                reconnect = true;
                room->host_client = client_index;
                room->updated_at = time(NULL);
            } else {
                send_error_to_client(client_index, "Could not reclaim host room.");
                return;
            }
        }
    }

    if (!reconnect) {
        room_index = room_find_free();
        if (room_index < 0) {
            send_error_to_client(client_index, "Relay room capacity reached.");
            return;
        }
        if (!room_create_for_host(room_index, client_index, packet->username, response.invite_code)) {
            send_error_to_client(client_index, "Failed to create room code.");
            return;
        }
    }

    {
        RelayRoom* room = &g_rooms[room_index];
        client->room_index = room_index;
        client->is_host = true;
        strncpy(client->username, packet->username, PLAYER_NAME_MAX);
        client->username[PLAYER_NAME_MAX] = '\0';

        memset(&response, 0, sizeof(response));
        response.type = NET_MSG_RELAY_HOST_ACK;
        response.flags = (uint8_t)room->host_side;
        strncpy(response.invite_code, room->code, INVITE_CODE_LEN);
        response.invite_code[INVITE_CODE_LEN] = '\0';
        send_packet_to_client(client_index, &response);

        if (room->guest_client >= 0 && room->guest_client < RELAY_MAX_CLIENTS && g_clients[room->guest_client].used) {
            NetPacket join_notice;
            memset(&join_notice, 0, sizeof(join_notice));
            join_notice.type = NET_MSG_JOIN_REQUEST;
            strncpy(join_notice.username, room->guest_username, PLAYER_NAME_MAX);
            join_notice.username[PLAYER_NAME_MAX] = '\0';
            strncpy(join_notice.invite_code, room->code, INVITE_CODE_LEN);
            join_notice.invite_code[INVITE_CODE_LEN] = '\0';
            send_packet_to_client(client_index, &join_notice);
        }
    }
}

/* Handles RELAY_JOIN command for guest attach/reconnect to room. */
static void handle_join_request(int client_index, const NetPacket* packet) {
    RelayClient* client;
    RelayRoom* room;
    NetPacket accept;
    NetPacket host_notice;
    int room_index;

    if (client_index < 0 || client_index >= RELAY_MAX_CLIENTS || packet == NULL) {
        return;
    }

    client = &g_clients[client_index];

    if (packet->username[0] == '\0') {
        send_error_to_client(client_index, "Username is required.");
        return;
    }
    if (!matchmaker_is_valid_code(packet->invite_code)) {
        send_error_to_client(client_index, "Invite code is invalid.");
        return;
    }
    if (client->room_index >= 0) {
        send_error_to_client(client_index, "Client already attached to room.");
        return;
    }

    room_index = room_find_by_code(packet->invite_code);
    if (room_index < 0) {
        send_error_to_client(client_index, "Room not found.");
        return;
    }

    room = &g_rooms[room_index];
    if (!room->used) {
        send_error_to_client(client_index, "Room not available.");
        return;
    }

    if (room->guest_client >= 0 && room->guest_client != client_index) {
        send_error_to_client(client_index, "Room already has a guest.");
        return;
    }

    if (room->guest_username[0] != '\0' &&
        strncmp(room->guest_username, packet->username, PLAYER_NAME_MAX) != 0 &&
        room->guest_client < 0) {
        send_error_to_client(client_index, "Room belongs to another guest.");
        return;
    }

    room->guest_client = client_index;
    room->updated_at = time(NULL);
    if (room->guest_username[0] == '\0') {
        strncpy(room->guest_username, packet->username, PLAYER_NAME_MAX);
        room->guest_username[PLAYER_NAME_MAX] = '\0';
    }

    client->room_index = room_index;
    client->is_host = false;
    strncpy(client->username, packet->username, PLAYER_NAME_MAX);
    client->username[PLAYER_NAME_MAX] = '\0';

    memset(&accept, 0, sizeof(accept));
    accept.type = NET_MSG_JOIN_ACCEPT;
    accept.flags = (room->host_side == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    strncpy(accept.invite_code, room->code, INVITE_CODE_LEN);
    accept.invite_code[INVITE_CODE_LEN] = '\0';
    strncpy(accept.username, room->host_username, PLAYER_NAME_MAX);
    accept.username[PLAYER_NAME_MAX] = '\0';
    send_packet_to_client(client_index, &accept);

    if (room->host_client >= 0 && room->host_client < RELAY_MAX_CLIENTS && g_clients[room->host_client].used) {
        memset(&host_notice, 0, sizeof(host_notice));
        host_notice.type = NET_MSG_JOIN_REQUEST;
        strncpy(host_notice.username, room->guest_username, PLAYER_NAME_MAX);
        host_notice.username[PLAYER_NAME_MAX] = '\0';
        strncpy(host_notice.invite_code, room->code, INVITE_CODE_LEN);
        host_notice.invite_code[INVITE_CODE_LEN] = '\0';
        send_packet_to_client(room->host_client, &host_notice);
    } else {
        NetPacket offline_notice;
        memset(&offline_notice, 0, sizeof(offline_notice));
        offline_notice.type = NET_MSG_ERROR;
        offline_notice.flags = 1U;
        strncpy(offline_notice.username, "Host disconnected. Waiting reconnect.", PLAYER_NAME_MAX);
        offline_notice.username[PLAYER_NAME_MAX] = '\0';
        send_packet_to_client(client_index, &offline_notice);
    }
}

/* Routes runtime game packets between host and guest in one room. */
static void handle_room_packet(int client_index, const NetPacket* packet) {
    RelayClient* client;
    RelayRoom* room;

    if (client_index < 0 || client_index >= RELAY_MAX_CLIENTS || packet == NULL) {
        return;
    }

    client = &g_clients[client_index];
    if (!client->used || client->room_index < 0 || client->room_index >= RELAY_MAX_ROOMS) {
        send_error_to_client(client_index, "Client is not attached to any room.");
        return;
    }

    room = &g_rooms[client->room_index];
    if (!room->used) {
        send_error_to_client(client_index, "Room was removed.");
        client->room_index = -1;
        return;
    }

    room->updated_at = time(NULL);

    if (packet->type == NET_MSG_PING) {
        NetPacket pong;
        memset(&pong, 0, sizeof(pong));
        pong.type = NET_MSG_PONG;
        send_packet_to_client(client_index, &pong);
        return;
    }

    if (packet->type == NET_MSG_LEAVE) {
        room_forward_packet(client->room_index, client->is_host, packet);
        room_destroy(client->room_index);
        return;
    }

    if (packet->type == NET_MSG_MOVE ||
        packet->type == NET_MSG_READY ||
        packet->type == NET_MSG_START) {
        room_forward_packet(client->room_index, client->is_host, packet);
    }
}

/* Handles one fully received packet from one client. */
static void handle_client_packet(int client_index, const NetPacket* packet) {
    if (packet == NULL) {
        return;
    }

    if (packet->type == NET_MSG_PING) {
        NetPacket pong;
        memset(&pong, 0, sizeof(pong));
        pong.type = NET_MSG_PONG;
        send_packet_to_client(client_index, &pong);
        return;
    }

    if (packet->type == NET_MSG_RELAY_HOST) {
        handle_host_request(client_index, packet);
        return;
    }

    if (packet->type == NET_MSG_RELAY_JOIN) {
        handle_join_request(client_index, packet);
        return;
    }

    handle_room_packet(client_index, packet);
}

/* Finds free client slot for new accepted socket. */
static int client_find_free(void) {
    for (int i = 0; i < RELAY_MAX_CLIENTS; ++i) {
        if (!g_clients[i].used) {
            return i;
        }
    }
    return -1;
}

/* Accepts all pending incoming TCP connections. */
static void accept_pending_connections(net_socket_t listen_socket) {
    while (true) {
        struct sockaddr_storage addr;
        socklen_t addr_len = (socklen_t)sizeof(addr);
        net_socket_t accepted = accept(listen_socket, (struct sockaddr*)&addr, &addr_len);
        int slot;

        if (accepted == NET_INVALID_SOCKET) {
            if (socket_would_block()) {
                return;
            }
            return;
        }

        slot = client_find_free();
        if (slot < 0) {
            socket_close(accepted);
            continue;
        }

        if (!socket_set_nonblocking(accepted)) {
            socket_close(accepted);
            continue;
        }

        memset(&g_clients[slot], 0, sizeof(g_clients[slot]));
        g_clients[slot].used = true;
        g_clients[slot].socket_fd = accepted;
        g_clients[slot].room_index = -1;
    }
}

/* Reads all pending packets from one client socket and dispatches them. */
static void poll_client_packets(int client_index) {
    RelayClient* client;

    if (client_index < 0 || client_index >= RELAY_MAX_CLIENTS) {
        return;
    }

    client = &g_clients[client_index];
    if (!client->used) {
        return;
    }

    while (true) {
        int capacity = (int)sizeof(client->rx_buffer) - client->rx_bytes;
        int rc;

        if (capacity <= 0) {
            client_disconnect(client_index);
            return;
        }

        rc = recv(client->socket_fd, (char*)client->rx_buffer + client->rx_bytes, capacity, 0);
        if (rc > 0) {
            client->rx_bytes += rc;
            continue;
        }

        if (rc == 0) {
            client_disconnect(client_index);
            return;
        }

        if (socket_would_block()) {
            break;
        }

        client_disconnect(client_index);
        return;
    }

    while (client->used && client->rx_bytes >= (int)sizeof(NetPacket)) {
        NetPacket packet;

        memcpy(&packet, client->rx_buffer, sizeof(NetPacket));
        memmove(client->rx_buffer,
                client->rx_buffer + sizeof(NetPacket),
                (size_t)(client->rx_bytes - (int)sizeof(NetPacket)));
        client->rx_bytes -= (int)sizeof(NetPacket);

        packet.sequence = ntohl(packet.sequence);
        handle_client_packet(client_index, &packet);
    }
}

/* Drops old fully-disconnected rooms to free memory. */
static void cleanup_old_rooms(void) {
    time_t now = time(NULL);

    for (int i = 0; i < RELAY_MAX_ROOMS; ++i) {
        RelayRoom* room = &g_rooms[i];
        if (!room->used) {
            continue;
        }

        if (room->host_client < 0 &&
            room->guest_client < 0 &&
            (now - room->updated_at) > RELAY_CLEANUP_SECONDS) {
            memset(room, 0, sizeof(*room));
        }
    }
}

/* Creates one configured listener on a specific TCP port. */
static net_socket_t create_listen_socket_on_port(uint16_t port) {
    net_socket_t listen_socket;
    struct sockaddr_in addr;

    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == NET_INVALID_SOCKET) {
        return NET_INVALID_SOCKET;
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
    addr.sin_port = htons(port);

    if (bind(listen_socket, (const struct sockaddr*)&addr, (socklen_t)sizeof(addr)) == NET_SOCK_ERR) {
        socket_close(listen_socket);
        return NET_INVALID_SOCKET;
    }

    if (listen(listen_socket, 64) == NET_SOCK_ERR) {
        socket_close(listen_socket);
        return NET_INVALID_SOCKET;
    }

    if (!socket_set_nonblocking(listen_socket)) {
        socket_close(listen_socket);
        return NET_INVALID_SOCKET;
    }

    return listen_socket;
}

/* Starts listener on first free managed relay port. */
static net_socket_t create_listen_socket(void) {
    net_socket_t listen_socket;

    for (int i = 0; i < RELAY_PORT_SPAN; ++i) {
        uint16_t port = (uint16_t)(RELAY_PORT_BASE + i);
        listen_socket = create_listen_socket_on_port(port);
        if (listen_socket != NET_INVALID_SOCKET) {
            printf("Relay server listening on 0.0.0.0:%u\n", (unsigned)port);
            fflush(stdout);
            return listen_socket;
        }
    }

    fprintf(stderr, "No free relay port in managed range %d-%d.\n",
            RELAY_PORT_BASE,
            RELAY_PORT_BASE + RELAY_PORT_SPAN - 1);
    return NET_INVALID_SOCKET;
}

int main(void) {
    net_socket_t listen_socket;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }
#endif

    srand((unsigned int)time(NULL));
    memset(g_clients, 0, sizeof(g_clients));
    memset(g_rooms, 0, sizeof(g_rooms));

    for (int i = 0; i < RELAY_MAX_CLIENTS; ++i) {
        g_clients[i].room_index = -1;
    }

    listen_socket = create_listen_socket();
    if (listen_socket == NET_INVALID_SOCKET) {
        fprintf(stderr, "Failed to start relay listener socket.\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    while (true) {
        accept_pending_connections(listen_socket);

        for (int i = 0; i < RELAY_MAX_CLIENTS; ++i) {
            if (g_clients[i].used) {
                poll_client_packets(i);
            }
        }

        cleanup_old_rooms();

#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }

    socket_close(listen_socket);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
