# Chess

A modular chess application written in pure C11, with a bitboard engine, Raylib GUI, and direct TCP peer-to-peer online play.

This repository targets a practical baseline that is easy to build, easy to extend, and clear in architecture.

## Table of Contents

1. [Overview](#overview)
2. [Current Feature Set](#current-feature-set)
3. [Project Architecture](#project-architecture)
4. [Repository Layout](#repository-layout)
5. [Build Requirements](#build-requirements)
6. [Build and Run](#build-and-run)
7. [Configuration Options](#configuration-options)
8. [Gameplay and UI Controls](#gameplay-and-ui-controls)
9. [Networking Model](#networking-model)
10. [Engine Details](#engine-details)
11. [Data Persistence](#data-persistence)
12. [Known Limitations](#known-limitations)
13. [Troubleshooting](#troubleshooting)
14. [Development Notes](#development-notes)
15. [Roadmap](#roadmap)
16. [License](#license)

## Overview

`Chess` provides:

- A legal move chess engine based on bitboards.
- A searchable AI opponent using negamax plus alpha-beta pruning.
- A threaded main loop so AI search does not freeze rendering.
- A Raylib desktop GUI with menu, play, and lobby screens.
- Direct host/join online mode over TCP invite codes.
- Local profile persistence (`username`, `wins`, `losses`).

The codebase is organized into explicit modules (`engine`, `core`, `gui`, `network`, `data`) with shared types in `include/`.

## Current Feature Set

### Rules

- Legal move generation.
- Check and checkmate/stalemate detection.
- Castling, en passant, and pawn promotion support in engine.

### AI and Search

- Iterative deepening search.
- Negamax with alpha-beta pruning.
- Zobrist hashing and transposition table.
- Move ordering based on TT move, captures, and promotions.
- Optional bounded randomness to weaken or diversify play.

### GUI and Application

- Menu with mode selection and AI settings.
- Play screen with selectable pieces and legal-target hints.
- Lobby screen for direct host/join invite flow.

### Online

- One host and one guest over direct TCP.
- Invite code encodes host endpoint (`IPv4 + port`).
- Move packets validated through normal engine move legality.

## Project Architecture

- `src/engine/`: bitboards, attacks, move generation, move application, evaluation, search.
- `src/core/`: app state, mode transitions, AI worker thread, main loop orchestration.
- `src/gui/`: Raylib rendering, widgets, input handling, screen controllers.
- `src/network/`: TCP socket client, packet protocol, invite code encode/decode.
- `src/data/`: local profile load/save and result updates.
- `include/`: shared API headers and public types for all modules.

## Repository Layout

```text
.
|-- assets/
|-- include/
|   |-- engine.h
|   |-- game_state.h
|   |-- gui.h
|   |-- main_loop.h
|   |-- network.h
|   |-- profile_mgr.h
|   `-- types.h
|-- src/
|   |-- core/
|   |   |-- game_state.c
|   |   `-- main_loop.c
|   |-- data/
|   |   `-- profile_mgr.c
|   |-- engine/
|   |   |-- bitboard.c
|   |   |-- movegen.c
|   |   `-- search.c
|   |-- gui/
|   |   |-- renderer.c
|   |   |-- ui_widgets.c
|   |   `-- screens/
|   |       |-- lobby_screen.c
|   |       |-- menu_screen.c
|   |       `-- play_screen.c
|   |-- network/
|   |   |-- client.c
|   |   |-- matchmaker.c
|   |   `-- protocol.h
|   `-- main.c
|-- CMakeLists.txt
|-- Makefile
`-- README.md
```

## Build Requirements

- C compiler with C11 support (`gcc`, `clang`, or compatible MinGW toolchain).
- CMake `3.20+`.
- Ninja (recommended).
- Raylib (if missing locally, CMake can fetch it automatically with `CHESS_FETCH_RAYLIB=ON`).

## Build and Run

### Recommended: CMake + Ninja

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Run:

```bash
./build/chess_app      # Linux/macOS
build\chess_app.exe    # Windows
```

### Optional: Makefile

```bash
make
```

Run:

```bash
./chess_app            # Linux/macOS
chess_app.exe          # Windows
```

## Configuration Options

CMake options:

- `CHESS_FETCH_RAYLIB` (default: `ON`): Fetches Raylib from GitHub when not already installed.
- `CHESS_ENABLE_WARNINGS` (default: `ON`): Enables strict warning flags (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` otherwise).

Example:

```bash
cmake -S . -B build -G Ninja -DCHESS_FETCH_RAYLIB=OFF -DCHESS_ENABLE_WARNINGS=ON
```

## Gameplay and UI Controls

### Menu

- `Single Player`: human vs AI.
- `Local 2 Player`: two humans on one machine.
- `Online (P2P)`: open host/join lobby.
- AI `Difficulty`: one 0..100 setting mapped internally to depth/time/randomness.

### Play Screen

- Left click a piece to select.
- Left click a legal destination to move.
- Click selected square again to clear selection.
- `Menu` button returns to main menu.

### Lobby

- `Host Game`: generate/share invite code.
- `Join Game`: enter invite code and send join request.

## Networking Model

- Protocol: compact packed TCP packet (`NetPacket`).
- Topology: direct P2P, one host and one guest, no standalone server binary required for players.
- Invite code is fixed-length (`10` chars), uses a Base32 alphabet without ambiguous symbols, and encodes `IPv4 + port` (48-bit payload).

Connection flow:

1. Host starts socket and shares invite code.
2. Guest decodes invite code and sends `JOIN_REQUEST`.
3. Host accepts first matching peer and sends `JOIN_ACCEPT`.
4. Both peers exchange `MOVE` packets.

Important:

- Public internet play works when host endpoint is reachable (open/NAT-mapped port).
- Strict NAT/CGNAT networks may still block direct P2P without external relay/STUN/TURN.
- No encryption/authentication is implemented in the baseline.

## Engine Details

### Representation

- Per-side/per-piece bitboards (`pieces[2][6]`).
- Cached occupancy bitboards.
- Zobrist key per position.

### Search

- Iterative deepening root search.
- Negamax with alpha-beta pruning.
- Transposition table size: `2^19` entries.
- Material-only static evaluation (centipawn values).

### Time and Depth Behavior

- Default AI limits are depth `4` and `1500 ms` max search time.
- Search depth is hard-clamped to `1..8` in current implementation.

### Promotion Behavior

- Engine supports all promotion pieces.
- GUI currently auto-chooses queen in click workflow.

## Data Persistence

The app writes `profile.dat` in the working directory.

Format:

```ini
username=Player
wins=0
losses=0
```

Profile is loaded at startup, updated after single-player checkmates, and saved on exit.

## Known Limitations

### Rule Completeness

- Current game-over detection is based on "no legal moves" (checkmate/stalemate).
- Draw rules like threefold repetition, 50-move rule, and insufficient material are not yet enforced.

### AI Quality

- Evaluation is currently material-dominant.
- No advanced positional heuristics yet (mobility, king safety, pawn structure, piece-square tables).

### Online Robustness

- Direct TCP P2P still depends on endpoint reachability (NAT/firewall rules).
- Session recovery is supported at app level, but no external NAT traversal service is bundled.

### UX

- Piece rendering uses text symbols, not sprite assets.
- Underpromotion selection UI is not exposed yet.

### Validation and CI

- No automated test suite/perft harness included yet.

## Troubleshooting

### CMake Cache Path Mismatch

- If the project folder was moved or renamed, remove the old build directory and reconfigure:

```bash
cmake -S . -B build -G Ninja
```

### Raylib Not Found

- If `CHESS_FETCH_RAYLIB=OFF`, install Raylib for your platform or turn fetch back on.

### Online Mode Cannot Connect Over Internet

- Verify host port reachability and router/NAT behavior.
- Some ISPs use CGNAT, which can block inbound P2P connections.

## Development Notes

- Keep cross-module dependencies through headers in `include/`.
- Prefer deterministic behavior for engine testing (`randomness=0`).
- Add move-generation and search regression checks before major engine changes.

## Roadmap

1. Add perft tests and CI build matrix.
2. Improve evaluation with positional heuristics.
3. Add stronger network reliability and session recovery.
4. Implement promotion-choice UI.
5. Add FEN/PGN import-export.

## License

No license file is currently included in this repository. Add a `LICENSE` file before distributing or accepting external contributions under defined terms.
