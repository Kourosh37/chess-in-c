#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AudioSfx {
    AUDIO_SFX_UI_CLICK = 0,
    AUDIO_SFX_MOVE = 1,
    AUDIO_SFX_CAPTURE = 2,
    AUDIO_SFX_CASTLE = 3,
    AUDIO_SFX_PROMOTION = 4,
    AUDIO_SFX_CHECK = 5,
    AUDIO_SFX_GAME_OVER = 6,
    AUDIO_SFX_LOBBY_JOIN = 7,
    AUDIO_SFX_COUNT = 8
} AudioSfx;

bool audio_init(void);
void audio_shutdown(void);

void audio_set_enabled(bool enabled);
bool audio_is_enabled(void);

void audio_set_master_volume(float volume);
float audio_get_master_volume(void);

bool audio_is_loaded(AudioSfx sfx);
const char* audio_expected_filename(AudioSfx sfx);
void audio_play(AudioSfx sfx);

void audio_set_menu_music_active(bool active);
bool audio_is_menu_music_loaded(void);
const char* audio_menu_music_expected_filename(void);
void audio_update(void);

#ifdef __cplusplus
}
#endif

#endif
