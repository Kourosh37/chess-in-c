#include "engine.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/* Transposition-table size (must be power-of-two for mask indexing). */
#define TT_SIZE (1U << 20)

/* Search score sentinels. */
#define INF_SCORE 300000
#define MATE_SCORE 250000
#define MATE_BOUND (MATE_SCORE - 1024)

/* Search limits and internal stack caps. */
#define SEARCH_MIN_DEPTH 1
#define SEARCH_MAX_DEPTH 14
#define MAX_SEARCH_PLY 128
#define MAX_HISTORY_PLY 256
#define ASPIRATION_BASE_WINDOW 35
#define ASPIRATION_MIN_DEPTH 3
#define ASPIRATION_MAX_WINDOW 1200

/* Castling rights bit layout (KQkq) used by evaluation heuristics. */
#define CASTLE_WHITE_KING  0x01
#define CASTLE_WHITE_QUEEN 0x02
#define CASTLE_BLACK_KING  0x04
#define CASTLE_BLACK_QUEEN 0x08

/* Built-in opening book limits. */
#define OPENING_BOOK_MAX_ENTRIES 1024
#define OPENING_BOOK_MAX_CANDIDATES 64

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

typedef struct OpeningBookSeed {
    const char* line;
    int weight;
} OpeningBookSeed;

typedef struct OpeningBookEntry {
    uint64_t key;
    Move move;
    int weight;
} OpeningBookEntry;

static TTEntry g_tt[TT_SIZE];
static OpeningBookEntry g_opening_book[OPENING_BOOK_MAX_ENTRIES];
static int g_opening_book_count = 0;
static bool g_opening_book_ready = false;

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

/* Curated practical opening lines (UCI format) with relative popularity weights. */
static const OpeningBookSeed g_opening_book_seeds[] = {
    {"e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7", 90},
    {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3 d7d6 e1g1 e8g8", 88},
    {"e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 f8c5 d4b3 c5b6 b1c3", 68},
    {"e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4", 60},
    {"e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6", 95},
    {"e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g7g6 b1c3 f8g7", 74},
    {"e2e4 c7c5 c2c3 d7d5 e4d5 d8d5 d2d4", 56},
    {"e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 e4e5 f6d7 g1f3 c7c5", 82},
    {"e2e4 e7e6 d2d4 d7d5 b1c3 f8b4 e4e5 c7c5 a2a3 b4c3 b2c3", 63},
    {"e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5", 84},
    {"e2e4 c7c6 d2d4 d7d5 e4e5 c8f5 g1f3", 57},
    {"d2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8", 92},
    {"d2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4", 77},
    {"d2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5 e1g1", 52},
    {"d2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5", 79},
    {"d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8", 86},
    {"d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 f2f4", 55},
    {"c2c4 e7e5 b1c3 g8f6 g2g3 d7d5 c4d5 f6d5 f1g2", 58},
    {"g1f3 d7d5 c2c4 e7e6 g2g3 g8f6 f1g2 f8e7 e1g1", 54},
    {"d2d4 d7d5 g1f3 g8f6 c1f4 c7c5 e2e3 b8c6 c2c3", 61},
    {"e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 g1f3 f8g7", 70},
    {"e2e4 g7g6 d2d4 f8g7 b1c3 d7d6 g1f3", 46},
    {"e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 d7d6", 72},
    {"e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e6", 69},
    {"d2d4 g8f6 c2c4 e7e6 g1f3 d7d5 b1c3 f8e7 c1g5 e8g8", 73}
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

/* Returns single-bit mask for one board square. */
static Bitboard bb_square(int square) {
    return 1ULL << square;
}

/* Returns full file mask (a-file when file = 0). */
static Bitboard file_mask(int file) {
    return 0x0101010101010101ULL << file;
}

/* True when side still has at least one piece other than king/pawns. */
static bool side_has_non_pawn_material(const Position* pos, Side side) {
    return (pos->pieces[side][PIECE_KNIGHT] |
            pos->pieces[side][PIECE_BISHOP] |
            pos->pieces[side][PIECE_ROOK] |
            pos->pieces[side][PIECE_QUEEN]) != 0ULL;
}

/* Adds or merges one opening-book entry for a position key. */
static void opening_book_add_entry(uint64_t key, Move move, int weight) {
    int clamped_weight = (weight > 0) ? weight : 1;

    for (int i = 0; i < g_opening_book_count; ++i) {
        if (g_opening_book[i].key == key && move_same(g_opening_book[i].move, move)) {
            g_opening_book[i].weight += clamped_weight;
            if (g_opening_book[i].weight > 10000) {
                g_opening_book[i].weight = 10000;
            }
            return;
        }
    }

    if (g_opening_book_count >= OPENING_BOOK_MAX_ENTRIES) {
        return;
    }

    g_opening_book[g_opening_book_count].key = key;
    g_opening_book[g_opening_book_count].move = move;
    g_opening_book[g_opening_book_count].weight = clamped_weight;
    g_opening_book_count++;
}

/* Parses one UCI token from a whitespace-separated line. */
static bool parse_next_uci_token(const char** cursor, char out[6]) {
    const char* p;
    int len = 0;

    if (cursor == NULL || *cursor == NULL || out == NULL) {
        return false;
    }

    p = *cursor;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }

    if (*p == '\0') {
        *cursor = p;
        return false;
    }

    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (len >= 5) {
            return false;
        }
        out[len++] = *p;
        p++;
    }
    out[len] = '\0';
    *cursor = p;

    return len >= 4 && len <= 5;
}

/* Builds key->move opening map from curated opening seeds. */
static void opening_book_build(void) {
    if (g_opening_book_ready) {
        return;
    }

    g_opening_book_ready = true;
    g_opening_book_count = 0;

    for (int i = 0; i < (int)(sizeof(g_opening_book_seeds) / sizeof(g_opening_book_seeds[0])); ++i) {
        Position pos;
        const char* cursor = g_opening_book_seeds[i].line;
        int ply = 0;

        position_set_start(&pos);

        while (true) {
            char token[6];
            Move parsed;
            MoveList legal;
            Move canonical = {0};
            bool found = false;

            if (!parse_next_uci_token(&cursor, token)) {
                break;
            }
            if (!move_from_uci(token, &parsed)) {
                break;
            }

            generate_legal_moves(&pos, &legal);
            for (int m = 0; m < legal.count; ++m) {
                if (move_same(legal.moves[m], parsed)) {
                    canonical = legal.moves[m];
                    found = true;
                    break;
                }
            }
            if (!found) {
                break;
            }

            opening_book_add_entry(pos.zobrist_key, canonical, g_opening_book_seeds[i].weight - (ply / 2));
            if (!engine_apply_move(&pos, canonical)) {
                break;
            }

            ply++;
            if (ply >= 24) {
                break;
            }
        }
    }
}

/* Probes opening book and returns one candidate move when available. */
static bool opening_book_pick_move(const Position* pos, int randomness, Move* out_move) {
    typedef struct OpeningCandidate {
        Move move;
        int weight;
    } OpeningCandidate;

    OpeningCandidate candidates[OPENING_BOOK_MAX_CANDIDATES];
    MoveList legal;
    int candidate_count = 0;

    if (pos == NULL || out_move == NULL) {
        return false;
    }

    if (pos->fullmove_number > 12 || pos->halfmove_clock > 10) {
        return false;
    }

    if (bit_count(pos->pieces[SIDE_WHITE][PIECE_QUEEN] | pos->pieces[SIDE_BLACK][PIECE_QUEEN]) < 2) {
        return false;
    }

    opening_book_build();
    if (g_opening_book_count <= 0) {
        return false;
    }

    generate_legal_moves(pos, &legal);
    if (legal.count <= 0) {
        return false;
    }

    for (int i = 0; i < g_opening_book_count; ++i) {
        if (g_opening_book[i].key != pos->zobrist_key) {
            continue;
        }

        for (int m = 0; m < legal.count; ++m) {
            if (!move_same(g_opening_book[i].move, legal.moves[m])) {
                continue;
            }

            for (int c = 0; c <= candidate_count; ++c) {
                if (c == candidate_count) {
                    if (candidate_count < OPENING_BOOK_MAX_CANDIDATES) {
                        candidates[candidate_count].move = legal.moves[m];
                        candidates[candidate_count].weight = g_opening_book[i].weight;
                        candidate_count++;
                    }
                    break;
                }

                if (move_same(candidates[c].move, legal.moves[m])) {
                    candidates[c].weight += g_opening_book[i].weight;
                    break;
                }
            }
        }
    }

    if (candidate_count <= 0) {
        return false;
    }

    if (randomness <= 0 || candidate_count == 1) {
        int best = 0;
        for (int i = 1; i < candidate_count; ++i) {
            if (candidates[i].weight > candidates[best].weight) {
                best = i;
            }
        }
        *out_move = candidates[best].move;
        return true;
    }

    {
        int total = 0;
        int pick;
        int sum = 0;

        for (int i = 0; i < candidate_count; ++i) {
            int w = candidates[i].weight;
            if (w < 1) {
                w = 1;
            }
            total += w;
        }

        if (total <= 0) {
            *out_move = candidates[0].move;
            return true;
        }

        pick = rand() % total;
        for (int i = 0; i < candidate_count; ++i) {
            int w = candidates[i].weight;
            if (w < 1) {
                w = 1;
            }

            sum += w;
            if (pick < sum) {
                *out_move = candidates[i].move;
                return true;
            }
        }
    }

    *out_move = candidates[candidate_count - 1].move;
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

    if (!qsearch && (move.flags & (MOVE_FLAG_KING_CASTLE | MOVE_FLAG_QUEEN_CASTLE)) != 0U) {
        score += 2200;
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

/* True when king already reached classical castled squares. */
static bool side_is_castled(const Position* pos, Side side) {
    Bitboard king = pos->pieces[side][PIECE_KING];
    if (side == SIDE_WHITE) {
        return (king & (bb_square(6) | bb_square(2))) != 0ULL;
    }
    return (king & (bb_square(62) | bb_square(58))) != 0ULL;
}

/* Pawn-structure evaluation for one side. */
static int pawn_structure_score(const Position* pos, Side side) {
    Side them = (side == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    Bitboard pawns = pos->pieces[side][PIECE_PAWN];
    Bitboard enemy_pawns = pos->pieces[them][PIECE_PAWN];
    int file_counts[8] = {0};
    int score = 0;
    Bitboard scan = pawns;

    while (scan != 0ULL) {
        int sq = pop_lsb(&scan);
        file_counts[sq & 7]++;
    }

    for (int file = 0; file < 8; ++file) {
        if (file_counts[file] > 1) {
            score -= 14 * (file_counts[file] - 1);
        }
    }

    scan = pawns;
    while (scan != 0ULL) {
        int sq = pop_lsb(&scan);
        int file = sq & 7;
        int rank = sq >> 3;
        bool isolated = ((file == 0 || file_counts[file - 1] == 0) &&
                         (file == 7 || file_counts[file + 1] == 0));
        bool passed = true;
        bool supported = false;
        Bitboard ep = enemy_pawns;

        if (isolated) {
            score -= 11;
        }

        while (ep != 0ULL) {
            int esq = pop_lsb(&ep);
            int efile = esq & 7;
            int erank = esq >> 3;

            if (efile < file - 1 || efile > file + 1) {
                continue;
            }

            if ((side == SIDE_WHITE && erank > rank) ||
                (side == SIDE_BLACK && erank < rank)) {
                passed = false;
                break;
            }
        }

        if (side == SIDE_WHITE) {
            if (rank > 0 && file > 0 && (pawns & bb_square((rank - 1) * 8 + (file - 1))) != 0ULL) {
                supported = true;
            }
            if (rank > 0 && file < 7 && (pawns & bb_square((rank - 1) * 8 + (file + 1))) != 0ULL) {
                supported = true;
            }
        } else {
            if (rank < 7 && file > 0 && (pawns & bb_square((rank + 1) * 8 + (file - 1))) != 0ULL) {
                supported = true;
            }
            if (rank < 7 && file < 7 && (pawns & bb_square((rank + 1) * 8 + (file + 1))) != 0ULL) {
                supported = true;
            }
        }

        if (supported) {
            score += 4;
        }

        if (passed) {
            int advance = (side == SIDE_WHITE) ? rank : (7 - rank);
            score += 18 + advance * 8;
        }
    }

    return score;
}

/* Piece activity and rook file-quality evaluation for one side. */
static int mobility_score(const Position* pos, Side side) {
    Side them = (side == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    Bitboard own = pos->occupied[side];
    int score = 0;
    Bitboard bb;

    bb = pos->pieces[side][PIECE_KNIGHT];
    while (bb != 0ULL) {
        int sq = pop_lsb(&bb);
        score += bit_count(engine_get_knight_attacks(sq) & ~own) * 4;
    }

    bb = pos->pieces[side][PIECE_BISHOP];
    while (bb != 0ULL) {
        int sq = pop_lsb(&bb);
        score += bit_count(engine_get_bishop_attacks(sq, pos->all_occupied) & ~own) * 4;
    }

    bb = pos->pieces[side][PIECE_ROOK];
    while (bb != 0ULL) {
        int sq = pop_lsb(&bb);
        Bitboard mask = file_mask(sq & 7);

        score += bit_count(engine_get_rook_attacks(sq, pos->all_occupied) & ~own) * 2;

        if ((pos->pieces[side][PIECE_PAWN] & mask) == 0ULL) {
            score += ((pos->pieces[them][PIECE_PAWN] & mask) == 0ULL) ? 18 : 9;
        }
    }

    bb = pos->pieces[side][PIECE_QUEEN];
    while (bb != 0ULL) {
        int sq = pop_lsb(&bb);
        Bitboard attacks = engine_get_bishop_attacks(sq, pos->all_occupied) |
                           engine_get_rook_attacks(sq, pos->all_occupied);
        score += bit_count(attacks & ~own);
    }

    return score;
}

/* King safety and castling incentives for one side. */
static int king_safety_score(const Position* pos, Side side, int phase) {
    Side them = (side == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    int king_sq = engine_find_king_square(pos, side);
    int score = 0;

    if (king_sq < 0) {
        return 0;
    }

    if (side_is_castled(pos, side)) {
        score += 52;
    } else {
        bool rights = false;
        if (side == SIDE_WHITE) {
            rights = (pos->castling_rights & (CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN)) != 0U;
            if (king_sq == 4 || king_sq == 3 || king_sq == 5) {
                score -= 24;
            }
        } else {
            rights = (pos->castling_rights & (CASTLE_BLACK_KING | CASTLE_BLACK_QUEEN)) != 0U;
            if (king_sq == 60 || king_sq == 59 || king_sq == 61) {
                score -= 24;
            }
        }

        if (rights) {
            score += 6;
        } else {
            score -= 34;
        }
    }

    {
        Bitboard pawns = pos->pieces[side][PIECE_PAWN];
        int file = king_sq & 7;
        int rank = king_sq >> 3;
        int shield_rank = rank + ((side == SIDE_WHITE) ? 1 : -1);

        for (int df = -1; df <= 1; ++df) {
            int f = file + df;
            if (f < 0 || f > 7 || shield_rank < 0 || shield_rank > 7) {
                continue;
            }

            if ((pawns & bb_square(shield_rank * 8 + f)) != 0ULL) {
                score += 7;
            } else {
                score -= 9;
            }
        }
    }

    {
        Bitboard zone = engine_get_king_attacks(king_sq) | bb_square(king_sq);
        int attackers = 0;
        Bitboard bb_attackers;

        bb_attackers = pos->pieces[them][PIECE_PAWN];
        while (bb_attackers != 0ULL) {
            int sq = pop_lsb(&bb_attackers);
            if ((engine_get_pawn_attacks(them, sq) & zone) != 0ULL) {
                attackers++;
            }
        }

        bb_attackers = pos->pieces[them][PIECE_KNIGHT];
        while (bb_attackers != 0ULL) {
            int sq = pop_lsb(&bb_attackers);
            if ((engine_get_knight_attacks(sq) & zone) != 0ULL) {
                attackers++;
            }
        }

        bb_attackers = pos->pieces[them][PIECE_BISHOP];
        while (bb_attackers != 0ULL) {
            int sq = pop_lsb(&bb_attackers);
            if ((engine_get_bishop_attacks(sq, pos->all_occupied) & zone) != 0ULL) {
                attackers++;
            }
        }

        bb_attackers = pos->pieces[them][PIECE_ROOK];
        while (bb_attackers != 0ULL) {
            int sq = pop_lsb(&bb_attackers);
            if ((engine_get_rook_attacks(sq, pos->all_occupied) & zone) != 0ULL) {
                attackers++;
            }
        }

        bb_attackers = pos->pieces[them][PIECE_QUEEN];
        while (bb_attackers != 0ULL) {
            int sq = pop_lsb(&bb_attackers);
            Bitboard attacks = engine_get_bishop_attacks(sq, pos->all_occupied) |
                               engine_get_rook_attacks(sq, pos->all_occupied);
            if ((attacks & zone) != 0ULL) {
                attackers++;
            }
        }

        score -= attackers * ((phase >= 14) ? 11 : 6);
    }

    return score;
}

/* Opening development incentives to improve early move choices. */
static int opening_development_score(const Position* pos, Side side, int phase) {
    int score = 0;
    int undeveloped = 0;
    bool queen_moved = false;

    if (phase < 12) {
        return 0;
    }

    if (side == SIDE_WHITE) {
        if ((pos->pieces[side][PIECE_KNIGHT] & bb_square(1)) != 0ULL) {
            undeveloped++;
            score -= 11;
        }
        if ((pos->pieces[side][PIECE_KNIGHT] & bb_square(6)) != 0ULL) {
            undeveloped++;
            score -= 11;
        }
        if ((pos->pieces[side][PIECE_BISHOP] & bb_square(2)) != 0ULL) {
            undeveloped++;
            score -= 9;
        }
        if ((pos->pieces[side][PIECE_BISHOP] & bb_square(5)) != 0ULL) {
            undeveloped++;
            score -= 9;
        }
        if ((pos->pieces[side][PIECE_PAWN] & bb_square(11)) == 0ULL) {
            score += 4;
        }
        if ((pos->pieces[side][PIECE_PAWN] & bb_square(12)) == 0ULL) {
            score += 6;
        }
        queen_moved = (pos->pieces[side][PIECE_QUEEN] & bb_square(3)) == 0ULL;
    } else {
        if ((pos->pieces[side][PIECE_KNIGHT] & bb_square(57)) != 0ULL) {
            undeveloped++;
            score -= 11;
        }
        if ((pos->pieces[side][PIECE_KNIGHT] & bb_square(62)) != 0ULL) {
            undeveloped++;
            score -= 11;
        }
        if ((pos->pieces[side][PIECE_BISHOP] & bb_square(58)) != 0ULL) {
            undeveloped++;
            score -= 9;
        }
        if ((pos->pieces[side][PIECE_BISHOP] & bb_square(61)) != 0ULL) {
            undeveloped++;
            score -= 9;
        }
        if ((pos->pieces[side][PIECE_PAWN] & bb_square(51)) == 0ULL) {
            score += 4;
        }
        if ((pos->pieces[side][PIECE_PAWN] & bb_square(52)) == 0ULL) {
            score += 6;
        }
        queen_moved = (pos->pieces[side][PIECE_QUEEN] & bb_square(59)) == 0ULL;
    }

    if (queen_moved && undeveloped >= 3) {
        score -= 12;
    }

    return score;
}

/* Blend MG/EG PST-evaluation and convert to side-to-move perspective. */
static int evaluate_for_side(const Position* pos) {
    int mg = 0;
    int eg = 0;
    int phase = 0;

    for (int side = SIDE_WHITE; side <= SIDE_BLACK; ++side) {
        for (int piece = PIECE_PAWN; piece <= PIECE_QUEEN; ++piece) {
            phase += g_phase_weights[piece] * bit_count(pos->pieces[side][piece]);
        }
    }

    if (phase > 24) {
        phase = 24;
    }

    for (int side = SIDE_WHITE; side <= SIDE_BLACK; ++side) {
        int sign = (side == SIDE_WHITE) ? 1 : -1;

        for (int piece = PIECE_PAWN; piece <= PIECE_KING; ++piece) {
            Bitboard bb = pos->pieces[side][piece];

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

        {
            int pawn = pawn_structure_score(pos, (Side)side);
            int mobility = mobility_score(pos, (Side)side);
            int king = king_safety_score(pos, (Side)side, phase);
            int development = opening_development_score(pos, (Side)side, phase);

            mg += sign * (pawn + mobility + king + development);
            eg += sign * (pawn + mobility + (king / 2));
        }
    }

    {
        int eval_white = (mg * phase + eg * (24 - phase)) / 24;
        eval_white += (pos->side_to_move == SIDE_WHITE) ? 10 : -10;
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
    int static_eval = 0;
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

    static_eval = evaluate_for_side(pos);

    if (!in_check && depth <= 2 && static_eval + (120 * depth) <= alpha) {
        result = quiescence(pos, alpha, beta, ply, ctx);
        goto cleanup;
    }

    if (!in_check && depth <= 3 && beta < MATE_BOUND) {
        int rfp_margin = 85 * depth;
        if (static_eval - rfp_margin >= beta) {
            result = static_eval - rfp_margin;
            goto cleanup;
        }
    }

    if (depth >= 3 &&
        !in_check &&
        beta < MATE_BOUND &&
        static_eval >= (beta - 40) &&
        side_has_non_pawn_material(pos, pos->side_to_move)) {
        Position null_pos = *pos;
        int reduction = 2 + ((depth >= 7) ? 1 : 0);
        int score;

        null_pos.side_to_move = (null_pos.side_to_move == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
        null_pos.en_passant_square = -1;
        null_pos.halfmove_clock++;
        if (null_pos.side_to_move == SIDE_WHITE) {
            null_pos.fullmove_number++;
        }
        null_pos.zobrist_key = position_compute_zobrist(&null_pos);

        score = -negamax(&null_pos, depth - 1 - reduction, -beta, -beta + 1, ply + 1, ctx);
        if (ctx->stop) {
            result = 0;
            goto cleanup;
        }
        if (score >= beta) {
            result = beta;
            goto cleanup;
        }
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
        bool quiet_non_castle = !tactical &&
            (move.flags & (MOVE_FLAG_KING_CASTLE | MOVE_FLAG_QUEEN_CASTLE)) == 0U;
        bool gives_check;

        if (!engine_apply_move(&next, move)) {
            continue;
        }

        gives_check = engine_in_check(&next, next.side_to_move);

        if (!in_check && !gives_check && quiet_non_castle && i > 0) {
            if (depth <= 3) {
                int lmp_threshold = 4 + (depth * depth);
                int futility_margin = 85 * depth + ((i >= 6) ? 30 : 0);

                if (i >= lmp_threshold) {
                    continue;
                }
                if (static_eval + futility_margin <= alpha) {
                    continue;
                }
            }
        }

        if (!in_check &&
            !gives_check &&
            quiet_non_castle &&
            depth >= 4 &&
            i >= 3) {
            int reduction = 1;
            if (depth >= 8) {
                reduction++;
            }
            if (i >= 8) {
                reduction++;
            }

            child_depth -= reduction;
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

    {
        uint8_t new_flag;
        bool exact = false;

        if (best_score <= alpha_orig) {
            new_flag = TT_FLAG_UPPER;
        } else if (best_score >= beta_orig) {
            new_flag = TT_FLAG_LOWER;
        } else {
            new_flag = TT_FLAG_EXACT;
            exact = true;
        }

        if (entry->key != pos->zobrist_key || depth + (exact ? 1 : 0) >= entry->depth) {
            entry->key = pos->zobrist_key;
            entry->depth = depth;
            entry->score = score_to_tt(best_score, ply);
            entry->best_move = best_move;
            entry->flag = new_flag;
        }
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

        if (!in_check &&
            (move.flags & MOVE_FLAG_CAPTURE) != 0U &&
            (move.flags & MOVE_FLAG_PROMOTION) == 0U) {
            int capture_gain = score_capture(pos, move) / 16;
            if (stand_pat + capture_gain + 90 < alpha) {
                continue;
            }
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

    memset(&result, 0, sizeof(result));
    result.best_move.promotion = PIECE_NONE;

    if (opening_book_pick_move(pos, local_limits.randomness, &result.best_move)) {
        Position next = *pos;
        if (engine_apply_move(&next, result.best_move)) {
            result.score = -evaluate_for_side(&next);
        } else {
            result.score = 0;
        }
        result.depth_reached = 0;
        result.nodes = 0;
        *out_result = result;
        return;
    }

    generate_legal_moves(pos, &root_moves);

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
        int aspiration_window = ASPIRATION_BASE_WINDOW + (depth * 8);
        bool use_aspiration = (depth >= ASPIRATION_MIN_DEPTH &&
                               best_score > -MATE_BOUND &&
                               best_score < MATE_BOUND);
        int alpha = -INF_SCORE;
        int beta = INF_SCORE;
        bool depth_completed = false;
        int depth_completed_score = -INF_SCORE;
        Move depth_completed_move = root_moves.moves[0];

        if (search_should_stop(&ctx)) {
            break;
        }

        if (root_entry->key == pos->zobrist_key) {
            tt_move = root_entry->best_move;
        }

        if (use_aspiration) {
            alpha = best_score - aspiration_window;
            beta = best_score + aspiration_window;

            if (alpha < -INF_SCORE) {
                alpha = -INF_SCORE;
            }
            if (beta > INF_SCORE) {
                beta = INF_SCORE;
            }
        }

        while (true) {
            MoveList depth_moves = root_moves;
            int depth_best_score = -INF_SCORE;
            Move depth_best_move = depth_moves.moves[0];
            bool completed = false;
            int search_alpha = alpha;
            int search_beta = beta;

            sort_moves(pos, &depth_moves, tt_move, &ctx, 0, false);

            for (int i = 0; i < depth_moves.count; ++i) {
                Position next = *pos;
                int score;

                if (!engine_apply_move(&next, depth_moves.moves[i])) {
                    continue;
                }

                if (i == 0) {
                    score = -negamax(&next, depth - 1, -search_beta, -search_alpha, 1, &ctx);
                } else {
                    score = -negamax(&next, depth - 1, -search_alpha - 1, -search_alpha, 1, &ctx);
                    if (!ctx.stop && score > search_alpha && score < search_beta) {
                        score = -negamax(&next, depth - 1, -search_beta, -search_alpha, 1, &ctx);
                    }
                }
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

                if (score > search_alpha) {
                    search_alpha = score;
                }

                if (search_alpha >= search_beta) {
                    break;
                }
            }

            if (ctx.stop || !completed) {
                depth_completed = false;
                break;
            }

            depth_completed = true;
            depth_completed_score = depth_best_score;
            depth_completed_move = depth_best_move;

            if (!use_aspiration) {
                break;
            }

            if (depth_best_score <= alpha || depth_best_score >= beta) {
                aspiration_window *= 2;
                if (aspiration_window > ASPIRATION_MAX_WINDOW) {
                    use_aspiration = false;
                    alpha = -INF_SCORE;
                    beta = INF_SCORE;
                    continue;
                }

                alpha = best_score - aspiration_window;
                beta = best_score + aspiration_window;
                if (alpha < -INF_SCORE) {
                    alpha = -INF_SCORE;
                }
                if (beta > INF_SCORE) {
                    beta = INF_SCORE;
                }
                continue;
            }

            break;
        }

        if (ctx.stop || !depth_completed) {
            break;
        }

        best_score = depth_completed_score;
        best_move = depth_completed_move;
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
