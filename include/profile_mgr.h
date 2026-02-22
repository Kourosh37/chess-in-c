#ifndef PROFILE_MGR_H
#define PROFILE_MGR_H

/*
 * Lightweight profile persistence API.
 * Stores username and win/loss counters in a simple local text file.
 */

#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load/save profile content to a file path. */
bool profile_load(Profile* out_profile, const char* path);
bool profile_save(const Profile* profile, const char* path);

/* Increment profile counters based on match result. */
void profile_record_result(Profile* profile, bool won);

#ifdef __cplusplus
}
#endif

#endif
