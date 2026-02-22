#include "engine.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/* Transposition-table size (must be power-of-two for mask indexing). */
#define TT_SIZE (1U << 19)

/* Search score sentinels. */
#define INF_SCORE 300000
#define MATE_SCORE 200000

/* Public depth clamp for this baseline engine implementation. */
#define SEARCH_MIN_DEPTH 1
#define SEARCH_MAX_DEPTH 8

typedef enum TTFlag {
    TT_FLAG_EXACT = 0,
    TT_FLAG_LOWER = 1,
    TT_FLAG_UPPER = 2
} TTFlag;

/* One transposition-table entry. */
typedef struct TTEntry {
    uint64_t key;
    int depth;
    int score;
    uint8_t flag;
    Move best_move;
} TTEntry;

/* Shared recursive-search context. */
typedef struct SearchContext {
    SearchLimits limits;
    uint64_t start_ms;
    uint64_t nodes;
    bool stop;
} SearchContext;

static TTEntry g_tt[TT_SIZE];

/* Material-only baseline values (centipawns). */
static const int g_piece_values[6] = {100, 320, 330, 500, 900, 20000};

/* Portable popcount for C11 baseline. */
static int bit_count(Bitboard bb) {
    int count = 0;
    while (bb != 0ULL) {
        bb &= (bb - 1ULL);
        count++;
    }
    return count;
}

/* Move equality helper used for root-score bookkeeping. */
static bool move_same(Move a, Move b) {
    if (a.from != b.from || a.to != b.to) {
        return false;
    }

    if ((a.flags & MOVE_FLAG_PROMOTION) != 0U || (b.flags & MOVE_FLAG_PROMOTION) != 0U) {
        return a.promotion == b.promotion;
    }

    return true;
}

/* Monotonic-ish millisecond timestamp for search time control. */
static uint64_t now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
#endif
}

/* Static exchange-inspired capture bonus used in move ordering. */
static int score_capture(const Position* pos, Move move) {
    Side us;
    Side them;
    Side side;
    PieceType piece;
    int captured_value = g_piece_values[PIECE_PAWN];
    int attacker_value = g_piece_values[PIECE_PAWN];

    if ((move.flags & MOVE_FLAG_CAPTURE) == 0U) {
        return 0;
    }

    us = pos->side_to_move;
    them = (us == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;

    if ((move.flags & MOVE_FLAG_EN_PASSANT) != 0U) {
        captured_value = g_piece_values[PIECE_PAWN];
    } else if (position_piece_at(pos, move.to, &side, &piece) && side == them) {
        captured_value = g_piece_values[piece];
    }

    if (position_piece_at(pos, move.from, &side, &piece) && side == us) {
        attacker_value = g_piece_values[piece];
    }

    return (captured_value * 16) - attacker_value;
}

/* Orders moves by TT move, captures, and promotions. */
static void sort_moves(const Position* pos, MoveList* list, Move tt_move) {
    for (int i = 0; i < list->count; ++i) {
        int score = 0;

        if (move_same(list->moves[i], tt_move)) {
            score += 30000;
        }

        if ((list->moves[i].flags & MOVE_FLAG_CAPTURE) != 0U) {
            score += 10000 + score_capture(pos, list->moves[i]);
        }

        if ((list->moves[i].flags & MOVE_FLAG_PROMOTION) != 0U) {
            score += 8000;
        }

        list->moves[i].score = (int16_t)score;
    }

    /* Insertion sort is fine for small move lists and cache friendly. */
    for (int i = 1; i < list->count; ++i) {
        Move key = list->moves[i];
        int j = i - 1;

        while (j >= 0 && list->moves[j].score < key.score) {
            list->moves[j + 1] = list->moves[j];
            j--;
        }

        list->moves[j + 1] = key;
    }
}

/* Material evaluation from side-to-move perspective (for negamax). */
static int evaluate_for_side(const Position* pos) {
    int white_score = 0;
    int black_score = 0;

    for (int piece = 0; piece < 6; ++piece) {
        white_score += bit_count(pos->pieces[SIDE_WHITE][piece]) * g_piece_values[piece];
        black_score += bit_count(pos->pieces[SIDE_BLACK][piece]) * g_piece_values[piece];
    }

    {
        int eval = white_score - black_score;
        return (pos->side_to_move == SIDE_WHITE) ? eval : -eval;
    }
}

/* Public evaluation from White perspective. */
int evaluate_position(const Position* pos) {
    int white_score = 0;
    int black_score = 0;

    for (int piece = 0; piece < 6; ++piece) {
        white_score += bit_count(pos->pieces[SIDE_WHITE][piece]) * g_piece_values[piece];
        black_score += bit_count(pos->pieces[SIDE_BLACK][piece]) * g_piece_values[piece];
    }

    return white_score - black_score;
}

/* Negamax with alpha-beta pruning and TT probing/storing. */
static int negamax(const Position* pos, int depth, int alpha, int beta, int ply, SearchContext* ctx) {
    int alpha_orig = alpha;
    int beta_orig = beta;
    TTEntry* entry;
    Move tt_move = {0};

    if (ctx->limits.max_time_ms > 0) {
        uint64_t elapsed = now_ms() - ctx->start_ms;
        if (elapsed >= (uint64_t)ctx->limits.max_time_ms) {
            ctx->stop = true;
            return 0;
        }
    }

    ctx->nodes++;

    if (depth <= 0) {
        return evaluate_for_side(pos);
    }

    entry = &g_tt[pos->zobrist_key & (TT_SIZE - 1)];

    if (entry->key == pos->zobrist_key) {
        tt_move = entry->best_move;

        if (entry->depth >= depth) {
            if (entry->flag == TT_FLAG_EXACT) {
                return entry->score;
            }

            if (entry->flag == TT_FLAG_LOWER && entry->score > alpha) {
                alpha = entry->score;
            }
            if (entry->flag == TT_FLAG_UPPER && entry->score < beta) {
                beta = entry->score;
            }

            if (alpha >= beta) {
                return entry->score;
            }
        }
    }

    {
        MoveList moves;
        int best_score = -INF_SCORE;
        Move best_move;

        generate_legal_moves(pos, &moves);

        if (moves.count == 0) {
            if (engine_in_check(pos, pos->side_to_move)) {
                return -MATE_SCORE + ply;
            }
            return 0;
        }

        sort_moves(pos, &moves, tt_move);
        best_move = moves.moves[0];

        for (int i = 0; i < moves.count; ++i) {
            Position next = *pos;
            int score;

            if (!engine_apply_move(&next, moves.moves[i])) {
                continue;
            }

            score = -negamax(&next, depth - 1, -beta, -alpha, ply + 1, ctx);
            if (ctx->stop) {
                return 0;
            }

            if (score > best_score) {
                best_score = score;
                best_move = moves.moves[i];
            }

            if (score > alpha) {
                alpha = score;
            }

            if (alpha >= beta) {
                break;
            }
        }

        entry->key = pos->zobrist_key;
        entry->depth = depth;
        entry->score = best_score;
        entry->best_move = best_move;

        if (best_score <= alpha_orig) {
            entry->flag = TT_FLAG_UPPER;
        } else if (best_score >= beta_orig) {
            entry->flag = TT_FLAG_LOWER;
        } else {
            entry->flag = TT_FLAG_EXACT;
        }

        return best_score;
    }
}

/* Clears transposition table content. */
void engine_reset_transposition_table(void) {
    memset(g_tt, 0, sizeof(g_tt));
}

/* Iterative deepening root search with optional move-randomness window. */
void search_best_move(const Position* pos, const SearchLimits* limits, SearchResult* out_result) {
    SearchLimits local_limits;
    SearchContext ctx;
    MoveList root_moves;
    SearchResult result;
    Move best_move;
    int best_score = -INF_SCORE;
    int root_scores[MAX_MOVES];

    if (pos == NULL || limits == NULL || out_result == NULL) {
        return;
    }

    local_limits = *limits;
    if (local_limits.depth < SEARCH_MIN_DEPTH) {
        local_limits.depth = SEARCH_MIN_DEPTH;
    }
    if (local_limits.depth > SEARCH_MAX_DEPTH) {
        local_limits.depth = SEARCH_MAX_DEPTH;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.limits = local_limits;
    ctx.start_ms = now_ms();

    generate_legal_moves(pos, &root_moves);

    memset(&result, 0, sizeof(result));
    result.best_move.promotion = PIECE_NONE;

    if (root_moves.count == 0) {
        *out_result = result;
        return;
    }

    best_move = root_moves.moves[0];
    memset(root_scores, 0, sizeof(root_scores));

    for (int depth = 1; depth <= local_limits.depth; ++depth) {
        Move tt_move = {0};
        TTEntry* root_entry = &g_tt[pos->zobrist_key & (TT_SIZE - 1)];
        MoveList depth_moves = root_moves;
        int depth_best_score = -INF_SCORE;
        Move depth_best_move = depth_moves.moves[0];

        if (root_entry->key == pos->zobrist_key) {
            tt_move = root_entry->best_move;
        }

        sort_moves(pos, &depth_moves, tt_move);

        for (int i = 0; i < depth_moves.count; ++i) {
            Position next = *pos;
            int score;

            if (!engine_apply_move(&next, depth_moves.moves[i])) {
                continue;
            }

            score = -negamax(&next, depth - 1, -INF_SCORE, INF_SCORE, 1, &ctx);
            if (ctx.stop) {
                break;
            }

            for (int m = 0; m < root_moves.count; ++m) {
                if (move_same(root_moves.moves[m], depth_moves.moves[i])) {
                    root_scores[m] = score;
                    break;
                }
            }

            if (score > depth_best_score) {
                depth_best_score = score;
                depth_best_move = depth_moves.moves[i];
            }
        }

        if (ctx.stop) {
            break;
        }

        best_score = depth_best_score;
        best_move = depth_best_move;
        result.depth_reached = depth;
    }

    if (!ctx.stop && local_limits.randomness > 0 && root_moves.count > 1) {
        Move candidates[MAX_MOVES];
        int candidate_count = 0;

        for (int i = 0; i < root_moves.count; ++i) {
            if (root_scores[i] >= (best_score - local_limits.randomness)) {
                candidates[candidate_count++] = root_moves.moves[i];
            }
        }

        if (candidate_count > 1) {
            best_move = candidates[rand() % candidate_count];
        }
    }

    result.best_move = best_move;
    result.score = best_score;
    result.nodes = ctx.nodes;

    *out_result = result;
}
