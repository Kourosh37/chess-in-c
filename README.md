# ChessProject

A modular chess application written in **pure C11** with a focus on performance, clean separation of concerns, and practical maintainability.

The project includes:
- A bitboard-based chess engine with legal move generation.
- A minimax/negamax search with alpha-beta pruning, iterative deepening, and transposition table.
- A Raylib GUI with Menu / Play / Lobby screens.
- Direct **peer-to-peer (P2P)** online play (no standalone central server process).
- A lightweight local profile manager.

## 1. Project Status

Current implementation is a strong baseline and fully buildable with CMake + Ninja.

Implemented:
- Engine core: bitboards, legal move validation, check detection, castling, en-passant, promotion.
- Search core: alpha-beta, iterative deepening, move ordering, zobrist hash + transposition table.
- GUI core: board rendering, piece interaction, game screens.
- AI threading: search runs on a background thread to avoid UI freeze.
- Online core: host/join flow over UDP with invite code endpoint encoding.
- Profile storage: local text file with username and win/loss counters.

Known limitations (explicit):
- P2P connectivity works best on LAN; internet play may require NAT traversal / port forwarding.
- Evaluation is material-dominant (no advanced positional heuristics yet).
- No dedicated automated test suite yet.

## 2. Architecture

The codebase is intentionally decoupled:

- `src/engine/`  
  Chess representation, move generation, legality, evaluation, search.
- `src/core/`  
  App state coordination, mode transitions, AI worker, main loop integration.
- `src/gui/`  
  Raylib rendering + widgets + screen handlers.
- `src/network/`  
  P2P UDP transport, invite-code utilities, packet protocol.
- `src/data/`  
  Local profile persistence.
- `include/`  
  Shared public headers and cross-module types.

## 3. Repository Layout

```text
ChessProject/
??? assets/
??? include/
?   ??? types.h
?   ??? engine.h
?   ??? game_state.h
?   ??? gui.h
?   ??? network.h
?   ??? profile_mgr.h
?   ??? main_loop.h
??? src/
?   ??? core/
?   ??? engine/
?   ??? gui/
?   ?   ??? screens/
?   ??? network/
?   ?   ??? protocol.h
?   ??? data/
?   ??? main.c
??? CMakeLists.txt
??? Makefile
??? README.md
```

## 4. Build Requirements

- C compiler with C11 support (GCC/Clang/MinGW GCC)
- CMake >= 3.20
- Ninja (recommended generator)
- Raylib
  - If not installed, CMake can fetch it automatically (`CHESS_FETCH_RAYLIB=ON`, default)

## 5. Build and Run

### Recommended: CMake + Ninja

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/chess_app          # Linux/macOS
build\chess_app.exe       # Windows
```

### Optional: Makefile

```bash
make
./chess_app                # Linux/macOS
chess_app.exe              # Windows
```

## 6. CMake Options

- `CHESS_FETCH_RAYLIB` (default: `ON`)  
  Fetches Raylib from GitHub if not found locally.
- `CHESS_ENABLE_WARNINGS` (default: `ON`)  
  Enables stricter warning flags.

Example:

```bash
cmake -S . -B build -G Ninja -DCHESS_FETCH_RAYLIB=OFF
```

## 7. Runtime Controls

- **Menu**:
  - Start Single Player
  - Start Local Multiplayer
  - Open Online Lobby (P2P)
  - Adjust AI depth/randomness
- **Board**:
  - Click piece to select
  - Click destination square to move
- **Lobby**:
  - Host game (generate/share invite code)
  - Join game (enter invite code)

## 8. P2P Networking Notes

- The host encodes local `IPv4:port` into an invite code.
- The guest decodes the code and sends a direct UDP join request.
- No separate relay server binary is required for baseline operation.

Security/robustness note:
- Moves are validated through engine legality on both peers before application.

## 9. Data Files

- `profile.dat`
  - Auto-created in the working directory.
  - Stores:
    - `username`
    - `wins`
    - `losses`

## 10. Development Guidelines

- Keep modules decoupled by depending on headers from `include/`.
- Preserve deterministic engine behavior when randomness is disabled.
- Prefer adding tests for move legality and search regressions before major engine changes.

## 11. Next Recommended Milestones

1. Add perft tests and a CI build matrix.
2. Improve evaluation (piece-square tables, king safety, mobility, pawn structure).
3. Add robust connection reliability (retries, heartbeat, reconnect strategy).
4. Add richer promotion UI (underpromotion selection).
5. Introduce PGN/FEN import/export support.
