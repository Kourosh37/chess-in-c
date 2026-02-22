#include "network.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Base32 alphabet without visually ambiguous characters. */
static const char* CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

/* Returns alphabet index for one uppercase symbol, or -1 if invalid. */
static int alphabet_index(char ch) {
    for (int i = 0; i < 32; ++i) {
        if (CODE_ALPHABET[i] == ch) {
            return i;
        }
    }
    return -1;
}

/* Generates random invite code in project alphabet. */
void matchmaker_generate_code(char out_code[INVITE_CODE_LEN + 1]) {
    static bool seeded = false;

    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }

    for (int i = 0; i < INVITE_CODE_LEN; ++i) {
        out_code[i] = CODE_ALPHABET[rand() % 32];
    }
    out_code[INVITE_CODE_LEN] = '\0';
}

/* Validates invite code length and alphabet membership. */
bool matchmaker_is_valid_code(const char* code) {
    if (code == NULL || (int)strlen(code) != INVITE_CODE_LEN) {
        return false;
    }

    for (int i = 0; i < INVITE_CODE_LEN; ++i) {
        int idx = alphabet_index((char)toupper((unsigned char)code[i]));
        if (idx < 0) {
            return false;
        }
    }

    return true;
}

/* Encodes endpoint (IPv4 + port) into fixed-length base32 invite code. */
bool matchmaker_encode_endpoint(uint32_t ipv4_be, uint16_t port, char out_code[INVITE_CODE_LEN + 1]) {
    uint64_t packed;

    if (out_code == NULL) {
        return false;
    }

    packed = ((uint64_t)ipv4_be << 16) | (uint64_t)port;

    for (int i = INVITE_CODE_LEN - 1; i >= 0; --i) {
        out_code[i] = CODE_ALPHABET[packed & 31ULL];
        packed >>= 5U;
    }

    out_code[INVITE_CODE_LEN] = '\0';
    return true;
}

/* Decodes endpoint (IPv4 + port) from fixed-length base32 invite code. */
bool matchmaker_decode_endpoint(const char* code, uint32_t* out_ipv4_be, uint16_t* out_port) {
    uint64_t packed = 0ULL;

    if (!matchmaker_is_valid_code(code) || out_ipv4_be == NULL || out_port == NULL) {
        return false;
    }

    for (int i = 0; i < INVITE_CODE_LEN; ++i) {
        int value = alphabet_index((char)toupper((unsigned char)code[i]));
        if (value < 0) {
            return false;
        }

        packed = (packed << 5U) | (uint64_t)value;
    }

    /* Invite code currently stores 48 bits: 32-bit IPv4 + 16-bit port. */
    packed &= 0x0000FFFFFFFFFFFFULL;
    *out_ipv4_be = (uint32_t)((packed >> 16U) & 0xFFFFFFFFULL);
    *out_port = (uint16_t)(packed & 0xFFFFULL);
    return true;
}
