#ifndef NETWORK_H
#define NETWORK_H

/*
 * TCP relay-based network API.
 * Both host and guest connect to one relay server and exchange game packets
 * through that server, so they do not need to share the same LAN.
 */

#include <stdbool.h>
#include <stdint.h>
#include "types.h"
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime socket and peer tracking state. */
typedef struct NetworkClient {
    intptr_t socket_handle;
    uint32_t sequence;
    bool initialized;
    bool relay_connected;
    bool connected;
    bool is_host;
    Side host_side;
    char invite_code[INVITE_CODE_LEN + 1];
    char local_username[PLAYER_NAME_MAX + 1];
    char peer_username[PLAYER_NAME_MAX + 1];
    uint8_t rx_buffer[sizeof(NetPacket) * 8];
    int rx_bytes;
    bool has_pending_packet;
    NetPacket pending_packet;
} NetworkClient;

/* Socket lifecycle. */
bool network_client_init(NetworkClient* client, uint16_t listen_port);
void network_client_shutdown(NetworkClient* client);

/* Session setup and move exchange. */
bool network_client_host(NetworkClient* client, const char* username, char out_code[INVITE_CODE_LEN + 1]);
bool network_client_host_reconnect(NetworkClient* client, const char* username, const char* invite_code);
bool network_client_join(NetworkClient* client, const char* username, const char* invite_code);
bool network_client_send_move(NetworkClient* client, Move move);
bool network_client_send_leave(NetworkClient* client);
bool network_client_send_ready(NetworkClient* client, bool ready);
bool network_client_send_start(NetworkClient* client);
bool network_client_poll(NetworkClient* client, NetPacket* out_packet);
bool network_relay_probe(void);
const char* network_last_error(void);

/* Invite-code utilities. */
void matchmaker_generate_code(char out_code[INVITE_CODE_LEN + 1]);
bool matchmaker_is_valid_code(const char* code);
bool matchmaker_encode_endpoint(uint32_t ipv4_be, uint16_t port, char out_code[INVITE_CODE_LEN + 1]);
bool matchmaker_decode_endpoint(const char* code, uint32_t* out_ipv4_be, uint16_t* out_port);

#ifdef __cplusplus
}
#endif

#endif
