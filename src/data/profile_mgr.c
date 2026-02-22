#include "profile_mgr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "secure_io.h"

/* Loads profile data from encrypted key=value text file. */
bool profile_load(Profile* out_profile, const char* path) {
    void* raw_data = NULL;
    size_t raw_size = 0;
    char* text = NULL;
    char* line;
    Profile temp;

    if (out_profile == NULL || path == NULL) {
        return false;
    }

    if (!secure_io_read_file(path, &raw_data, &raw_size)) {
        return false;
    }

    text = (char*)malloc(raw_size + 1U);
    if (text == NULL) {
        secure_io_free(raw_data);
        return false;
    }

    if (raw_size > 0U && raw_data != NULL) {
        memcpy(text, raw_data, raw_size);
    }
    text[raw_size] = '\0';
    secure_io_free(raw_data);

    memset(&temp, 0, sizeof(temp));

    line = strtok(text, "\r\n");
    while (line != NULL) {
        if (strncmp(line, "username=", 9) == 0) {
            const char* value = line + 9;
            strncpy(temp.username, value, PLAYER_NAME_MAX);
            temp.username[PLAYER_NAME_MAX] = '\0';
        } else if (strncmp(line, "wins=", 5) == 0) {
            temp.wins = (uint32_t)strtoul(line + 5, NULL, 10);
        } else if (strncmp(line, "losses=", 7) == 0) {
            temp.losses = (uint32_t)strtoul(line + 7, NULL, 10);
        }
        line = strtok(NULL, "\r\n");
    }

    free(text);

    if (temp.username[0] == '\0') {
        strncpy(temp.username, "Player", PLAYER_NAME_MAX);
        temp.username[PLAYER_NAME_MAX] = '\0';
    }

    *out_profile = temp;
    return true;
}

/* Saves profile data to encrypted key=value text file. */
bool profile_save(const Profile* profile, const char* path) {
    char payload[256];
    int written;

    if (profile == NULL || path == NULL) {
        return false;
    }

    written = snprintf(payload,
                       sizeof(payload),
                       "username=%s\nwins=%u\nlosses=%u\n",
                       profile->username,
                       profile->wins,
                       profile->losses);
    if (written < 0 || (size_t)written >= sizeof(payload)) {
        return false;
    }

    return secure_io_write_file(path, payload, (size_t)written);
}

/* Updates aggregate win/loss counters. */
void profile_record_result(Profile* profile, bool won) {
    if (profile == NULL) {
        return;
    }

    if (won) {
        profile->wins++;
    } else {
        profile->losses++;
    }
}
