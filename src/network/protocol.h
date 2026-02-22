#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * Compact wire protocol used by direct TCP peer transport.
 * Payload intentionally stays minimal (mostly move coordinates + metadata).
 */

#include <stdint.h>
#include "types.h"

/* Network message kinds exchanged by host and guest peers. */
typedef enum NetMsgType {
    NET_MSG_NONE = 0,
    NET_MSG_JOIN_REQUEST = 1,
    NET_MSG_JOIN_ACCEPT = 2,
    NET_MSG_JOIN_REJECT = 3,
    NET_MSG_MOVE = 4,
    NET_MSG_SYNC = 5,
    NET_MSG_ERROR = 6,
    NET_MSG_PING = 7,
    NET_MSG_PONG = 8,
    NET_MSG_LEAVE = 9,
    NET_MSG_READY = 10,
    NET_MSG_START = 11,
    NET_MSG_RELAY_HOST = 12,
    NET_MSG_RELAY_JOIN = 13,
    NET_MSG_RELAY_HOST_ACK = 14
} NetMsgType;

/* Packed message for direct TCP transfer. */
#pragma pack(push, 1)
typedef struct NetPacket {
    uint8_t type;
    uint8_t from;
    uint8_t to;
    uint8_t promotion;
    uint8_t flags;
    uint32_t sequence;
    char invite_code[INVITE_CODE_LEN + 1];
    char username[PLAYER_NAME_MAX + 1];
} NetPacket;
#pragma pack(pop)

#endif
