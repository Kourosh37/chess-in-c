#include "engine.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

/* File masks used for pawn attack shifts to avoid board wrap-around. */
#define NOT_FILE_A 0xFEFEFEFEFEFEFEFEULL
#define NOT_FILE_H 0x7F7F7F7F7F7F7F7FULL

/* Precomputed attack tables. */
static Bitboard g_knight_attacks[BOARD_SQUARES];
static Bitboard g_king_attacks[BOARD_SQUARES];
static Bitboard g_pawn_attacks[2][BOARD_SQUARES];

/* Zobrist hash random tables. */
static uint64_t g_zobrist_piece[2][6][BOARD_SQUARES];
static uint64_t g_zobrist_castling[16];
static uint64_t g_zobrist_ep_file[8];
static uint64_t g_zobrist_side;

static bool g_engine_initialized = false;
static uint64_t g_rng_state = 0xA5A5A5A5D3C1F27BULL;

/* Returns a single-bit bitboard for one square index. */
static Bitboard bb_square(int square) {
    return 1ULL << square;
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

/* Xorshift-based pseudo-random generator for zobrist initialization. */
static uint64_t rng_next_u64(void) {
    uint64_t x = g_rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng_state = x;
    return x * 2685821657736338717ULL;
}

/* True when square index is in [0, 63]. */
static bool is_square_on_board(int square) {
    return square >= 0 && square < BOARD_SQUARES;
}

/* Parses one piece designator from FEN board field. */
static bool fen_piece_from_char(char ch, Side* out_side, PieceType* out_piece) {
    if (out_side == NULL || out_piece == NULL) {
        return false;
    }

    switch (ch) {
        case 'P':
            *out_side = SIDE_WHITE;
            *out_piece = PIECE_PAWN;
            return true;
        case 'N':
            *out_side = SIDE_WHITE;
            *out_piece = PIECE_KNIGHT;
            return true;
        case 'B':
            *out_side = SIDE_WHITE;
            *out_piece = PIECE_BISHOP;
            return true;
        case 'R':
            *out_side = SIDE_WHITE;
            *out_piece = PIECE_ROOK;
            return true;
        case 'Q':
            *out_side = SIDE_WHITE;
            *out_piece = PIECE_QUEEN;
            return true;
        case 'K':
            *out_side = SIDE_WHITE;
            *out_piece = PIECE_KING;
            return true;
        case 'p':
            *out_side = SIDE_BLACK;
            *out_piece = PIECE_PAWN;
            return true;
        case 'n':
            *out_side = SIDE_BLACK;
            *out_piece = PIECE_KNIGHT;
            return true;
        case 'b':
            *out_side = SIDE_BLACK;
            *out_piece = PIECE_BISHOP;
            return true;
        case 'r':
            *out_side = SIDE_BLACK;
            *out_piece = PIECE_ROOK;
            return true;
        case 'q':
            *out_side = SIDE_BLACK;
            *out_piece = PIECE_QUEEN;
            return true;
        case 'k':
            *out_side = SIDE_BLACK;
            *out_piece = PIECE_KING;
            return true;
        default:
            break;
    }

    return false;
}

/* Parses one non-negative integer field from FEN. */
static bool fen_parse_uint_field(const char** cursor, unsigned* out_value) {
    const char* p;
    unsigned value = 0U;
    bool has_digit = false;

    if (cursor == NULL || *cursor == NULL || out_value == NULL) {
        return false;
    }

    p = *cursor;
    while (*p == ' ') {
        p++;
    }

    while (isdigit((unsigned char)*p) != 0) {
        has_digit = true;
        value = value * 10U + (unsigned)(*p - '0');
        p++;
    }

    if (!has_digit) {
        return false;
    }

    *cursor = p;
    *out_value = value;
    return true;
}

/* Builds knight attack lookup table for all squares. */
static void init_knight_attacks(void) {
    static const int offsets[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
    };

    for (int sq = 0; sq < BOARD_SQUARES; ++sq) {
        int file = sq & 7;
        int rank = sq >> 3;
        Bitboard attacks = 0ULL;

        for (int i = 0; i < 8; ++i) {
            int nf = file + offsets[i][0];
            int nr = rank + offsets[i][1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
                attacks |= bb_square((nr << 3) | nf);
            }
        }

        g_knight_attacks[sq] = attacks;
    }
}

/* Builds king attack lookup table for all squares. */
static void init_king_attacks(void) {
    for (int sq = 0; sq < BOARD_SQUARES; ++sq) {
        int file = sq & 7;
        int rank = sq >> 3;
        Bitboard attacks = 0ULL;

        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) {
                    continue;
                }

                {
                    int nf = file + df;
                    int nr = rank + dr;
                    if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
                        attacks |= bb_square((nr << 3) | nf);
                    }
                }
            }
        }

        g_king_attacks[sq] = attacks;
    }
}

/* Builds pawn capture attack lookup table for both sides. */
static void init_pawn_attacks(void) {
    for (int sq = 0; sq < BOARD_SQUARES; ++sq) {
        int file = sq & 7;
        int rank = sq >> 3;

        Bitboard white_attacks = 0ULL;
        Bitboard black_attacks = 0ULL;

        if (file > 0 && rank < 7) {
            white_attacks |= bb_square(sq + 7);
        }
        if (file < 7 && rank < 7) {
            white_attacks |= bb_square(sq + 9);
        }

        if (file > 0 && rank > 0) {
            black_attacks |= bb_square(sq - 9);
        }
        if (file < 7 && rank > 0) {
            black_attacks |= bb_square(sq - 7);
        }

        g_pawn_attacks[SIDE_WHITE][sq] = white_attacks;
        g_pawn_attacks[SIDE_BLACK][sq] = black_attacks;
    }
}

/* Seeds all zobrist lookup tables. */
static void init_zobrist(void) {
    for (int side = 0; side < 2; ++side) {
        for (int piece = 0; piece < 6; ++piece) {
            for (int sq = 0; sq < BOARD_SQUARES; ++sq) {
                g_zobrist_piece[side][piece][sq] = rng_next_u64();
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        g_zobrist_castling[i] = rng_next_u64();
    }

    for (int i = 0; i < 8; ++i) {
        g_zobrist_ep_file[i] = rng_next_u64();
    }

    g_zobrist_side = rng_next_u64();
}

/* Engine one-time initialization for tables/hashes. */
void engine_init(void) {
    if (g_engine_initialized) {
        return;
    }

    g_rng_state ^= (uint64_t)time(NULL);

    init_knight_attacks();
    init_king_attacks();
    init_pawn_attacks();
    init_zobrist();

    g_engine_initialized = true;
}

/* Clears position object to a deterministic empty state. */
void position_set_empty(Position* pos) {
    memset(pos, 0, sizeof(*pos));
    pos->en_passant_square = -1;
    pos->side_to_move = SIDE_WHITE;
    pos->fullmove_number = 1;
}

/* Recomputes side and total occupancy bitboards from piece bitboards. */
void position_refresh_occupancy(Position* pos) {
    pos->occupied[SIDE_WHITE] = 0ULL;
    pos->occupied[SIDE_BLACK] = 0ULL;

    for (int piece = 0; piece < 6; ++piece) {
        pos->occupied[SIDE_WHITE] |= pos->pieces[SIDE_WHITE][piece];
        pos->occupied[SIDE_BLACK] |= pos->pieces[SIDE_BLACK][piece];
    }

    pos->all_occupied = pos->occupied[SIDE_WHITE] | pos->occupied[SIDE_BLACK];
}

/* Loads standard chess starting position. */
void position_set_start(Position* pos) {
    position_set_empty(pos);

    pos->pieces[SIDE_WHITE][PIECE_PAWN] = 0x000000000000FF00ULL;
    pos->pieces[SIDE_WHITE][PIECE_KNIGHT] = 0x0000000000000042ULL;
    pos->pieces[SIDE_WHITE][PIECE_BISHOP] = 0x0000000000000024ULL;
    pos->pieces[SIDE_WHITE][PIECE_ROOK] = 0x0000000000000081ULL;
    pos->pieces[SIDE_WHITE][PIECE_QUEEN] = 0x0000000000000008ULL;
    pos->pieces[SIDE_WHITE][PIECE_KING] = 0x0000000000000010ULL;

    pos->pieces[SIDE_BLACK][PIECE_PAWN] = 0x00FF000000000000ULL;
    pos->pieces[SIDE_BLACK][PIECE_KNIGHT] = 0x4200000000000000ULL;
    pos->pieces[SIDE_BLACK][PIECE_BISHOP] = 0x2400000000000000ULL;
    pos->pieces[SIDE_BLACK][PIECE_ROOK] = 0x8100000000000000ULL;
    pos->pieces[SIDE_BLACK][PIECE_QUEEN] = 0x0800000000000000ULL;
    pos->pieces[SIDE_BLACK][PIECE_KING] = 0x1000000000000000ULL;

    pos->side_to_move = SIDE_WHITE;
    pos->castling_rights = 0x0F;
    pos->en_passant_square = -1;
    pos->halfmove_clock = 0;
    pos->fullmove_number = 1;

    position_refresh_occupancy(pos);
    pos->zobrist_key = position_compute_zobrist(pos);
}

/* Loads arbitrary legal/illegal setup from a FEN string for analysis/testing. */
bool position_set_from_fen(Position* pos, const char* fen) {
    const char* p;
    int rank = 7;
    int file = 0;

    if (pos == NULL || fen == NULL) {
        return false;
    }

    position_set_empty(pos);
    p = fen;

    while (*p != '\0' && *p != ' ') {
        char ch = *p;

        if (ch == '/') {
            if (file != 8 || rank <= 0) {
                return false;
            }
            rank--;
            file = 0;
            p++;
            continue;
        }

        if (ch >= '1' && ch <= '8') {
            file += (int)(ch - '0');
            if (file > 8) {
                return false;
            }
            p++;
            continue;
        }

        {
            Side side;
            PieceType piece;
            int square;

            if (!fen_piece_from_char(ch, &side, &piece)) {
                return false;
            }
            if (file >= 8) {
                return false;
            }

            square = (rank << 3) | file;
            pos->pieces[side][piece] |= bb_square(square);
            file++;
            p++;
        }
    }

    if (rank != 0 || file != 8) {
        return false;
    }
    if (*p != ' ') {
        return false;
    }

    while (*p == ' ') {
        p++;
    }
    if (*p == 'w') {
        pos->side_to_move = SIDE_WHITE;
        p++;
    } else if (*p == 'b') {
        pos->side_to_move = SIDE_BLACK;
        p++;
    } else {
        return false;
    }
    if (*p != ' ') {
        return false;
    }

    while (*p == ' ') {
        p++;
    }
    pos->castling_rights = 0U;
    if (*p == '-') {
        p++;
    } else {
        while (*p != '\0' && *p != ' ') {
            if (*p == 'K') {
                pos->castling_rights |= 0x01;
            } else if (*p == 'Q') {
                pos->castling_rights |= 0x02;
            } else if (*p == 'k') {
                pos->castling_rights |= 0x04;
            } else if (*p == 'q') {
                pos->castling_rights |= 0x08;
            } else {
                return false;
            }
            p++;
        }
    }
    if (*p != ' ') {
        return false;
    }

    while (*p == ' ') {
        p++;
    }
    if (*p == '-') {
        pos->en_passant_square = -1;
        p++;
    } else {
        int ep_file;
        int ep_rank;

        if (p[0] < 'a' || p[0] > 'h' || p[1] < '1' || p[1] > '8') {
            return false;
        }

        ep_file = p[0] - 'a';
        ep_rank = p[1] - '1';
        pos->en_passant_square = (int8_t)((ep_rank << 3) | ep_file);
        p += 2;
    }

    pos->halfmove_clock = 0;
    pos->fullmove_number = 1;

    if (*p != '\0') {
        unsigned halfmove = 0U;
        unsigned fullmove = 1U;

        while (*p == ' ') {
            p++;
        }

        if (*p != '\0') {
            if (!fen_parse_uint_field(&p, &halfmove)) {
                return false;
            }
            while (*p == ' ') {
                p++;
            }
            if (*p != '\0') {
                if (!fen_parse_uint_field(&p, &fullmove)) {
                    return false;
                }
                while (*p == ' ') {
                    p++;
                }
                if (*p != '\0') {
                    return false;
                }
            }
        }

        pos->halfmove_clock = (halfmove > 65535U) ? 65535U : (uint16_t)halfmove;
        pos->fullmove_number = (fullmove == 0U) ? 1U : ((fullmove > 65535U) ? 65535U : (uint16_t)fullmove);
    }

    position_refresh_occupancy(pos);
    pos->zobrist_key = position_compute_zobrist(pos);
    return true;
}

/* Computes full zobrist hash for a position snapshot. */
uint64_t position_compute_zobrist(const Position* pos) {
    uint64_t key = 0ULL;

    for (int side = 0; side < 2; ++side) {
        for (int piece = 0; piece < 6; ++piece) {
            Bitboard bb = pos->pieces[side][piece];
            while (bb != 0ULL) {
                int sq = pop_lsb(&bb);
                key ^= g_zobrist_piece[side][piece][sq];
            }
        }
    }

    key ^= g_zobrist_castling[pos->castling_rights & 0x0F];

    if (pos->en_passant_square >= 0 && pos->en_passant_square < BOARD_SQUARES) {
        key ^= g_zobrist_ep_file[pos->en_passant_square & 7];
    }

    if (pos->side_to_move == SIDE_BLACK) {
        key ^= g_zobrist_side;
    }

    return key;
}

/* Lookup helpers for precomputed attack tables. */
Bitboard engine_get_knight_attacks(int square) {
    return is_square_on_board(square) ? g_knight_attacks[square] : 0ULL;
}

Bitboard engine_get_king_attacks(int square) {
    return is_square_on_board(square) ? g_king_attacks[square] : 0ULL;
}

Bitboard engine_get_pawn_attacks(Side side, int square) {
    if (!is_square_on_board(square) || (side != SIDE_WHITE && side != SIDE_BLACK)) {
        return 0ULL;
    }

    return g_pawn_attacks[side][square];
}

/* Runtime bishop rays with occupancy blocking. */
Bitboard engine_get_bishop_attacks(int square, Bitboard occupancy) {
    static const int directions[4] = {9, 7, -7, -9};
    Bitboard attacks = 0ULL;

    for (int i = 0; i < 4; ++i) {
        int dir = directions[i];
        int sq = square;

        while (true) {
            int next = sq + dir;
            if (!is_square_on_board(next)) {
                break;
            }

            {
                int sq_file = sq & 7;
                int next_file = next & 7;

                if ((dir == 9 || dir == -7) && next_file != sq_file + 1) {
                    break;
                }
                if ((dir == 7 || dir == -9) && next_file != sq_file - 1) {
                    break;
                }
            }

            attacks |= bb_square(next);
            if ((occupancy & bb_square(next)) != 0ULL) {
                break;
            }

            sq = next;
        }
    }

    return attacks;
}

/* Runtime rook rays with occupancy blocking. */
Bitboard engine_get_rook_attacks(int square, Bitboard occupancy) {
    static const int directions[4] = {8, -8, 1, -1};
    Bitboard attacks = 0ULL;

    for (int i = 0; i < 4; ++i) {
        int dir = directions[i];
        int sq = square;

        while (true) {
            int next = sq + dir;
            if (!is_square_on_board(next)) {
                break;
            }

            if ((dir == 1 || dir == -1) && ((sq >> 3) != (next >> 3))) {
                break;
            }

            attacks |= bb_square(next);
            if ((occupancy & bb_square(next)) != 0ULL) {
                break;
            }

            sq = next;
        }
    }

    return attacks;
}

/* Returns king square index for a side, or -1 if not present. */
int engine_find_king_square(const Position* pos, Side side) {
    Bitboard king = pos->pieces[side][PIECE_KING];
    if (king == 0ULL) {
        return -1;
    }

    {
        int sq = 0;
        while ((king & 1ULL) == 0ULL) {
            king >>= 1U;
            sq++;
        }
        return sq;
    }
}

/* True when a square is attacked by at least one piece of the given side. */
bool engine_is_square_attacked(const Position* pos, int square, Side by_side) {
    Bitboard target;

    if (!is_square_on_board(square)) {
        return false;
    }
    target = bb_square(square);

    {
        Bitboard pawns = pos->pieces[by_side][PIECE_PAWN];
        Bitboard pawn_attacks;

        if (by_side == SIDE_WHITE) {
            pawn_attacks = ((pawns << 7) & NOT_FILE_H) | ((pawns << 9) & NOT_FILE_A);
        } else {
            pawn_attacks = ((pawns >> 7) & NOT_FILE_A) | ((pawns >> 9) & NOT_FILE_H);
        }

        if ((pawn_attacks & target) != 0ULL) {
            return true;
        }
    }

    {
        Bitboard knights = pos->pieces[by_side][PIECE_KNIGHT];
        while (knights != 0ULL) {
            int sq = pop_lsb(&knights);
            if ((g_knight_attacks[sq] & target) != 0ULL) {
                return true;
            }
        }
    }

    {
        Bitboard bishops = pos->pieces[by_side][PIECE_BISHOP] | pos->pieces[by_side][PIECE_QUEEN];
        while (bishops != 0ULL) {
            int sq = pop_lsb(&bishops);
            if ((engine_get_bishop_attacks(sq, pos->all_occupied) & target) != 0ULL) {
                return true;
            }
        }
    }

    {
        Bitboard rooks = pos->pieces[by_side][PIECE_ROOK] | pos->pieces[by_side][PIECE_QUEEN];
        while (rooks != 0ULL) {
            int sq = pop_lsb(&rooks);
            if ((engine_get_rook_attacks(sq, pos->all_occupied) & target) != 0ULL) {
                return true;
            }
        }
    }

    {
        Bitboard king = pos->pieces[by_side][PIECE_KING];
        if (king != 0ULL) {
            int king_sq = 0;
            while ((king & 1ULL) == 0ULL) {
                king >>= 1U;
                king_sq++;
            }

            if ((g_king_attacks[king_sq] & target) != 0ULL) {
                return true;
            }
        }
    }

    return false;
}

/* Piece lookup helper for GUI/debug/network validation paths. */
bool position_piece_at(const Position* pos, int square, Side* out_side, PieceType* out_piece) {
    if (!is_square_on_board(square)) {
        return false;
    }

    {
        Bitboard mask = bb_square(square);
        for (int side = 0; side < 2; ++side) {
            for (int piece = 0; piece < 6; ++piece) {
                if ((pos->pieces[side][piece] & mask) != 0ULL) {
                    if (out_side != NULL) {
                        *out_side = (Side)side;
                    }
                    if (out_piece != NULL) {
                        *out_piece = (PieceType)piece;
                    }
                    return true;
                }
            }
        }
    }

    return false;
}

/* Converts piece identity to a simple character representation. */
char piece_to_char(Side side, PieceType piece) {
    static const char white_map[6] = {'P', 'N', 'B', 'R', 'Q', 'K'};
    static const char black_map[6] = {'p', 'n', 'b', 'r', 'q', 'k'};

    if (piece < PIECE_PAWN || piece > PIECE_KING) {
        return '.';
    }

    return (side == SIDE_WHITE) ? white_map[piece] : black_map[piece];
}

/* Encodes move coordinates in UCI coordinate notation. */
void move_to_uci(Move move, char out[6]) {
    out[0] = (char)('a' + (move.from & 7));
    out[1] = (char)('1' + (move.from >> 3));
    out[2] = (char)('a' + (move.to & 7));
    out[3] = (char)('1' + (move.to >> 3));

    if ((move.flags & MOVE_FLAG_PROMOTION) != 0U) {
        char promo = 'q';
        if (move.promotion == PIECE_ROOK) {
            promo = 'r';
        } else if (move.promotion == PIECE_BISHOP) {
            promo = 'b';
        } else if (move.promotion == PIECE_KNIGHT) {
            promo = 'n';
        }

        out[4] = promo;
        out[5] = '\0';
    } else {
        out[4] = '\0';
    }
}

/* Parses UCI coordinate notation into internal move format. */
bool move_from_uci(const char* text, Move* out_move) {
    size_t len;
    int from_file;
    int from_rank;
    int to_file;
    int to_rank;
    Move move;

    if (text == NULL || out_move == NULL) {
        return false;
    }

    len = strlen(text);
    if (len < 4 || len > 5) {
        return false;
    }

    from_file = text[0] - 'a';
    from_rank = text[1] - '1';
    to_file = text[2] - 'a';
    to_rank = text[3] - '1';

    if (from_file < 0 || from_file > 7 || from_rank < 0 || from_rank > 7 ||
        to_file < 0 || to_file > 7 || to_rank < 0 || to_rank > 7) {
        return false;
    }

    move.from = (uint8_t)((from_rank << 3) | from_file);
    move.to = (uint8_t)((to_rank << 3) | to_file);
    move.flags = MOVE_FLAG_NONE;
    move.promotion = PIECE_NONE;
    move.score = 0;

    if (len == 5) {
        char promo = text[4];
        move.flags |= MOVE_FLAG_PROMOTION;

        if (promo == 'q' || promo == 'Q') {
            move.promotion = PIECE_QUEEN;
        } else if (promo == 'r' || promo == 'R') {
            move.promotion = PIECE_ROOK;
        } else if (promo == 'b' || promo == 'B') {
            move.promotion = PIECE_BISHOP;
        } else if (promo == 'n' || promo == 'N') {
            move.promotion = PIECE_KNIGHT;
        } else {
            return false;
        }
    }

    *out_move = move;
    return true;
}
