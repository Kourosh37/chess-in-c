#include "app_storage.h"
#include "file_io.h"
#include "game_state.h"
#include "secure_io.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); failures++; } } while (0)

/* Core settings tests deliberately have no graphics/audio device dependency. */
void audio_play(AudioSfx sound) { (void)sound; }
bool audio_is_loaded(AudioSfx sound) { (void)sound; return false; }

static void customize(ChessApp* app) {
    app->theme = THEME_OCEAN;
    app_set_ai_difficulty(app, 73);
    app->touch_move_enabled = true;
    app->turn_timer_enabled = true;
    app->turn_time_seconds = 120;
    app->sound_enabled = false;
    app->sfx_volume = 0.123f;
    app->menu_music_volume = 0.456f;
    app->game_music_volume = 0.789f;
    strcpy(app->profile.username, "Persistent Player");
    app->profile.wins = 17;
    app->profile.losses = 9;
}

static void check_custom(const ChessApp* app, int theme) {
    CHECK((int)app->theme == theme);
    CHECK(app->ai_difficulty == 73);
    CHECK(app->touch_move_enabled);
    CHECK(app->turn_timer_enabled && app->turn_time_seconds == 120);
    CHECK(!app->sound_enabled);
    CHECK(fabsf(app->sfx_volume - 0.123f) < 0.001f);
    CHECK(fabsf(app->menu_music_volume - 0.456f) < 0.001f);
    CHECK(fabsf(app->game_music_volume - 0.789f) < 0.001f);
    CHECK(strcmp(app->profile.username, "Persistent Player") == 0);
    CHECK(app->profile.wins == 17 && app->profile.losses == 9);
    CHECK(!app->settings_load_failed);
}

static char* read_text(const char* path) {
    void* data = NULL;
    size_t size = 0;
    char* text;
    if (!secure_io_read_file(path, &data, &size)) { CHECK(false); return NULL; }
    text = malloc(size + 1);
    if (text != NULL) { memcpy(text, data, size); text[size] = '\0'; }
    else CHECK(false);
    secure_io_free(data);
    return text;
}

int main(int argc, char** argv) {
    static ChessApp app;
    const char* mode;
    const char* path;
    char dir[CHESS_PATH_MAX];
    char fixture[CHESS_PATH_MAX];
    char backup[CHESS_PATH_MAX];
    if (argc < 2) return 2;
    mode = argv[1];
    if (strcmp(mode, "default_path") == 0) {
        path = app_storage_settings_path();
        CHECK(file_io_absolute(path));
        CHECK(strstr(path, "settings.dat") != NULL);
        CHECK(file_io_executable_directory(dir));
        CHECK(strncmp(path, dir, strlen(dir)) != 0);
    } else if (strcmp(mode, "invalid_directory") == 0 || strcmp(mode, "unreadable") == 0) {
        app_init(&app);
        CHECK(app.settings_load_failed);
        CHECK(app.settings_status[0] != '\0');
        CHECK(!app_save_settings(&app));
    } else if (strcmp(mode, "migrate_plain") == 0 || strcmp(mode, "migrate_bom") == 0 || strcmp(mode, "migrate_encrypted") == 0 || strcmp(mode, "migrate_nested") == 0) {
        const char* content = "theme=2\nai_difficulty=73\nsfx_volume=0.123\n";
        CHECK(file_io_working_directory(dir));
        CHECK(file_io_join(fixture, sizeof(fixture), dir, "settings.dat"));
        if (strcmp(mode, "migrate_plain") == 0)
            CHECK(file_io_write_atomic(fixture, content, strlen(content)));
        else if (strcmp(mode, "migrate_bom") == 0) {
            char with_bom[256];
            snprintf(with_bom, sizeof(with_bom), "\xEF\xBB\xBF%s", content);
            CHECK(file_io_write_atomic(fixture, with_bom, strlen(with_bom)));
        } else CHECK(secure_io_write_file(fixture, content, strlen(content)));
        if (strcmp(mode, "migrate_nested") == 0) {
            void* wrapped = NULL;
            size_t wrapped_size = 0;
            CHECK(file_io_read(fixture, &wrapped, &wrapped_size));
            CHECK(secure_io_write_file(fixture, wrapped, wrapped_size));
            free(wrapped);
        }
        app_init(&app);
        CHECK(app.theme == THEME_OCEAN && app.ai_difficulty == 73);
        CHECK(!app.settings_load_failed);
        CHECK(file_io_path_status(app_storage_settings_path()) == 1);
        char* migrated = read_text(app_storage_settings_path());
        CHECK(migrated != NULL && strcmp(migrated, content) == 0);
        free(migrated);
        CHECK(file_io_path_status(fixture) == 1);
    } else if (strcmp(mode, "malformed_numbers") == 0) {
        const char* content = "theme=2\nai_difficulty=99999999999999999999999\nsfx_volume=nan\nmenu_music_volume=inf\nsound_volume=invalid\nwins=-1\nlosses=4294967296\n";
        CHECK(file_io_working_directory(dir));
        CHECK(file_io_join(fixture, sizeof(fixture), dir, "settings.dat"));
        CHECK(file_io_write_atomic(fixture, content, strlen(content)));
        app_init(&app);
        CHECK(app.theme == THEME_OCEAN && app.ai_difficulty == 60);
        CHECK(isfinite(app.sfx_volume) && app.sfx_volume == 1.0f);
        CHECK(isfinite(app.menu_music_volume) && app.menu_music_volume == 0.55f);
        CHECK(app.game_music_volume == 0.55f);
        CHECK(app.profile.wins == 0 && app.profile.losses == 0);
    } else {
        path = app_storage_settings_path();
        CHECK(path[0] != '\0');
        if (strcmp(mode, "write") == 0) {
            app_init(&app);
            CHECK(!app.settings_load_failed && app.theme == THEME_CLASSIC);
            customize(&app);
            app_settings_changed(&app);
            app_tick(&app, 0.1f);
            CHECK(app.settings_save_pending && file_io_path_status(path) == 0);
            app_tick(&app, 0.3f);
            if (app.settings_status[0] != '\0') fprintf(stderr, "Storage error: %s\n", app.settings_status);
            CHECK(!app.settings_save_pending && !app.settings_dirty && app.settings_status[0] == '\0');
            CHECK(file_io_path_status(path) == 1);
        } else if (strcmp(mode, "check") == 0) {
            app_init(&app);
            check_custom(&app, argc > 2 ? atoi(argv[2]) : THEME_OCEAN);
        } else if (strcmp(mode, "update") == 0) {
            app_init(&app);
            check_custom(&app, THEME_OCEAN);
            app.theme = THEME_EMERALD;
            app_settings_changed(&app);
            CHECK(app_save_settings(&app));
        } else if (strcmp(mode, "recover") == 0) {
            CHECK(file_io_write_atomic(path, "CSO1", 4));
            app_init(&app);
            check_custom(&app, THEME_OCEAN);
            CHECK(app_storage_recovered_backup());
            CHECK(app.settings_status[0] != '\0');
            CHECK(app_save_settings(&app));
        } else if (strcmp(mode, "corrupt_without_backup") == 0) {
            void* data = NULL;
            size_t size = 0;
            app_init(&app);
            customize(&app);
            CHECK(app_save_settings(&app));
            CHECK(file_io_write_atomic(path, "CSO1", 4));
            app_init(&app);
            CHECK(app.settings_load_failed && !app_save_settings(&app));
            CHECK(file_io_read(path, &data, &size));
            CHECK(size == 4 && memcmp(data, "CSO1", 4) == 0);
            free(data);
            app.theme = THEME_EMERALD;
            app_settings_changed(&app);
            CHECK(app_save_settings(&app));
        } else if (strcmp(mode, "save_failure") == 0) {
            char* text;
            app_init(&app);
            customize(&app);
            CHECK(app_save_settings(&app));
            CHECK(snprintf(backup, sizeof(backup), "%s.bak", path) < (int)sizeof(backup));
            CHECK(file_io_make_directories(backup)); /* Replacement must fail, leaving primary intact. */
            app.theme = THEME_CLASSIC;
            app_settings_changed(&app);
            CHECK(!app_save_settings(&app));
            CHECK(app.settings_status[0] != '\0' && app.settings_dirty);
            text = read_text(path);
            CHECK(text != NULL && strstr(text, "theme=2\n") != NULL);
            free(text);
        } else if (strcmp(mode, "atomic_failure") == 0) {
            CHECK(file_io_working_directory(dir));
            CHECK(file_io_join(fixture, sizeof(fixture), dir, "destination-directory"));
            CHECK(file_io_make_directories(fixture));
            CHECK(!file_io_write_atomic(fixture, "data", 4));
            CHECK(file_io_path_status(fixture) == 1);
        } else if (strcmp(mode, "container_validation") == 0) {
            void* data = NULL;
            void* raw = NULL;
            size_t size = 0;
            size_t raw_size = 0;
            CHECK(file_io_working_directory(dir));
            CHECK(file_io_join(fixture, sizeof(fixture), dir, "container.dat"));
            CHECK(secure_io_write_file(fixture, "theme=2\n", 8));
            CHECK(file_io_read(fixture, &raw, &raw_size));
            if (raw != NULL && raw_size > 12) {
#ifdef _WIN32
                ((unsigned char*)raw)[4] = 1; /* Original DPAPI header version. */
                CHECK(file_io_write_atomic(fixture, raw, raw_size));
                CHECK(secure_io_read_file(fixture, &data, &size));
                CHECK(size == 8 && memcmp(data, "theme=2\n", 8) == 0);
                secure_io_free(data);
                data = NULL;
                ((unsigned char*)raw)[4] = 2;
#endif
                CHECK(file_io_write_atomic(fixture, raw, raw_size - 1));
                CHECK(!secure_io_read_file(fixture, &data, &size));
                ((unsigned char*)raw)[raw_size - 1] ^= 0x40;
                CHECK(file_io_write_atomic(fixture, raw, raw_size));
                CHECK(!secure_io_read_file(fixture, &data, &size));
                ((unsigned char*)raw)[raw_size - 1] ^= 0x40;
                ((unsigned char*)raw)[4] = 99;
                CHECK(file_io_write_atomic(fixture, raw, raw_size));
                CHECK(!secure_io_read_file(fixture, &data, &size));
            }
            free(raw);
        } else if (strcmp(mode, "legacy_container") == 0) {
            /* A known version-1 XOR fixture for "theme=2\n" from the original format. */
            static const unsigned char legacy[] = {
                0x43,0x53,0x4f,0x31,1,2,0,0,8,0,0,0,
                0x06,0x53,0xc2,0x52,0x5a,0x19,0x1d,0xf3
            };
            void* data = NULL;
            size_t size = 0;
            CHECK(file_io_working_directory(dir));
            CHECK(file_io_join(fixture, sizeof(fixture), dir, "version-one.dat"));
            CHECK(file_io_write_atomic(fixture, legacy, sizeof(legacy)));
            CHECK(secure_io_read_file(fixture, &data, &size));
            CHECK(size == 8 && memcmp(data, "theme=2\n", 8) == 0);
            secure_io_free(data);
        } else {
            fprintf(stderr, "Unknown test: %s\n", mode);
            return 2;
        }
    }
    printf("%s: %s\n", mode, failures ? "FAILED" : "passed");
    return failures != 0;
}
