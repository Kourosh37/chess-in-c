#include "secure_io.h"
#include "file_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#define SECURE_IO_MAGIC 0x314F5343U /* CSO1 */
#define SECURE_IO_VERSION 2U
#define SECURE_IO_METHOD_DPAPI 1U
#define SECURE_IO_METHOD_XOR 2U

#pragma pack(push, 1)
typedef struct SecureIoHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t method;
    uint16_t reserved;
    uint32_t payload_size;
} SecureIoHeader;
#pragma pack(pop)

static char g_error[256];

const char* secure_io_last_error(void) { return g_error; }

static bool invalid(const char* message) {
    snprintf(g_error, sizeof(g_error), "%s", message);
    return false;
}

static uint8_t xor_transform_byte(uint8_t value, size_t index) {
    static const uint8_t k_key[16] = {
        0x79, 0x13, 0xE2, 0x5D, 0x40, 0xB8, 0x96, 0x2F,
        0xA1, 0xC4, 0x17, 0x6B, 0x53, 0x8D, 0xF0, 0x34
    };
    uint8_t step = (uint8_t)(index * 29U + 11U);
    return (uint8_t)(value ^ k_key[index & 15U] ^ step);
}

/* Detect accidental corruption in the non-Windows obfuscation format. */
static uint32_t checksum(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

void secure_io_free(void* data) {
    free(data);
}

bool secure_io_write_file(const char* path, const void* data, size_t size) {
    SecureIoHeader header;
    uint8_t* out_payload = NULL;
    size_t out_payload_size = 0;
    uint8_t* final_blob = NULL;
    size_t final_size;
    bool ok;

    g_error[0] = '\0';
    if (path == NULL || (data == NULL && size > 0) || size > 16U * 1024U * 1024U) {
        return invalid("Could not allocate or encode settings data.");
    }

#ifdef _WIN32
    {
        DATA_BLOB in_blob;
        DATA_BLOB out_blob;

        in_blob.cbData = (DWORD)size;
        in_blob.pbData = (BYTE*)data;

        if (!CryptProtectData(&in_blob,
                              L"Chess Secure Data",
                              NULL,
                              NULL,
                              NULL,
                              CRYPTPROTECT_UI_FORBIDDEN,
                              &out_blob)) {
            return invalid("Windows could not encrypt the settings.");
        }

        out_payload_size = (size_t)out_blob.cbData;
        out_payload = (uint8_t*)malloc(out_payload_size);
        if (out_payload == NULL) {
            LocalFree(out_blob.pbData);
            return invalid("Could not allocate or encode settings data.");
        }
        if (out_payload_size > 0) {
            memcpy(out_payload, out_blob.pbData, out_payload_size);
        }
        LocalFree(out_blob.pbData);
        header.method = SECURE_IO_METHOD_DPAPI;
    }
#else
    out_payload_size = size + sizeof(uint32_t);
    out_payload = (uint8_t*)malloc(out_payload_size > 0 ? out_payload_size : 1U);
    if (out_payload == NULL) {
        return invalid("Could not allocate or encode settings data.");
    }
    for (size_t i = 0; i < size; ++i) {
        out_payload[i] = xor_transform_byte(((const uint8_t*)data)[i], i);
    }
    {
        uint32_t crc = checksum(data, size);
        memcpy(out_payload + size, &crc, sizeof(crc));
    }
    header.method = SECURE_IO_METHOD_XOR;
#endif

    if (out_payload_size > 16U * 1024U * 1024U) {
        free(out_payload);
        return invalid("Could not allocate or encode settings data.");
    }

    header.magic = SECURE_IO_MAGIC;
    header.version = SECURE_IO_VERSION;
    header.reserved = 0U;
    header.payload_size = (uint32_t)out_payload_size;

    final_size = sizeof(header) + out_payload_size;
    final_blob = (uint8_t*)malloc(final_size > 0 ? final_size : 1U);
    if (final_blob == NULL) {
        free(out_payload);
        return invalid("Could not allocate or encode settings data.");
    }

    memcpy(final_blob, &header, sizeof(header));
    if (out_payload_size > 0) {
        memcpy(final_blob + sizeof(header), out_payload, out_payload_size);
    }

    ok = file_io_write_atomic(path, final_blob, final_size);
    if (!ok) invalid(file_io_last_error());
    free(final_blob);
    free(out_payload);
    return ok;
}

static bool decode_container(uint8_t* raw, size_t raw_size, void** out_data, size_t* out_size) {
    uint32_t magic = 0;

    *out_data = NULL;
    *out_size = 0;

    if (raw_size >= sizeof(magic)) memcpy(&magic, raw, sizeof(magic));
    if (magic == SECURE_IO_MAGIC) {
        SecureIoHeader header;
        if (raw_size < sizeof(header)) {
            free(raw);
            return invalid("Settings container is truncated.");
        }
        memcpy(&header, raw, sizeof(header));
        if ((header.version != 1U && header.version != SECURE_IO_VERSION) || header.reserved != 0 ||
            (size_t)header.payload_size != raw_size - sizeof(header)) {
            free(raw);
            return invalid("Settings container has an invalid header or length.");
        }

        {
            const uint8_t* payload = raw + sizeof(SecureIoHeader);
            size_t payload_size = (size_t)header.payload_size;

#ifdef _WIN32
            if (header.method == SECURE_IO_METHOD_DPAPI) {
                DATA_BLOB in_blob;
                DATA_BLOB out_blob;
                uint8_t* out_copy;

                in_blob.cbData = (DWORD)payload_size;
                in_blob.pbData = (BYTE*)payload;

                if (!CryptUnprotectData(&in_blob,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        CRYPTPROTECT_UI_FORBIDDEN,
                                        &out_blob)) {
                    free(raw);
                    return invalid("Windows could not decrypt these settings for the current user.");
                }

                out_copy = (uint8_t*)malloc((size_t)out_blob.cbData > 0 ? (size_t)out_blob.cbData : 1U);
                if (out_copy == NULL) {
                    LocalFree(out_blob.pbData);
                    free(raw);
                    return invalid("Could not allocate or encode settings data.");
                }

                if (out_blob.cbData > 0) {
                    memcpy(out_copy, out_blob.pbData, (size_t)out_blob.cbData);
                }

                LocalFree(out_blob.pbData);
                *out_data = out_copy;
                *out_size = (size_t)out_blob.cbData;
                free(raw);
                return true;
            }
#endif

            if (header.method == SECURE_IO_METHOD_XOR) {
                size_t decoded_size = payload_size;
                uint8_t* out_copy;
                if (header.version >= 2U) {
                    if (payload_size < sizeof(uint32_t)) { free(raw); return invalid("Settings checksum is missing."); }
                    decoded_size -= sizeof(uint32_t);
                }
                out_copy = (uint8_t*)malloc(decoded_size > 0 ? decoded_size : 1U);
                if (out_copy == NULL) {
                    free(raw);
                    return invalid("Could not allocate or encode settings data.");
                }

                for (size_t i = 0; i < decoded_size; ++i) {
                    out_copy[i] = xor_transform_byte(payload[i], i);
                }

                if (header.version >= 2U) {
                    uint32_t expected;
                    memcpy(&expected, payload + decoded_size, sizeof(expected));
                    if (checksum(out_copy, decoded_size) != expected) {
                        free(out_copy);
                        free(raw);
                        return invalid("Settings checksum does not match.");
                    }
                }
                *out_data = out_copy;
                *out_size = decoded_size;
                free(raw);
                return true;
            }

            free(raw);
            return invalid("Settings encryption method is not supported.");
        }
    }

    *out_data = raw;
    *out_size = raw_size;
    return true;
}

/* Older migration code sometimes encrypted an already encrypted file. */
bool secure_io_read_file(const char* path, void** out_data, size_t* out_size) {
    uint8_t* raw = NULL;
    size_t raw_size = 0;
    g_error[0] = '\0';
    if (path == NULL || out_data == NULL || out_size == NULL) return invalid("Invalid settings read request.");
    *out_data = NULL;
    *out_size = 0;
    if (!file_io_read(path, (void**)&raw, &raw_size)) return invalid(file_io_last_error());
    for (int layer = 0; layer < 4; ++layer) {
        uint32_t magic = 0;
        if (!decode_container(raw, raw_size, out_data, out_size)) return false;
        if (*out_size >= sizeof(magic)) memcpy(&magic, *out_data, sizeof(magic));
        if (magic != SECURE_IO_MAGIC) return true;
        raw = *out_data;
        raw_size = *out_size;
        *out_data = NULL;
        *out_size = 0;
    }
    free(raw);
    return invalid("Too many nested settings containers.");
}
