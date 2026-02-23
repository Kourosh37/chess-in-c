#include "audio.h"

#include <stdio.h>
#include <string.h>

#include <raylib.h>

typedef struct AudioSlot {
    const char* filename;
    Sound sound;
    bool loaded;
} AudioSlot;

static AudioSlot g_slots[AUDIO_SFX_COUNT] = {
    {.filename = "ui_click.wav", .sound = {{0}}, .loaded = false},
    {.filename = "piece_move.wav", .sound = {{0}}, .loaded = false},
    {.filename = "piece_capture.wav", .sound = {{0}}, .loaded = false},
    {.filename = "piece_castle.wav", .sound = {{0}}, .loaded = false},
    {.filename = "piece_promotion.wav", .sound = {{0}}, .loaded = false},
    {.filename = "king_check.wav", .sound = {{0}}, .loaded = false},
    {.filename = "game_over.wav", .sound = {{0}}, .loaded = false},
    {.filename = "lobby_join.wav", .sound = {{0}}, .loaded = false},
    {.filename = "game_victory.wav", .sound = {{0}}, .loaded = false},
    {.filename = "piece_select.wav", .sound = {{0}}, .loaded = false}
};

static const char* g_menu_music_candidates[] = {
    "menu_bgm.ogg",
    "menu_bgm.mp3",
    "menu_bgm.wav"
};

static const char* g_game_music_candidates[] = {
    "game_bgm.ogg",
    "game_bgm.mp3",
    "game_bgm.wav"
};

static bool g_audio_initialized = false;
static bool g_audio_enabled = true;

static float g_sfx_volume = 1.0f;
static float g_menu_music_volume = 0.55f;
static float g_game_music_volume = 0.55f;

static Music g_menu_music = {0};
static bool g_menu_music_loaded = false;
static bool g_menu_music_active = false;
static bool g_menu_music_paused = false;
static const char* g_menu_music_loaded_name = NULL;

static Music g_game_music = {0};
static bool g_game_music_loaded = false;
static bool g_game_music_active = false;
static bool g_game_music_paused = false;
static const char* g_game_music_loaded_name = NULL;

/* Clamps a value to the [0, 1] interval. */
static float clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

/* Applies SFX volume to all loaded one-shot sounds. */
static void apply_sfx_volume(void) {
    for (int i = 0; i < AUDIO_SFX_COUNT; ++i) {
        if (g_slots[i].loaded) {
            SetSoundVolume(g_slots[i].sound, g_sfx_volume);
        }
    }
}

/* Applies menu music volume on current stream. */
static void apply_menu_music_volume(void) {
    if (!g_menu_music_loaded) {
        return;
    }
    SetMusicVolume(g_menu_music, g_menu_music_volume);
}

/* Applies in-game music volume on current stream. */
static void apply_game_music_volume(void) {
    if (!g_game_music_loaded) {
        return;
    }
    SetMusicVolume(g_game_music, g_game_music_volume);
}

/* Updates menu music play/pause state based on active screen and mute state. */
static void refresh_menu_music_state(void) {
    if (!g_menu_music_loaded) {
        return;
    }

    if (!g_audio_enabled || !g_menu_music_active) {
        if (!g_menu_music_paused && IsMusicStreamPlaying(g_menu_music)) {
            PauseMusicStream(g_menu_music);
            g_menu_music_paused = true;
        }
        return;
    }

    apply_menu_music_volume();

    if (g_menu_music_paused) {
        ResumeMusicStream(g_menu_music);
        g_menu_music_paused = false;
        return;
    }

    if (!IsMusicStreamPlaying(g_menu_music)) {
        PlayMusicStream(g_menu_music);
    }
}

/* Updates in-game music play/pause state based on active screen and mute state. */
static void refresh_game_music_state(void) {
    if (!g_game_music_loaded) {
        return;
    }

    if (!g_audio_enabled || !g_game_music_active) {
        if (!g_game_music_paused && IsMusicStreamPlaying(g_game_music)) {
            PauseMusicStream(g_game_music);
            g_game_music_paused = true;
        }
        return;
    }

    apply_game_music_volume();

    if (g_game_music_paused) {
        ResumeMusicStream(g_game_music);
        g_game_music_paused = false;
        return;
    }

    if (!IsMusicStreamPlaying(g_game_music)) {
        PlayMusicStream(g_game_music);
    }
}

/* Loads one optional music stream by trying candidate filenames in order. */
static void load_music_candidates(const char* const* candidates,
                                  int candidate_count,
                                  Music* out_music,
                                  bool* out_loaded,
                                  const char** out_loaded_name) {
    char path[260];

    *out_loaded = false;
    *out_loaded_name = NULL;
    memset(out_music, 0, sizeof(*out_music));

    for (int i = 0; i < candidate_count; ++i) {
        const char* name = candidates[i];
        Music music;

        snprintf(path, sizeof(path), "assets/sfx/%s", name);
        if (!FileExists(path)) {
            continue;
        }

        music = LoadMusicStream(path);
        if (music.frameCount > 0) {
            *out_music = music;
            *out_loaded = true;
            *out_loaded_name = name;
            return;
        }
    }
}

/* Loads one sound effect from assets/sfx if available. */
static void load_slot(AudioSfx sfx) {
    char path[260];
    AudioSlot* slot;

    if (sfx < 0 || sfx >= AUDIO_SFX_COUNT) {
        return;
    }

    slot = &g_slots[sfx];
    snprintf(path, sizeof(path), "assets/sfx/%s", slot->filename);

    if (!FileExists(path)) {
        slot->loaded = false;
        return;
    }

    slot->sound = LoadSound(path);
    slot->loaded = (slot->sound.frameCount > 0);
}

bool audio_init(void) {
    if (g_audio_initialized) {
        return true;
    }

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        return false;
    }

    for (int i = 0; i < AUDIO_SFX_COUNT; ++i) {
        load_slot((AudioSfx)i);
    }

    load_music_candidates(g_menu_music_candidates,
                          (int)(sizeof(g_menu_music_candidates) / sizeof(g_menu_music_candidates[0])),
                          &g_menu_music,
                          &g_menu_music_loaded,
                          &g_menu_music_loaded_name);

    load_music_candidates(g_game_music_candidates,
                          (int)(sizeof(g_game_music_candidates) / sizeof(g_game_music_candidates[0])),
                          &g_game_music,
                          &g_game_music_loaded,
                          &g_game_music_loaded_name);

    apply_sfx_volume();
    apply_menu_music_volume();
    apply_game_music_volume();

    g_audio_initialized = true;
    return true;
}

void audio_shutdown(void) {
    if (!g_audio_initialized) {
        return;
    }

    for (int i = 0; i < AUDIO_SFX_COUNT; ++i) {
        if (g_slots[i].loaded) {
            UnloadSound(g_slots[i].sound);
            g_slots[i].loaded = false;
        }
    }

    if (g_menu_music_loaded) {
        if (IsMusicStreamPlaying(g_menu_music)) {
            StopMusicStream(g_menu_music);
        }
        UnloadMusicStream(g_menu_music);
        g_menu_music_loaded = false;
        g_menu_music_active = false;
        g_menu_music_paused = false;
        g_menu_music_loaded_name = NULL;
        memset(&g_menu_music, 0, sizeof(g_menu_music));
    }

    if (g_game_music_loaded) {
        if (IsMusicStreamPlaying(g_game_music)) {
            StopMusicStream(g_game_music);
        }
        UnloadMusicStream(g_game_music);
        g_game_music_loaded = false;
        g_game_music_active = false;
        g_game_music_paused = false;
        g_game_music_loaded_name = NULL;
        memset(&g_game_music, 0, sizeof(g_game_music));
    }

    CloseAudioDevice();
    g_audio_initialized = false;
}

void audio_set_enabled(bool enabled) {
    g_audio_enabled = enabled;
    refresh_menu_music_state();
    refresh_game_music_state();
}

bool audio_is_enabled(void) {
    return g_audio_enabled;
}

void audio_set_sfx_volume(float volume) {
    g_sfx_volume = clamp01(volume);
    apply_sfx_volume();
}

float audio_get_sfx_volume(void) {
    return g_sfx_volume;
}

void audio_set_menu_music_volume(float volume) {
    g_menu_music_volume = clamp01(volume);
    apply_menu_music_volume();
}

float audio_get_menu_music_volume(void) {
    return g_menu_music_volume;
}

void audio_set_game_music_volume(float volume) {
    g_game_music_volume = clamp01(volume);
    apply_game_music_volume();
}

float audio_get_game_music_volume(void) {
    return g_game_music_volume;
}

bool audio_is_loaded(AudioSfx sfx) {
    if (sfx < 0 || sfx >= AUDIO_SFX_COUNT) {
        return false;
    }
    return g_slots[sfx].loaded;
}

const char* audio_expected_filename(AudioSfx sfx) {
    if (sfx < 0 || sfx >= AUDIO_SFX_COUNT) {
        return "";
    }
    return g_slots[sfx].filename;
}

void audio_play(AudioSfx sfx) {
    if (!g_audio_initialized || !g_audio_enabled) {
        return;
    }

    if (sfx < 0 || sfx >= AUDIO_SFX_COUNT) {
        return;
    }

    if (!g_slots[sfx].loaded) {
        return;
    }

    PlaySound(g_slots[sfx].sound);
}

void audio_set_menu_music_active(bool active) {
    g_menu_music_active = active;
    refresh_menu_music_state();
}

bool audio_is_menu_music_loaded(void) {
    return g_menu_music_loaded;
}

const char* audio_menu_music_expected_filename(void) {
    if (g_menu_music_loaded_name != NULL) {
        return g_menu_music_loaded_name;
    }
    return g_menu_music_candidates[0];
}

void audio_set_game_music_active(bool active) {
    g_game_music_active = active;
    refresh_game_music_state();
}

bool audio_is_game_music_loaded(void) {
    return g_game_music_loaded;
}

const char* audio_game_music_expected_filename(void) {
    if (g_game_music_loaded_name != NULL) {
        return g_game_music_loaded_name;
    }
    return g_game_music_candidates[0];
}

void audio_update(void) {
    if (!g_audio_initialized) {
        return;
    }

    if (g_audio_enabled && g_menu_music_loaded && g_menu_music_active) {
        UpdateMusicStream(g_menu_music);
    }
    if (g_audio_enabled && g_game_music_loaded && g_game_music_active) {
        UpdateMusicStream(g_game_music);
    }
}
