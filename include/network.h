#ifndef NETWORK_H
#define NETWORK_H

/*
 * Direct peer-to-peer network API.
 * One client acts as host (listening socket) and one as guest (join request).
 * There is no standalone central server process in this architecture.
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
    uint8_t peer_addr_storage[128];
    int peer_addr_len;
    uint32_t sequence;
    bool initialized;
    bool connected;
    bool is_host;
    char invite_code[INVITE_CODE_LEN + 1];
} NetworkClient;

/* Socket lifecycle. */
bool network_client_init(NetworkClient* client, uint16_t listen_port);
void network_client_shutdown(NetworkClient* client);

/* Session setup and move exchange. */
bool network_client_host(NetworkClient* client, const char* username, char out_code[INVITE_CODE_LEN + 1]);
bool network_client_join(NetworkClient* client, const char* username, const char* invite_code);
bool network_client_send_move(NetworkClient* client, Move move);
bool network_client_send_leave(NetworkClient* client);
bool network_client_poll(NetworkClient* client, NetPacket* out_packet);

/* Invite-code utilities. */
void matchmaker_generate_code(char out_code[INVITE_CODE_LEN + 1]);
bool matchmaker_is_valid_code(const char* code);
bool matchmaker_encode_endpoint(uint32_t ipv4_be, uint16_t port, char out_code[INVITE_CODE_LEN + 1]);
bool matchmaker_decode_endpoint(const char* code, uint32_t* out_ipv4_be, uint16_t* out_port);

#ifdef __cplusplus
}
#endif

#endif
