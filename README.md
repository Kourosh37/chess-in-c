# Chess

Modern C11 chess application with:

- Bitboard engine
- Raylib GUI
- Stronger AI (iterative deepening + alpha-beta + TT)
- Local 2-player multiplayer
- Persistent settings and local player statistics

## Table of Contents

1. [Overview](#overview)
2. [Features](#features)
3. [Requirements](#requirements)
4. [Build and Run](#build-and-run)
5. [Engine Benchmarking](#engine-benchmarking)
6. [Linux Release Packaging](#linux-release-packaging)
7. [One-File Packaging (Windows)](#one-file-packaging-windows)
8. [How To Play](#how-to-play)
9. [AI Details](#ai-details)
10. [Audio System](#audio-system)
11. [Assets](#assets)
12. [Settings and Saved Data](#settings-and-saved-data)
13. [Architecture](#architecture)
14. [Repository Layout](#repository-layout)
15. [Known Limitations](#known-limitations)
16. [Troubleshooting](#troubleshooting)
17. [License](#license)

## Overview

`Chess` is a desktop chess game focused on practical gameplay quality and clean architecture.
It supports single-player games against the AI and local 2-player games on one device.

The app includes:

- Responsive UI with scalable layout
- Board rotation between turns in local 2-player games
- Move animations and move-history panel
- Multiple visual themes
- Split audio controls (SFX, menu music, game music)

## Features

### Core Gameplay

- Legal move generation with bitboards
- Castling, en passant, and promotion
- Check detection and checkmate/stalemate end detection
- Touch-move rule toggle (optional)
- Optional per-turn timer (Off, 10s, 30s, 60s, 120s)

### Modes

- `Single Player`: human vs AI
- `Local 2 Player`: two players on one device, rotating board

### UI / UX

- Dedicated screens: menu, play, settings
- Enter-key submit behavior on confirm dialogs/buttons
- Scrollable panels with mouse-wheel + draggable scrollbar thumb
- Exit confirmation and leave-match confirmation flows

## Requirements

- C compiler with C11 support (`gcc`, `clang`, MinGW, etc.)
- CMake `>= 3.21`
- Ninja (recommended)
- Raylib `5.5` (auto-fetched by default via CMake)

For `chess_onefile` packaging on Windows:

- Inno Setup `>= 6.3` (`ISCC.exe`), required only to build the installer

For Linux release packaging:

- `tar`, `awk`, `tail` (standard Linux userland tools)

## Build and Run

### CMake + Ninja (recommended)

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Run:

```bash
./build/chess_app      # Linux/macOS
build\chess_app.exe    # Windows
```

### Release build

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

### Makefile (optional)

```bash
make
```

Run:

```bash
./chess_app            # Linux/macOS
chess_app.exe          # Windows
```

### CMake options

| Option | Default | Description |
|---|---:|---|
| `CHESS_FETCH_RAYLIB` | `ON` | Auto-fetch raylib if not found locally |
| `CHESS_ENABLE_WARNINGS` | `ON` | Enable compiler warnings (`-Wall -Wextra -Wpedantic` / `/W4`) |
| `CHESS_BUILD_APP` | `ON` | Build GUI app target (`chess_app`) |
| `CHESS_BUILD_ENGINE_BENCH` | `ON` | Build engine benchmark target (`chess_engine_bench`) |
| `CHESS_BUILD_STORAGE_TESTS` | `ON` | Build settings persistence regression tests (no GUI dependency) |
| `CHESS_WINDOWS_PORTABLE_RUNTIME` | `ON` | Use the static MSVC runtime for Windows packages |
| `CHESS_ENABLE_ENGINE_TESTS` | `ON` | Register quick engine benchmark in CTest |
| `CHESS_RELEASE_VERSION` | `1.1.0` | Version label used in release package filenames |
| `CHESS_RELEASE_ARCH` | auto | Architecture label used in release package filenames (`x64`, `arm64`, ...) |

Example:

```bash
cmake -S . -B build -G Ninja -DCHESS_FETCH_RAYLIB=OFF -DCHESS_ENABLE_WARNINGS=ON
```

## Engine Benchmarking

Build engine-only benchmark (no raylib/app dependency):

```bash
cmake -S . -B build-bench -G Ninja -DCHESS_BUILD_APP=OFF -DCHESS_BUILD_ENGINE_BENCH=ON
cmake --build build-bench --target chess_engine_bench
```

Run benchmark suites:

```bash
./build-bench/chess_engine_bench --quick     # fast perft + tactical checks
./build-bench/chess_engine_bench --perft     # full perft validation
./build-bench/chess_engine_bench --tactics   # tactical-only checks
```

Run through CTest (also exercises settings restart, migration, Unicode paths, backup recovery and failed writes):

```bash
ctest --test-dir build-bench --output-on-failure
```

## Linux Release Packaging

Build Linux release bundles:

```bash
cmake -S . -B build-release-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHESS_RELEASE_VERSION=1.1.0
cmake --build build-release-linux --target chess_linux_release
```

or:

```bash
tools/package_linux.sh 1.1.0
```

Output:

```text
build-release-linux/release/chess-linux-x64-v1.1.0.run
build-release-linux/release/chess-linux-x64-v1.1.0.run.sha256
build-release-linux/release/chess-linux-x64-v1.1.0.tar.gz
build-release-linux/release/chess-linux-x64-v1.1.0.tar.gz.sha256
```

Verification:

```bash
cd build-release-linux/release
sha256sum -c chess-linux-x64-v1.1.0.run.sha256
sha256sum -c chess-linux-x64-v1.1.0.tar.gz.sha256
```

Extraction behavior:

- `.run` extracts to `<target>/chess` (default target: current directory)
- `.tar.gz` also contains top-level `chess/` directory
- extracted folder contains:
  - `chess_app`
  - `run.sh` (launcher helper)
  - full `assets/`

Examples:

```bash
chmod +x chess-linux-x64-v1.1.0.run
./chess-linux-x64-v1.1.0.run
./chess-linux-x64-v1.1.0.run /tmp
```

GitHub automation:

- workflow file: `.github/workflows/release-linux.yml`
- pushing tag `v1.1.0` builds Linux artifacts, verifies checksums, validates extracted layout, and attaches files to GitHub Release.

## One-File Packaging (Windows)

Build an Inno Setup installer with the executable, assets and its required non-system DLLs:

```powershell
cmake -S . -B build-release-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHESS_WINDOWS_PORTABLE_RUNTIME=ON
cmake --build build-release-windows
ctest --test-dir build-release-windows --output-on-failure
cmake --build build-release-windows --target chess_onefile
```

Or run `tools\package_onefile.bat [version]` from any working directory. The helper builds Release, runs tests, and packages into `build-release-windows/release/`.

Versioned x64 output (the architecture follows the configured target):

```text
build-release-windows/release/chess-windows-x64-v1.1.0.exe
build-release-windows/release/chess-windows-x64-v1.1.0.exe.sha256
build-release-windows/release/chess.exe
build-release-windows/release/chess.exe.sha256
```

The installer defaults to Program Files, creates shortcuts and an uninstaller, and reuses the previous installation directory on upgrades. Settings live in the current user's data directory and survive reinstalling or uninstalling the app. Post-install launch uses the original user's credentials. Neither users nor packagers need 7-Zip.

Runtime dependency inspection fails the package build if a required DLL is unresolved. Windows system DLLs and unrelated DLLs in the repository are excluded. `runtime_manifest.txt` and the generated `chess_installer.iss` are kept beside the installer for build diagnostics. MSVC packages require `CHESS_WINDOWS_PORTABLE_RUNTIME=ON`.

If Inno Setup is outside PATH, configure with `-DCHESS_ISCC_EXE="C:/path/to/ISCC.exe"`. The legacy `cmake/build_onefile.cmake` entry point delegates to the same installer implementation.

The Windows release workflow builds and runs engine/storage tests, checks a real install/reinstall/uninstall in a temporary directory, verifies the generated SHA256, and uploads the installer. To run the installer smoke test locally on an account without an existing per-user Chess installation:

```powershell
./tools/test_windows_installer.ps1 -Installer ./build-release-windows/release/chess.exe -StorageTests ./build-release-windows/chess_storage_tests.exe -TestRoot ./build-release-windows/installer-tests
```

Add `-TestUI` to exercise changing the theme in the installed app across restarts, including with writes denied to the installation directory. This option requires a Windows desktop session; the game windows are hidden during the test.

## How To Play

### General controls

- Left click pieces/squares to select and move
- Use `Menu` button in play screen for leave menu
- Use settings screen for AI/theme/audio/timer/touch-move

### Play screen behavior

- Move log panel is scrollable
- Check/checkmate/time-out status appears in side panel
- Captured pieces shown for both sides
- If touch-move is enabled: after selecting a movable piece, selection cannot be switched

### Window behavior

- Resizable window
- Minimum size: `920 x 640`

## AI Details

Search:

- Iterative deepening
- Negamax with alpha-beta pruning
- Transposition table (Zobrist hashing)
- Move ordering (TT move, captures, promotions)
- Built-in opening book for practical early-game play
- Additional search heuristics:
  - PVS + LMR + null-move pruning
  - aspiration windows in iterative deepening
  - reverse/futility-style shallow pruning + LMP controls
  - improved quiescence filtering

Difficulty model:

- User controls one value: `0..100`
- Internally mapped to:
  - search depth `2..18`
  - search time budget up to `25000 ms`
- Randomness is forced to `0` (deterministic quality scaling)

## Audio System

SFX channels (expected filenames in `assets/sfx/`):

- `ui_click.wav`
- `piece_move.wav`
- `piece_capture.wav`
- `piece_promotion.wav`
- `king_check.wav`
- `game_over.wav`
- `lobby_join.wav`
- `game_victory.wav`
- `piece_select.wav`

Background music:

- Menu music candidates (first existing file wins):
  - `menu_bgm.ogg`, `menu_bgm.mp3`, `menu_bgm.wav`
- In-game music candidates:
  - `game_bgm.ogg`, `game_bgm.mp3`, `game_bgm.wav`

Notes:

- Missing files do not crash the app; only that sound stays silent
- Separate runtime volume controls:
  - SFX volume
  - Menu music volume
  - Game music volume
- Missing audio files are listed in a scrollable panel in settings

## Assets

### Piece textures

- Normal set:
  - `assets/pieces/staunton/*.png`
- Flipped set:
  - `assets/pieces/staunton_flipped/*.png`

If flipped textures are missing, renderer can still draw with rotation fallback.
If both texture sets are missing, renderer falls back to built-in vector pieces.

### Window/EXE icon

- Path: `assets/icons/app_icon.png`
- Used for:
  - runtime window icon
  - embedded executable icon (Windows build step)

### UI fonts

Font loading order:

1. `assets/fonts/ui_font.ttf`
2. `assets/fonts/Cinzel-Bold.ttf`
3. `assets/fonts/PlayfairDisplay-Bold.ttf`
4. system bold serif fallbacks
5. `assets/fonts/NotoSans-Regular.ttf`
6. raylib default fallback

## Settings and Saved Data

Settings are independent of the executable directory, shortcuts and launch working directory:

| Platform | Settings file |
|---|---|
| Windows | `%LOCALAPPDATA%\Chess\settings.dat` |
| Linux | `$XDG_CONFIG_HOME/chess/settings.dat`, or `$HOME/.config/chess/settings.dat` |
| macOS | `$HOME/Library/Application Support/Chess/settings.dat` |

The directory is created automatically. `CHESS_CONFIG_DIR` can override it with an absolute UTF-8 path for isolated testing or custom deployments. Unicode names and long Windows paths are supported.

Changes are saved after a short debounce while adjusting sliders, immediately when leaving Settings, and on normal exit. A failed save is shown in Settings and reported on exit. Saving uses a flushed temporary file in the same directory followed by atomic replacement; the previous valid preferences are kept as `settings.dat.bak`. If the primary file cannot be read, the backup is tried. An unreadable primary and backup are not silently replaced with defaults merely by closing the game.

Windows files use user-scoped DPAPI. Other platforms retain the legacy XOR obfuscation with a checksum for accidental-corruption detection; this is not encryption. Version-1 files and legacy plaintext remain readable.

Important `settings.dat` keys:

- `theme`
- `ai_difficulty`
- `touch_move_enabled`
- `turn_timer_enabled`
- `turn_time_seconds`
- `sound_enabled`
- `sfx_volume`
- `menu_music_volume`
- `game_music_volume`
- `player_name`
- `wins`
- `losses`

Legacy compatibility:

- On first run, an existing settings file beside the executable or in the original launch directory is imported into the user directory without deleting the original. Encrypted and accidentally double-encrypted legacy files are decoded before migration. Existing user settings take precedence over installation-directory files.

## Architecture

Top-level modules:

- `src/engine/`:
  - board representation
  - move generation/validation
  - evaluation/search
- `src/core/`:
  - app state lifecycle
  - mode transitions
  - game flow + timers
  - persistence orchestration
- `src/gui/`:
  - rendering
  - widgets
  - screen controllers
- `src/data/`:
  - per-user paths, legacy migration and backup recovery
  - UTF-8 file access and atomic writes
  - secure file read/write

Public headers in `include/` define shared contracts.

## Repository Layout

```text
.
|-- assets/
|   |-- fonts/
|   |-- icons/
|   |-- pieces/
|   `-- sfx/
|-- cmake/
|-- include/
|   |-- app_storage.h
|   |-- audio.h
|   |-- file_io.h
|   |-- engine.h
|   |-- game_state.h
|   |-- gui.h
|   |-- main_loop.h
|   |-- secure_io.h
|   `-- types.h
|-- src/
|   |-- core/
|   |-- data/
|   |-- engine/
|   |-- gui/
|   |   `-- screens/
|   `-- main.c
|-- tools/
|-- CMakeLists.txt
|-- Makefile
|-- LICENSE
`-- README.md
```

## Known Limitations

- Draw rules not fully complete yet:
  - no threefold repetition
  - no 50-move rule
  - no insufficient-material auto-draw
- CI runs quick engine and storage regressions; use `--perft` for deeper move-generation validation

## Troubleshooting

### Build fails with `Permission denied` on `chess_app.exe`

Cause:

- game exe is still running while linker tries to overwrite it

Fix:

1. Close running game process
2. Rebuild

### `chess_onefile` packaging fails

Install Inno Setup 6.3 or newer, or set `CHESS_ISCC_EXE` to its compiler path. Build using Release and the static MSVC runtime. An unresolved DLL error must be fixed before distributing a package; `runtime_manifest.txt` records which files were bundled.

### Settings could not be saved

The Settings screen displays the actual storage error. Check that the current user's configuration directory is writable and that security software is not blocking atomic file replacement. There is no need to run the game as administrator. If both settings files are damaged, changing a preference explicitly allows new settings to be written; otherwise the unreadable file is preserved.

## License

This project is licensed under the MIT License.
See `LICENSE`.
