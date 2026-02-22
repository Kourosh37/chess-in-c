#ifndef TYPES_H
#define TYPES_H

/*
 * Shared project-wide data types for the chess engine, GUI, and networking
 * layers. Keeping these definitions in one header prevents type drift between
 * modules and helps preserve a clean, decoupled architecture.
 */

#include <stdbool.h>
#include <stdint.h>

/* Board and protocol limits. */
#define BOARD_SQUARES 64
#define MAX_MOVES 256
#define INVITE_CODE_LEN 10
#define PLAYER_NAME_MAX 31

typedef uint64_t Bitboard;

/* Piece side (color). */
typedef enum Side {
    SIDE_WHITE = 0,
    SIDE_BLACK = 1
} Side;

/* Piece kind index used in bitboard arrays. */
typedef enum PieceType {
    PIECE_PAWN = 0,
    PIECE_KNIGHT = 1,
    PIECE_BISHOP = 2,
    PIECE_ROOK = 3,
    PIECE_QUEEN = 4,
    PIECE_KING = 5,
    PIECE_NONE = 255
} PieceType;

/* High-level play modes exposed by the application. */
typedef enum GameMode {
    MODE_SINGLE = 0,
    MODE_LOCAL = 1,
    MODE_ONLINE = 2
} GameMode;

/* Top-level GUI screens. */
typedef enum AppScreen {
    SCREEN_MENU = 0,
    SCREEN_PLAY = 1,
    SCREEN_LOBBY = 2,
    SCREEN_SETTINGS = 3
} AppScreen;

/* Built-in visual themes exposed in the settings screen. */
typedef enum ColorTheme {
    THEME_CLASSIC = 0,
    THEME_EMERALD = 1,
    THEME_OCEAN = 2
} ColorTheme;

/* Move flags used by move generation, validation, and networking. */
typedef enum MoveFlags {
    MOVE_FLAG_NONE = 0,
    MOVE_FLAG_CAPTURE = 1 << 0,
    MOVE_FLAG_DOUBLE_PAWN = 1 << 1,
    MOVE_FLAG_EN_PASSANT = 1 << 2,
    MOVE_FLAG_KING_CASTLE = 1 << 3,
    MOVE_FLAG_QUEEN_CASTLE = 1 << 4,
    MOVE_FLAG_PROMOTION = 1 << 5
} MoveFlags;

/* Compact move structure; squares are 0..63. */
typedef struct Move {
    uint8_t from;
    uint8_t to;
    uint8_t promotion;
    uint8_t flags;
    int16_t score;
} Move;

/* Flat move list with fixed capacity for speed and allocation simplicity. */
typedef struct MoveList {
    Move moves[MAX_MOVES];
    int count;
} MoveList;

/*
 * Full game position represented with per-side/per-piece bitboards.
 * Castling rights are encoded as bit flags (KQkq in low 4 bits).
 */
typedef struct Position {
    Bitboard pieces[2][6];
    Bitboard occupied[2];
    Bitboard all_occupied;
    Side side_to_move;
    uint8_t castling_rights;
    int8_t en_passant_square;
    uint16_t halfmove_clock;
    uint16_t fullmove_number;
    uint64_t zobrist_key;
} Position;

/* Search limits configured by UI and consumed by engine search. */
typedef struct SearchLimits {
    int depth;
    int max_time_ms;
    int randomness;
} SearchLimits;

/* Search output payload for GUI and logging. */
typedef struct SearchResult {
    Move best_move;
    int score;
    int depth_reached;
    uint64_t nodes;
} SearchResult;

/* Persisted user profile (local file-backed storage). */
typedef struct Profile {
    char username[PLAYER_NAME_MAX + 1];
    uint32_t wins;
    uint32_t losses;
} Profile;

#endif
