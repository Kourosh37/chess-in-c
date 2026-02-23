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
#define MATE_SCORE 250000
#define MATE_BOUND (MATE_SCORE - 1024)

/* Search limits and internal stack caps. */
#define SEARCH_MIN_DEPTH 1
#define SEARCH_MAX_DEPTH 12
#define MAX_SEARCH_PLY 128
#define MAX_HISTORY_PLY 256

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

    uint64_t path_keys[MAX_HISTORY_PLY];
    int path_len;

    Move killer_moves[MAX_SEARCH_PLY][2];
    int history[2][BOARD_SQUARES][BOARD_SQUARES];
} SearchContext;

static TTEntry g_tt[TT_SIZE];

/* Capture ordering values (king remains very high for MVV/LVA ranking). */
static const int g_capture_values[6] = {100, 320, 330, 500, 900, 20000};
/* Evaluation values (king excluded to avoid giant cancelling constants). */
static const int g_eval_values[6] = {100, 320, 330, 500, 900, 0};
/* Game-phase interpolation weights (max total = 24). */
static const int g_phase_weights[6] = {0, 1, 1, 2, 4, 0};

/* Midgame PST values from White perspective (a1..h8). */
static const int g_pst_mg[6][64] = {
    /* Pawn */
    {
          0,   0,   0,   0,   0,   0,   0,   0,
         98, 134,  61,  95,  68, 126,  34, -11,
         -6,   7,  26,  31,  65,  56,  25, -20,
        -14,  13,   6,  21,  23,  12,  17, -23,
        -27,  -2,  -5,  12,  17,   6,  10, -25,
        -26,  -4,  -4, -10,   3,   3,  33, -12,
        -35,  -1, -20, -23, -15,  24,  38, -22,
          0,   0,   0,   0,   0,   0,   0,   0
    },
    /* Knight */
    {
       -167, -89, -34, -49,  61, -97, -15,-107,
        -73, -41,  72,  36,  23,  62,   7, -17,
        -47,  60,  37,  65,  84, 129,  73,  44,
         -9,  17,  19,  53,  37,  69,  18,  22,
        -13,   4,  16,  13,  28,  19,  21,  -8,
        -23,  -9,  12,  10,  19,  17,  25, -16,
        -29, -53, -12,  -3,  -1,  18, -14, -19,
       -105, -21, -58, -33, -17, -28, -19, -23
    },
    /* Bishop */
    {
        -29,   4, -82, -37, -25, -42,   7,  -8,
        -26,  16, -18, -13,  30,  59,  18, -47,
        -16,  37,  43,  40,  35,  50,  37,  -2,
         -4,   5,  19,  50,  37,  37,   7,  -2,
         -6,  13,  13,  26,  34,  12,  10,   4,
          0,  15,  15,  15,  14,  27,  18,  10,
          4,  15,  16,   0,   7,  21,  33,   1,
        -33,  -3, -14, -21, -13, -12, -39, -21
    },
    /* Rook */
    {
         32,  42,  32,  51,  63,   9,  31,  43,
         27,  32,  58,  62,  80,  67,  26,  44,
         -5,  19,  26,  36,  17,  45,  61,  16,
        -24, -11,   7,  26,  24,  35,  -8, -20,
        -36, -26, -12,  -1,   9,  -7,   6, -23,
        -45, -25, -16, -17,   3,   0,  -5, -33,
        -44, -16, -20,  -9,  -1,  11,  -6, -71,
        -19, -13,   1,  17,  16,   7, -37, -26
    },
    /* Queen */
    {
        -28,   0,  29,  12,  59,  44,  43,  45,
        -24, -39,  -5,   1, -16,  57,  28,  54,
        -13, -17,   7,   8,  29,  56,  47,  57,
        -27, -27, -16, -16,  -1,  17,  -2,   1,
         -9, -26,  -9, -10,  -2,  -4,   3,  -3,
        -14,   2, -11,  -2,  -5,   2,  14,   5,
        -35,  -8,  11,   2,   8,  15,  -3,   1,
         -1, -18,  -9,  10, -15, -25, -31, -50
    },
    /* King (midgame) */
    {
        -65,  23,  16, -15, -56, -34,   2,  13,
         29,  -1, -20,  -7,  -8,  -4, -38, -29,
         -9,  24,   2, -16, -20,   6,  22, -22,
        -17, -20, -12, -27, -30, -25, -14, -36,
        -49,  -1, -27, -39, -46, -44, -33, -51,
        -14, -14, -22, -46, -44, -30, -15, -27,
          1,   7,  -8, -64, -43, -16,   9,   8,
        -15,  36,  12, -54,   8, -28,  24,  14
    }
};

/* Endgame PST values from White perspective (a1..h8). */
static const int g_pst_eg[6][64] = {
    /* Pawn */
    {
          0,   0,   0,   0,   0,   0,   0,   0,
        178, 173, 158, 134, 147, 132, 165, 187,
         94, 100,  85,  67,  56,  53,  82,  84,
         32,  24,  13,   5,  -2,   4,  17,  17,
         13,   9,  -3,  -7,  -7,  -8,   3,  -1,
          4,   7,  -6,   1,   0,  -5,  -1,  -8,
         13,   8,   8,  10,  13,   0,   2,  -7,
          0,   0,   0,   0,   0,   0,   0,   0
    },
    /* Knight */
    {
        -58, -38, -13, -28, -31, -27, -63, -99,
        -25,  -8, -25,  -2,  -9, -25, -24, -52,
        -24, -20,  10,   9,  -1,  -9, -19, -41,
        -17,   3,  22,  22,  22,  11,   8, -18,
        -18,  -6,  16,  25,  16,  17,   4, -18,
        -23,  -3,  -1,  15,  10,  -3, -20, -22,
        -42, -20, -10,  -5,  -2, -20, -23, -44,
        -29, -51, -23, -15, -22, -18, -50, -64
    },
    /* Bishop */
    {
        -14, -21, -11,  -8,  -7,  -9, -17, -24,
         -8,  -4,   7, -12,  -3, -13,  -4, -14,
          2,  -8,   0,  -1,  -2,   6,   0,   4,
         -3,   9,  12,   9,  14,  10,   3,   2,
         -6,   3,  13,  19,   7,  10,  -3,  -9,
        -12,  -3,   8,  10,  13,   3,  -7, -15,
        -14, -18,  -7,  -1,   4,  -9, -15, -27,
        -23,  -9, -23,  -5,  -9, -16,  -5, -17
    },
    /* Rook */
    {
         13,  10,  18,  15,  12,  12,   8,   5,
         11,  13,  13,  11,  -3,   3,   8,   3,
          7,   7,   7,   5,   4,  -3,  -5,  -3,
          4,   3,  13,   1,   2,   1,  -1,   2,
          3,   5,   8,   4,  -5,  -6,  -8, -11,
         -4,   0,  -5,  -1,  -7, -12,  -8, -16,
         -6,  -6,   0,   2,  -9,  -9, -11,  -3,
         -9,   2,   3,  -1,  -5, -13,   4, -20
    },
    /* Queen */
    {
         -9,  22,  22,  27,  27,  19,  10,  20,
        -17,  20,  32,  41,  58,  25,  30,   0,
        -20,   6,   9,  49,  47,  35,  19,   9,
          3,  22,  24,  45,  57,  40,  57,  36,
        -18,  28,  19,  47,  31,  34,  39,  23,
        -16, -27,  15,   6,   9,  17,  10,   5,
        -22, -23, -30, -16, -16, -23, -36, -32,
        -33, -28, -22, -43,  -5, -32, -20, -41
    },
    /* King (endgame) */
    {
        -74, -35, -18, -18, -11,  15,   4, -17,
        -12,  17,  14,  17,  17,  38,  23,  11,
         10,  17,  23,  15,  20,  45,  44,  13,
         -8,  22,  24,  27,  26,  33,  26,   3,
        -18,  -4,  21,  24,  27,  23,   9, -11,
        -19,  -3,  11,  21,  23,  16,   7,  -9,
        -27, -11,   4,  13,  14,   4,  -5, -17,
        -53, -34, -21, -11, -28, -14, -24, -43
    }
};

/* Portable popcount for C11 baseline. */
static int bit_count(Bitboard bb) {
    int count = 0;
    while (bb != 0ULL) {
        bb &= (bb - 1ULL);
        count++;
    }
    return count;
}

/* Pops and returns least-significant set bit index from a non-zero bitboard. */
static int pop_lsb(Bitboard* bb) {
    int index = 0;
    Bitboard value = *bb;

    while ((value & 1ULL) == 0ULL) {
        value >>= 1U;
        index++;
    }

    *bb &= (*bb - 1ULL);
    return index;
}

/* Move equality helper used for ordering and bookkeeping. */
static bool move_same(Move a, Move b) {
    if (a.from != b.from || a.to != b.to) {
        return false;
    }

    if ((a.flags & MOVE_FLAG_PROMOTION) != 0U || (b.flags & MOVE_FLAG_PROMOTION) != 0U) {
        return a.promotion == b.promotion;
    }

    return true;
}

/* Mirrors square index around horizontal axis (white<->black perspective). */
static int mirror_square(int square) {
    return square ^ 56;
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

/* Converts mate score for TT storage so distance-to-mate remains stable by ply. */
static int score_to_tt(int score, int ply) {
    if (score > MATE_BOUND) {
        return score + ply;
    }
    if (score < -MATE_BOUND) {
        return score - ply;
    }
    return score;
}

/* Converts TT mate score back to current ply distance. */
static int score_from_tt(int score, int ply) {
    if (score > MATE_BOUND) {
        return score - ply;
    }
    if (score < -MATE_BOUND) {
        return score + ply;
    }
    return score;
}

/* Light repetition detection over current PV path (draw by repetition). */
static bool is_repetition(const SearchContext* ctx, uint64_t key) {
    for (int i = ctx->path_len - 2; i >= 0; i -= 2) {
        if (ctx->path_keys[i] == key) {
            return true;
        }
    }
    return false;
}

/* Periodic timeout check (amortized to avoid expensive clock calls every node). */
static bool search_should_stop(SearchContext* ctx) {
    if (ctx->stop) {
        return true;
    }
    if (ctx->limits.max_time_ms <= 0) {
        return false;
    }
    if ((ctx->nodes & 1023ULL) != 0ULL) {
        return false;
    }
    if ((now_ms() - ctx->start_ms) >= (uint64_t)ctx->limits.max_time_ms) {
        ctx->stop = true;
        return true;
    }
    return false;
}

/* Static exchange-inspired capture bonus used in move ordering. */
static int score_capture(const Position* pos, Move move) {
    Side us;
    Side them;
    Side side;
    PieceType piece;
    int captured_value = g_capture_values[PIECE_PAWN];
    int attacker_value = g_capture_values[PIECE_PAWN];

    if ((move.flags & MOVE_FLAG_CAPTURE) == 0U) {
        return 0;
    }

    us = pos->side_to_move;
    them = (us == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;

    if ((move.flags & MOVE_FLAG_EN_PASSANT) != 0U) {
        captured_value = g_capture_values[PIECE_PAWN];
    } else if (position_piece_at(pos, move.to, &side, &piece) && side == them) {
        captured_value = g_capture_values[piece];
    }

    if (position_piece_at(pos, move.from, &side, &piece) && side == us) {
        attacker_value = g_capture_values[piece];
    }

    return (captured_value * 16) - attacker_value;
}

/* Scores one move for ordering with TT move, MVV/LVA, killers and history. */
static int score_move(const Position* pos, Move move, Move tt_move, const SearchContext* ctx, int ply, bool qsearch) {
    int score = 0;
    bool is_capture = (move.flags & MOVE_FLAG_CAPTURE) != 0U;
    bool is_promo = (move.flags & MOVE_FLAG_PROMOTION) != 0U;

    if (move_same(move, tt_move)) {
        score += 30000;
    }

    if (is_capture) {
        score += 10000 + score_capture(pos, move);
    }

    if (is_promo) {
        score += 9000 + g_capture_values[move.promotion == PIECE_NONE ? PIECE_QUEEN : move.promotion];
    }

    if (!qsearch && !is_capture && !is_promo && ply >= 0 && ply < MAX_SEARCH_PLY) {
        if (move_same(move, ctx->killer_moves[ply][0])) {
            score += 7000;
        } else if (move_same(move, ctx->killer_moves[ply][1])) {
            score += 6500;
        }

        score += ctx->history[pos->side_to_move][move.from][move.to];
    }

    return score;
}

/* Orders moves by score in descending order (insertion sort fits small lists). */
static void sort_moves(const Position* pos,
                       MoveList* list,
                       Move tt_move,
                       const SearchContext* ctx,
                       int ply,
                       bool qsearch) {
    for (int i = 0; i < list->count; ++i) {
        int s = score_move(pos, list->moves[i], tt_move, ctx, ply, qsearch);
        list->moves[i].score = (int16_t)((s > 32767) ? 32767 : ((s < -32768) ? -32768 : s));
    }

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

/* Blend MG/EG PST-evaluation and convert to side-to-move perspective. */
static int evaluate_for_side(const Position* pos) {
    int mg = 0;
    int eg = 0;
    int phase = 0;

    for (int side = SIDE_WHITE; side <= SIDE_BLACK; ++side) {
        int sign = (side == SIDE_WHITE) ? 1 : -1;

        for (int piece = PIECE_PAWN; piece <= PIECE_KING; ++piece) {
            Bitboard bb = pos->pieces[side][piece];
            int piece_count = bit_count(bb);

            if (piece != PIECE_KING) {
                phase += g_phase_weights[piece] * piece_count;
            }

            while (bb != 0ULL) {
                int sq = pop_lsb(&bb);
                int pst_sq = (side == SIDE_WHITE) ? sq : mirror_square(sq);
                mg += sign * (g_eval_values[piece] + g_pst_mg[piece][pst_sq]);
                eg += sign * (g_eval_values[piece] + g_pst_eg[piece][pst_sq]);
            }
        }

        if (bit_count(pos->pieces[side][PIECE_BISHOP]) >= 2) {
            mg += sign * 35;
            eg += sign * 45;
        }
    }

    if (phase > 24) {
        phase = 24;
    }

    {
        int eval_white = (mg * phase + eg * (24 - phase)) / 24;
        return (pos->side_to_move == SIDE_WHITE) ? eval_white : -eval_white;
    }
}

/* Public evaluation from White perspective. */
int evaluate_position(const Position* pos) {
    Position white_pov = *pos;
    white_pov.side_to_move = SIDE_WHITE;
    return evaluate_for_side(&white_pov);
}

/* Lightweight killer/history update after quiet beta cutoff. */
static void update_cutoff_heuristics(SearchContext* ctx, const Position* pos, Move move, int depth, int ply) {
    bool quiet = ((move.flags & MOVE_FLAG_CAPTURE) == 0U) && ((move.flags & MOVE_FLAG_PROMOTION) == 0U);
    int bonus;

    if (!quiet || ply < 0 || ply >= MAX_SEARCH_PLY) {
        return;
    }

    if (!move_same(move, ctx->killer_moves[ply][0])) {
        ctx->killer_moves[ply][1] = ctx->killer_moves[ply][0];
        ctx->killer_moves[ply][0] = move;
    }

    bonus = depth * depth;
    if (bonus < 1) {
        bonus = 1;
    }

    {
        int* hist = &ctx->history[pos->side_to_move][move.from][move.to];
        *hist += bonus;
        if (*hist > 8000) {
            *hist = 8000;
        }
    }
}

static int quiescence(const Position* pos, int alpha, int beta, int ply, SearchContext* ctx);

/* Negamax with alpha-beta, TT, PVS, LMR, repetition and 50-move draw handling. */
static int negamax(const Position* pos, int depth, int alpha, int beta, int ply, SearchContext* ctx) {
    int alpha_orig = alpha;
    int beta_orig = beta;
    int result = 0;
    bool pushed = false;
    TTEntry* entry;
    Move tt_move = {0};
    bool in_check;
    int best_score = -INF_SCORE;
    Move best_move = {0};
    MoveList moves;

    if (search_should_stop(ctx)) {
        return 0;
    }

    if (pos->halfmove_clock >= 100) {
        return 0;
    }

    if (is_repetition(ctx, pos->zobrist_key)) {
        return 0;
    }

    if (depth <= 0) {
        return quiescence(pos, alpha, beta, ply, ctx);
    }

    if (ply >= MAX_SEARCH_PLY - 1) {
        return evaluate_for_side(pos);
    }

    ctx->nodes++;

    if (ctx->path_len < MAX_HISTORY_PLY) {
        ctx->path_keys[ctx->path_len++] = pos->zobrist_key;
        pushed = true;
    }

    entry = &g_tt[pos->zobrist_key & (TT_SIZE - 1)];
    if (entry->key == pos->zobrist_key) {
        int tt_score = score_from_tt(entry->score, ply);
        tt_move = entry->best_move;

        if (entry->depth >= depth) {
            if (entry->flag == TT_FLAG_EXACT) {
                result = tt_score;
                goto cleanup;
            }
            if (entry->flag == TT_FLAG_LOWER && tt_score > alpha) {
                alpha = tt_score;
            } else if (entry->flag == TT_FLAG_UPPER && tt_score < beta) {
                beta = tt_score;
            }
            if (alpha >= beta) {
                result = tt_score;
                goto cleanup;
            }
        }
    }

    in_check = engine_in_check(pos, pos->side_to_move);
    if (in_check && depth < (SEARCH_MAX_DEPTH + 2)) {
        depth++;
    }

    generate_legal_moves(pos, &moves);
    if (moves.count == 0) {
        result = in_check ? (-MATE_SCORE + ply) : 0;
        goto cleanup;
    }

    sort_moves(pos, &moves, tt_move, ctx, ply, false);
    best_move = moves.moves[0];

    for (int i = 0; i < moves.count; ++i) {
        Move move = moves.moves[i];
        Position next = *pos;
        int child_depth = depth - 1;
        int score;
        bool tactical = ((move.flags & MOVE_FLAG_CAPTURE) != 0U) || ((move.flags & MOVE_FLAG_PROMOTION) != 0U);
        bool gives_check;

        if (!engine_apply_move(&next, move)) {
            continue;
        }

        gives_check = engine_in_check(&next, next.side_to_move);

        if (!in_check && !gives_check && !tactical && depth >= 4 && i >= 4) {
            child_depth--;
            if (child_depth < 1) {
                child_depth = 1;
            }
        }

        if (i == 0) {
            score = -negamax(&next, child_depth, -beta, -alpha, ply + 1, ctx);
        } else {
            score = -negamax(&next, child_depth, -alpha - 1, -alpha, ply + 1, ctx);
            if (!ctx->stop && score > alpha && score < beta) {
                score = -negamax(&next, depth - 1, -beta, -alpha, ply + 1, ctx);
            } else if (!ctx->stop && child_depth != (depth - 1) && score > alpha) {
                score = -negamax(&next, depth - 1, -beta, -alpha, ply + 1, ctx);
            }
        }

        if (ctx->stop) {
            result = 0;
            goto cleanup;
        }

        if (score > best_score) {
            best_score = score;
            best_move = move;
        }

        if (score > alpha) {
            alpha = score;
        }

        if (alpha >= beta) {
            update_cutoff_heuristics(ctx, pos, move, depth, ply);
            break;
        }
    }

    entry->key = pos->zobrist_key;
    entry->depth = depth;
    entry->score = score_to_tt(best_score, ply);
    entry->best_move = best_move;
    if (best_score <= alpha_orig) {
        entry->flag = TT_FLAG_UPPER;
    } else if (best_score >= beta_orig) {
        entry->flag = TT_FLAG_LOWER;
    } else {
        entry->flag = TT_FLAG_EXACT;
    }

    result = best_score;

cleanup:
    if (pushed && ctx->path_len > 0) {
        ctx->path_len--;
    }
    return result;
}

/* Quiescence search to stabilize tactical leaf evaluations. */
static int quiescence(const Position* pos, int alpha, int beta, int ply, SearchContext* ctx) {
    int result = 0;
    bool pushed = false;
    bool in_check;
    MoveList moves;
    int stand_pat;
    int best_score;
    Move tt_move = {0};

    if (search_should_stop(ctx)) {
        return 0;
    }

    if (pos->halfmove_clock >= 100) {
        return 0;
    }

    if (is_repetition(ctx, pos->zobrist_key)) {
        return 0;
    }

    if (ply >= MAX_HISTORY_PLY - 1) {
        return evaluate_for_side(pos);
    }

    ctx->nodes++;

    if (ctx->path_len < MAX_HISTORY_PLY) {
        ctx->path_keys[ctx->path_len++] = pos->zobrist_key;
        pushed = true;
    }

    in_check = engine_in_check(pos, pos->side_to_move);
    stand_pat = evaluate_for_side(pos);
    best_score = stand_pat;

    if (!in_check) {
        if (stand_pat >= beta) {
            result = stand_pat;
            goto cleanup;
        }
        if (stand_pat > alpha) {
            alpha = stand_pat;
        }
    }

    generate_legal_moves(pos, &moves);
    if (moves.count == 0) {
        result = in_check ? (-MATE_SCORE + ply) : 0;
        goto cleanup;
    }

    sort_moves(pos, &moves, tt_move, ctx, ply, true);

    for (int i = 0; i < moves.count; ++i) {
        Move move = moves.moves[i];
        Position next = *pos;
        int score;

        if (!in_check &&
            (move.flags & MOVE_FLAG_CAPTURE) == 0U &&
            (move.flags & MOVE_FLAG_PROMOTION) == 0U) {
            continue;
        }

        if (!engine_apply_move(&next, move)) {
            continue;
        }

        score = -quiescence(&next, -beta, -alpha, ply + 1, ctx);
        if (ctx->stop) {
            result = 0;
            goto cleanup;
        }

        if (score > best_score) {
            best_score = score;
        }
        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break;
        }
    }

    result = best_score;

cleanup:
    if (pushed && ctx->path_len > 0) {
        ctx->path_len--;
    }
    return result;
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
    if (local_limits.randomness < 0) {
        local_limits.randomness = 0;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.limits = local_limits;
    ctx.start_ms = now_ms();
    ctx.path_keys[0] = pos->zobrist_key;
    ctx.path_len = 1;

    generate_legal_moves(pos, &root_moves);

    memset(&result, 0, sizeof(result));
    result.best_move.promotion = PIECE_NONE;

    if (root_moves.count == 0) {
        *out_result = result;
        return;
    }

    for (int i = 0; i < MAX_MOVES; ++i) {
        root_scores[i] = -INF_SCORE;
    }

    best_move = root_moves.moves[0];

    for (int depth = 1; depth <= local_limits.depth; ++depth) {
        Move tt_move = {0};
        TTEntry* root_entry = &g_tt[pos->zobrist_key & (TT_SIZE - 1)];
        MoveList depth_moves = root_moves;
        int depth_best_score = -INF_SCORE;
        Move depth_best_move = depth_moves.moves[0];
        bool completed = false;

        if (search_should_stop(&ctx)) {
            break;
        }

        if (root_entry->key == pos->zobrist_key) {
            tt_move = root_entry->best_move;
        }

        sort_moves(pos, &depth_moves, tt_move, &ctx, 0, false);

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

            completed = true;

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

        if (ctx.stop || !completed) {
            break;
        }

        best_score = depth_best_score;
        best_move = depth_best_move;
        result.depth_reached = depth;
    }

    if (!ctx.stop &&
        local_limits.randomness > 0 &&
        root_moves.count > 1 &&
        best_score > -MATE_BOUND &&
        best_score < MATE_BOUND) {
        Move candidates[MAX_MOVES];
        int candidate_count = 0;

        for (int i = 0; i < root_moves.count; ++i) {
            if (root_scores[i] > -INF_SCORE / 2 &&
                root_scores[i] >= (best_score - local_limits.randomness)) {
                candidates[candidate_count++] = root_moves.moves[i];
            }
        }

        if (candidate_count > 1) {
            best_move = candidates[rand() % candidate_count];
        }
    }

    if (best_score == -INF_SCORE) {
        Position next = *pos;
        if (engine_apply_move(&next, best_move)) {
            best_score = -evaluate_for_side(&next);
        } else {
            best_score = 0;
        }
    }

    result.best_move = best_move;
    result.score = best_score;
    result.nodes = ctx.nodes;

    *out_result = result;
}
