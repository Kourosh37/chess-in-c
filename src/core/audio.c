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
    {.filename = "lobby_join.wav", .sound = {{0}}, .loaded = false}
};

static bool g_audio_initialized = false;
static bool g_audio_enabled = true;
static float g_master_volume = 1.0f;

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
    if (slot->loaded) {
        SetSoundVolume(slot->sound, g_master_volume);
    }
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

    CloseAudioDevice();
    g_audio_initialized = false;
}

void audio_set_enabled(bool enabled) {
    g_audio_enabled = enabled;
}

bool audio_is_enabled(void) {
    return g_audio_enabled;
}

void audio_set_master_volume(float volume) {
    g_master_volume = clamp01(volume);

    for (int i = 0; i < AUDIO_SFX_COUNT; ++i) {
        if (g_slots[i].loaded) {
            SetSoundVolume(g_slots[i].sound, g_master_volume);
        }
    }
}

float audio_get_master_volume(void) {
    return g_master_volume;
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
