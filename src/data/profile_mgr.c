#include "profile_mgr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Loads profile data from key=value text file. */
bool profile_load(Profile* out_profile, const char* path) {
    FILE* file;
    Profile temp;
    char line[128];

    if (out_profile == NULL || path == NULL) {
        return false;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }

    memset(&temp, 0, sizeof(temp));

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "username=", 9) == 0) {
            char* value = line + 9;
            value[strcspn(value, "\r\n")] = '\0';
            strncpy(temp.username, value, PLAYER_NAME_MAX);
            temp.username[PLAYER_NAME_MAX] = '\0';
        } else if (strncmp(line, "wins=", 5) == 0) {
            temp.wins = (uint32_t)strtoul(line + 5, NULL, 10);
        } else if (strncmp(line, "losses=", 7) == 0) {
            temp.losses = (uint32_t)strtoul(line + 7, NULL, 10);
        }
    }

    fclose(file);

    if (temp.username[0] == '\0') {
        strncpy(temp.username, "Player", PLAYER_NAME_MAX);
        temp.username[PLAYER_NAME_MAX] = '\0';
    }

    *out_profile = temp;
    return true;
}

/* Saves profile data to key=value text file. */
bool profile_save(const Profile* profile, const char* path) {
    FILE* file;

    if (profile == NULL || path == NULL) {
        return false;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }

    fprintf(file, "username=%s\n", profile->username);
    fprintf(file, "wins=%u\n", profile->wins);
    fprintf(file, "losses=%u\n", profile->losses);

    fclose(file);
    return true;
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
