#include "secure_io.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#define SECURE_IO_MAGIC 0x314F5343U /* CSO1 */
#define SECURE_IO_VERSION 1U
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

static bool read_raw_file(const char* path, uint8_t** out_data, size_t* out_size) {
    FILE* file;
    long length;
    uint8_t* data = NULL;

    if (path == NULL || out_data == NULL || out_size == NULL) {
        return false;
    }

    *out_data = NULL;
    *out_size = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    if (length > 0) {
        data = (uint8_t*)malloc((size_t)length);
        if (data == NULL) {
            fclose(file);
            return false;
        }

        if (fread(data, 1, (size_t)length, file) != (size_t)length) {
            free(data);
            fclose(file);
            return false;
        }
    }

    fclose(file);
    *out_data = data;
    *out_size = (size_t)length;
    return true;
}

static bool write_raw_file(const char* path, const void* data, size_t size) {
    FILE* file;

    if (path == NULL || (data == NULL && size > 0)) {
        return false;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }

    if (size > 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

static uint8_t xor_transform_byte(uint8_t value, size_t index) {
    static const uint8_t k_key[16] = {
        0x79, 0x13, 0xE2, 0x5D, 0x40, 0xB8, 0x96, 0x2F,
        0xA1, 0xC4, 0x17, 0x6B, 0x53, 0x8D, 0xF0, 0x34
    };
    uint8_t step = (uint8_t)(index * 29U + 11U);
    return (uint8_t)(value ^ k_key[index & 15U] ^ step);
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

    if (path == NULL || (data == NULL && size > 0) || size > 0xFFFFFFFFU) {
        return false;
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
            return false;
        }

        out_payload_size = (size_t)out_blob.cbData;
        out_payload = (uint8_t*)malloc(out_payload_size);
        if (out_payload == NULL) {
            LocalFree(out_blob.pbData);
            return false;
        }
        if (out_payload_size > 0) {
            memcpy(out_payload, out_blob.pbData, out_payload_size);
        }
        LocalFree(out_blob.pbData);
        header.method = SECURE_IO_METHOD_DPAPI;
    }
#else
    out_payload_size = size;
    out_payload = (uint8_t*)malloc(out_payload_size > 0 ? out_payload_size : 1U);
    if (out_payload == NULL) {
        return false;
    }
    for (size_t i = 0; i < out_payload_size; ++i) {
        out_payload[i] = xor_transform_byte(((const uint8_t*)data)[i], i);
    }
    header.method = SECURE_IO_METHOD_XOR;
#endif

    if (out_payload_size > 0xFFFFFFFFU) {
        free(out_payload);
        return false;
    }

    header.magic = SECURE_IO_MAGIC;
    header.version = SECURE_IO_VERSION;
    header.reserved = 0U;
    header.payload_size = (uint32_t)out_payload_size;

    final_size = sizeof(header) + out_payload_size;
    final_blob = (uint8_t*)malloc(final_size > 0 ? final_size : 1U);
    if (final_blob == NULL) {
        free(out_payload);
        return false;
    }

    memcpy(final_blob, &header, sizeof(header));
    if (out_payload_size > 0) {
        memcpy(final_blob + sizeof(header), out_payload, out_payload_size);
    }

    ok = write_raw_file(path, final_blob, final_size);
    free(final_blob);
    free(out_payload);
    return ok;
}

bool secure_io_read_file(const char* path, void** out_data, size_t* out_size) {
    uint8_t* raw = NULL;
    size_t raw_size = 0;
    bool has_header = false;

    if (path == NULL || out_data == NULL || out_size == NULL) {
        return false;
    }

    *out_data = NULL;
    *out_size = 0;

    if (!read_raw_file(path, &raw, &raw_size)) {
        return false;
    }

    if (raw_size >= sizeof(SecureIoHeader)) {
        SecureIoHeader header;
        memcpy(&header, raw, sizeof(header));
        has_header = (header.magic == SECURE_IO_MAGIC &&
                      header.version == SECURE_IO_VERSION &&
                      sizeof(SecureIoHeader) + (size_t)header.payload_size <= raw_size);

        if (has_header) {
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
                    return false;
                }

                out_copy = (uint8_t*)malloc((size_t)out_blob.cbData > 0 ? (size_t)out_blob.cbData : 1U);
                if (out_copy == NULL) {
                    LocalFree(out_blob.pbData);
                    free(raw);
                    return false;
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
                uint8_t* out_copy = (uint8_t*)malloc(payload_size > 0 ? payload_size : 1U);
                if (out_copy == NULL) {
                    free(raw);
                    return false;
                }

                for (size_t i = 0; i < payload_size; ++i) {
                    out_copy[i] = xor_transform_byte(payload[i], i);
                }

                *out_data = out_copy;
                *out_size = payload_size;
                free(raw);
                return true;
            }

            free(raw);
            return false;
        }
    }

    *out_data = raw;
    *out_size = raw_size;
    return true;
}
