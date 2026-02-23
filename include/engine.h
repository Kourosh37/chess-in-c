#ifndef ENGINE_H
#define ENGINE_H

/*
 * Public API for the chess engine layer:
 * - position setup and helpers
 * - legal move generation
 * - move application/validation
 * - evaluation and search
 */

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void engine_init(void);
void engine_reset_transposition_table(void);

/* Position lifecycle helpers. */
void position_set_empty(Position* pos);
void position_set_start(Position* pos);
bool position_set_from_fen(Position* pos, const char* fen);
void position_refresh_occupancy(Position* pos);
uint64_t position_compute_zobrist(const Position* pos);

/* Attack table/ray helpers used by move generation. */
Bitboard engine_get_knight_attacks(int square);
Bitboard engine_get_king_attacks(int square);
Bitboard engine_get_pawn_attacks(Side side, int square);
Bitboard engine_get_bishop_attacks(int square, Bitboard occupancy);
Bitboard engine_get_rook_attacks(int square, Bitboard occupancy);

/* Tactical helpers. */
int engine_find_king_square(const Position* pos, Side side);
bool engine_is_square_attacked(const Position* pos, int square, Side by_side);
bool engine_in_check(const Position* pos, Side side);

/* Board inspection and piece presentation helpers. */
bool position_piece_at(const Position* pos, int square, Side* out_side, PieceType* out_piece);
char piece_to_char(Side side, PieceType piece);

/* Move generation and application. */
void generate_legal_moves(const Position* pos, MoveList* list);
bool engine_apply_move(Position* pos, Move move);
bool engine_make_move(Position* pos, Move move);

/* Evaluation and search entry points. */
int evaluate_position(const Position* pos);
void search_best_move(const Position* pos, const SearchLimits* limits, SearchResult* out_result);

/* UCI coordinate helpers (e.g. e2e4, e7e8q). */
void move_to_uci(Move move, char out[6]);
bool move_from_uci(const char* text, Move* out_move);

#ifdef __cplusplus
}
#endif

#endif
