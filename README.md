# Chess

Modern C11 chess application with:

- Bitboard engine
- Raylib GUI
- Stronger AI (iterative deepening + alpha-beta + TT)
- Local/Online multiplayer
- Persistent settings and resumable online sessions

## Table of Contents

1. [Overview](#overview)
2. [Features](#features)
3. [Requirements](#requirements)
4. [Build and Run](#build-and-run)
5. [Engine Benchmarking](#engine-benchmarking)
6. [Linux Release Packaging](#linux-release-packaging)
7. [One-File Packaging (Windows)](#one-file-packaging-windows)
8. [How To Play](#how-to-play)
9. [Online Mode](#online-mode)
10. [AI Details](#ai-details)
11. [Audio System](#audio-system)
12. [Assets](#assets)
13. [Settings and Saved Data](#settings-and-saved-data)
14. [Architecture](#architecture)
15. [Repository Layout](#repository-layout)
16. [Known Limitations](#known-limitations)
17. [Troubleshooting](#troubleshooting)
18. [License](#license)

## Overview

`Chess` is a desktop chess game focused on practical gameplay quality and clean architecture.
It supports single-player, local 2-player, and direct TCP online matches with invite codes.

The app includes:

- Responsive UI with scalable layout
- Board rotation for local/online perspective
- Move animations and move-history panel
- Multiple visual themes
- Split audio controls (SFX, menu music, game music)
- Online active matches list with reconnect/resume flow

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
- `Online`: host/join by invite code over direct TCP

### UI / UX

- Dedicated screens: menu, play, lobby, settings
- Enter-key submit behavior on confirm dialogs/buttons
- Better input UX:
  - `Ctrl+V`, `Shift+Insert` paste
  - `Ctrl+C`, `Ctrl+X`, `Ctrl+A`
  - Backspace/Delete repeat
  - Right-click input context menu
- Scrollable panels with mouse-wheel + draggable scrollbar thumb
- Exit confirmation and leave-match confirmation flows

### Online Session UX

- Host and guest usernames shown in lobby
- Copy button for invite code with visual feedback (`Copied`)
- Match readiness and start flow
- Active matches list (with timestamp, opponent, state)
- Resume/reconnect from active matches
- Session snapshots persisted between app runs

## Requirements

- C compiler with C11 support (`gcc`, `clang`, MinGW, etc.)
- CMake `>= 3.20`
- Ninja (recommended)
- Raylib `5.5` (auto-fetched by default via CMake)

For `chess_onefile` packaging on Windows:

- 7-Zip installed (`7z.exe` + `7z.sfx`)

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
| `CHESS_ENABLE_ENGINE_TESTS` | `ON` | Register quick engine benchmark in CTest |
| `CHESS_BUILD_LEGACY_RELAY_SERVER` | `OFF` | Build legacy optional relay server binary |
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

Run through CTest:

```bash
ctest --test-dir build-bench --output-on-failure
```

## Linux Release Packaging

Build Linux release bundles:

```bash
cmake -S . -B build-release-linux -G Ninja -DCHESS_RELEASE_VERSION=1.1.0
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

Build portable self-extracting executable:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target chess_onefile
```

or:

```bat
tools\package_onefile.bat
```

Versioned package example:

```bash
cmake -S . -B build -G Ninja -DCHESS_RELEASE_VERSION=1.1.0
cmake --build build --target chess_onefile
```

Output:

```text
build/release/chess-windows-x64-v1.1.0.exe
build/release/chess-windows-x64-v1.1.0.exe.sha256
build/release/chess.exe
build/release/chess.exe.sha256
```

Behavior:

- `chess.exe` extracts a `chess` folder in current run directory
- Extracted folder contains:
  - main app executable
  - full `assets/`
- End user does **not** need 7-Zip installed to run packaged `chess.exe`
- 7-Zip is required only at packaging/build time

## How To Play

### General controls

- Left click pieces/squares to select and move
- Use `Menu` button in play screen for leave menu
- Use settings screen for AI/theme/audio/timer/touch-move/online name

### Play screen behavior

- Move log panel is scrollable
- Check/checkmate/time-out status appears in side panel
- Captured pieces shown for both sides
- If touch-move is enabled: after selecting a movable piece, selection cannot be switched

### Window behavior

- Resizable window
- Minimum size: `920 x 640`

## Online Mode

Model:

- Direct TCP P2P
- Invite code encodes host endpoint (`IPv4 + port`, fixed 10-char code)
- No external dedicated game server required for normal player use

Flow:

1. Player A hosts and gets invite code
2. Player B joins with invite code
3. Guest marks `Ready`
4. Host starts game
5. Moves exchanged as validated packets

Connectivity notes:

- Internet connectivity is checked before entering online
- Direct P2P still depends on NAT/firewall reachability
- Some networks (strict NAT/CGNAT) can block incoming direct connections
- Detailed error popup text is shown for join/host/connect failures

Session persistence:

- Up to `ONLINE_MATCH_MAX = 6` online sessions tracked
- Sessions are persisted and can be reopened/reconnected later
- Active games list is sorted by latest start time

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

Storage location:

```text
<executable-directory>/settings.dat
<executable-directory>/online_matches.dat
```

Security layer:

- `settings.dat` and `online_matches.dat` are written through `secure_io`
- On Windows: DPAPI user-scoped encryption
- On non-Windows: XOR obfuscation fallback container

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
- `online_name`

Legacy compatibility:

- If legacy plaintext files exist in working directory, migration is attempted on first run.

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
- `src/network/`:
  - TCP socket client
  - packet protocol
  - invite-code encode/decode
- `src/data/`:
  - secure file read/write

Public headers in `include/` define shared contracts.

Optional target:

- `chess_relay_server` can be built with `-DCHESS_BUILD_LEGACY_RELAY_SERVER=ON`
- Not required for normal direct online gameplay

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
|   |-- audio.h
|   |-- engine.h
|   |-- game_state.h
|   |-- gui.h
|   |-- main_loop.h
|   |-- network.h
|   |-- secure_io.h
|   `-- types.h
|-- src/
|   |-- core/
|   |-- data/
|   |-- engine/
|   |-- gui/
|   |   `-- screens/
|   |-- network/
|   |   `-- protocol.h
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
- No external NAT traversal stack (STUN/TURN) for hard NAT cases
- No full automated CI test/perft suite yet

## Troubleshooting

### Build fails with `Permission denied` on `chess_app.exe`

Cause:

- game exe is still running while linker tries to overwrite it

Fix:

1. Close running game process
2. Rebuild

### Join handshake timeout in online mode

Cause:

- host unreachable due to NAT/firewall/CGNAT/offline host

Fix:

1. Verify host is running and waiting in room
2. Check firewall/router rules for host side
3. Retry on another network if ISP uses strict NAT

### `chess_onefile` packaging fails

Cause:

- missing `7z.exe` or `7z.sfx`

Fix:

1. Install 7-Zip (full installer)
2. Re-run `cmake --build build --target chess_onefile`

## License

This project is licensed under the MIT License.
See `LICENSE`.
