# ------------------------------------------------------------
# ChessProject Makefile
# ------------------------------------------------------------
# Baseline build script for the Raylib-based chess application.
# Preferred modern build path is CMake + Ninja (see README), but
# this Makefile remains useful for quick local builds.
# ------------------------------------------------------------

CC ?= gcc
TARGET ?= chess_app

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Iinclude
LDFLAGS ?=
LIBS ?=

# Thread flag is required for pthread-based AI worker on GCC/Clang.
THREAD_FLAGS ?= -pthread

SRC = \
	src/main.c \
	src/core/audio.c \
	src/core/game_state.c \
	src/core/main_loop.c \
	src/core/platform_dialog.c \
	src/core/threading.c \
	src/engine/bitboard.c \
	src/engine/movegen.c \
	src/engine/search.c \
	src/gui/font.c \
	src/gui/renderer.c \
	src/gui/ui_widgets.c \
	src/gui/screens/menu_screen.c \
	src/gui/screens/play_screen.c \
	src/gui/screens/settings_screen.c \
	src/data/app_storage.c \
	src/data/file_io.c \
	src/data/secure_io.c

OBJ = $(SRC:.c=.o)

ifeq ($(OS),Windows_NT)
	CFLAGS += $(THREAD_FLAGS)
	LDFLAGS += $(THREAD_FLAGS) -mwindows
	LIBS += -lraylib -lopengl32 -lgdi32 -lwinmm -lcrypt32 -lshell32 -lole32 -luuid
else
	CFLAGS += $(THREAD_FLAGS)
	LDFLAGS += $(THREAD_FLAGS)
	LIBS += -lraylib -lm
endif

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJ) $(TARGET)

.PHONY: all clean
