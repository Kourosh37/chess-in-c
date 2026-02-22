#include "engine.h"

#include <string.h>

/* Castling rights bit layout (KQkq). */
#define CASTLE_WHITE_KING  0x01
#define CASTLE_WHITE_QUEEN 0x02
#define CASTLE_BLACK_KING  0x04
#define CASTLE_BLACK_QUEEN 0x08

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

/* Move equality for external move validation (promotion-sensitive). */
static bool move_same_for_validation(Move a, Move b) {
    if (a.from != b.from || a.to != b.to) {
        return false;
    }

    if ((b.flags & MOVE_FLAG_PROMOTION) != 0U) {
        return a.promotion == b.promotion;
    }

    return true;
}

/* Appends move if list capacity allows it. */
static void add_move(MoveList* list, uint8_t from, uint8_t to, uint8_t flags, uint8_t promotion) {
    if (list->count >= MAX_MOVES) {
        return;
    }

    list->moves[list->count].from = from;
    list->moves[list->count].to = to;
    list->moves[list->count].flags = flags;
    list->moves[list->count].promotion = promotion;
    list->moves[list->count].score = 0;
    list->count++;
}

/* Generates four promotion variants for one pawn destination. */
static void add_promotion_moves(MoveList* list, uint8_t from, uint8_t to, uint8_t base_flags) {
    add_move(list, from, to, (uint8_t)(base_flags | MOVE_FLAG_PROMOTION), PIECE_QUEEN);
    add_move(list, from, to, (uint8_t)(base_flags | MOVE_FLAG_PROMOTION), PIECE_ROOK);
    add_move(list, from, to, (uint8_t)(base_flags | MOVE_FLAG_PROMOTION), PIECE_BISHOP);
    add_move(list, from, to, (uint8_t)(base_flags | MOVE_FLAG_PROMOTION), PIECE_KNIGHT);
}

/* Generates all pseudo-legal pawn moves for one side. */
static void generate_pawn_moves(const Position* pos, Side us, MoveList* list) {
    Side them = (us == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    Bitboard pawns = pos->pieces[us][PIECE_PAWN];

    while (pawns != 0ULL) {
        int from = pop_lsb(&pawns);
        int file = from & 7;
        int rank = from >> 3;

        {
            int forward = (us == SIDE_WHITE) ? (from + 8) : (from - 8);
            if (forward >= 0 && forward < BOARD_SQUARES && ((pos->all_occupied & bb_square(forward)) == 0ULL)) {
                int promote_rank = (us == SIDE_WHITE) ? 7 : 0;

                if ((forward >> 3) == promote_rank) {
                    add_promotion_moves(list, (uint8_t)from, (uint8_t)forward, MOVE_FLAG_NONE);
                } else {
                    add_move(list, (uint8_t)from, (uint8_t)forward, MOVE_FLAG_NONE, PIECE_NONE);

                    {
                        int start_rank = (us == SIDE_WHITE) ? 1 : 6;
                        int double_forward = (us == SIDE_WHITE) ? (from + 16) : (from - 16);
                        if (rank == start_rank && ((pos->all_occupied & bb_square(double_forward)) == 0ULL)) {
                            add_move(list, (uint8_t)from, (uint8_t)double_forward, MOVE_FLAG_DOUBLE_PAWN, PIECE_NONE);
                        }
                    }
                }
            }
        }

        {
            int cap_left = (us == SIDE_WHITE) ? (from + 7) : (from - 9);
            if (file > 0 && cap_left >= 0 && cap_left < BOARD_SQUARES) {
                bool is_capture = (pos->occupied[them] & bb_square(cap_left)) != 0ULL;
                bool is_ep = cap_left == pos->en_passant_square;
                if (is_capture || is_ep) {
                    uint8_t flags = MOVE_FLAG_CAPTURE;
                    if (is_ep) {
                        flags = (uint8_t)(flags | MOVE_FLAG_EN_PASSANT);
                    }

                    if ((cap_left >> 3) == ((us == SIDE_WHITE) ? 7 : 0)) {
                        add_promotion_moves(list, (uint8_t)from, (uint8_t)cap_left, flags);
                    } else {
                        add_move(list, (uint8_t)from, (uint8_t)cap_left, flags, PIECE_NONE);
                    }
                }
            }
        }

        {
            int cap_right = (us == SIDE_WHITE) ? (from + 9) : (from - 7);
            if (file < 7 && cap_right >= 0 && cap_right < BOARD_SQUARES) {
                bool is_capture = (pos->occupied[them] & bb_square(cap_right)) != 0ULL;
                bool is_ep = cap_right == pos->en_passant_square;
                if (is_capture || is_ep) {
                    uint8_t flags = MOVE_FLAG_CAPTURE;
                    if (is_ep) {
                        flags = (uint8_t)(flags | MOVE_FLAG_EN_PASSANT);
                    }

                    if ((cap_right >> 3) == ((us == SIDE_WHITE) ? 7 : 0)) {
                        add_promotion_moves(list, (uint8_t)from, (uint8_t)cap_right, flags);
                    } else {
                        add_move(list, (uint8_t)from, (uint8_t)cap_right, flags, PIECE_NONE);
                    }
                }
            }
        }
    }
}

/* Generates all pseudo-legal knight moves for one side. */
static void generate_knight_moves(const Position* pos, Side us, MoveList* list) {
    Side them = (us == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    Bitboard knights = pos->pieces[us][PIECE_KNIGHT];

    while (knights != 0ULL) {
        int from = pop_lsb(&knights);
        Bitboard attacks = engine_get_knight_attacks(from) & ~pos->occupied[us];

        while (attacks != 0ULL) {
            int to = pop_lsb(&attacks);
            uint8_t flags = ((pos->occupied[them] & bb_square(to)) != 0ULL) ? MOVE_FLAG_CAPTURE : MOVE_FLAG_NONE;
            add_move(list, (uint8_t)from, (uint8_t)to, flags, PIECE_NONE);
        }
    }
}

/* Generates pseudo-legal sliding moves for bishops/rooks/queens. */
static void generate_slider_moves(const Position* pos, Side us, PieceType piece, MoveList* list) {
    Side them = (us == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    Bitboard sliders = pos->pieces[us][piece];

    while (sliders != 0ULL) {
        int from = pop_lsb(&sliders);
        Bitboard attacks = 0ULL;

        if (piece == PIECE_BISHOP) {
            attacks = engine_get_bishop_attacks(from, pos->all_occupied);
        } else if (piece == PIECE_ROOK) {
            attacks = engine_get_rook_attacks(from, pos->all_occupied);
        } else if (piece == PIECE_QUEEN) {
            attacks = engine_get_bishop_attacks(from, pos->all_occupied) |
                      engine_get_rook_attacks(from, pos->all_occupied);
        }

        attacks &= ~pos->occupied[us];

        while (attacks != 0ULL) {
            int to = pop_lsb(&attacks);
            uint8_t flags = ((pos->occupied[them] & bb_square(to)) != 0ULL) ? MOVE_FLAG_CAPTURE : MOVE_FLAG_NONE;
            add_move(list, (uint8_t)from, (uint8_t)to, flags, PIECE_NONE);
        }
    }
}

/* Generates pseudo-legal king moves, including castling checks. */
static void generate_king_moves(const Position* pos, Side us, MoveList* list) {
    Side them = (us == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    Bitboard king = pos->pieces[us][PIECE_KING];
    int from = 0;

    if (king == 0ULL) {
        return;
    }

    while ((king & 1ULL) == 0ULL) {
        king >>= 1U;
        from++;
    }

    {
        Bitboard attacks = engine_get_king_attacks(from) & ~pos->occupied[us];
        while (attacks != 0ULL) {
            int to = pop_lsb(&attacks);
            uint8_t flags = ((pos->occupied[them] & bb_square(to)) != 0ULL) ? MOVE_FLAG_CAPTURE : MOVE_FLAG_NONE;
            add_move(list, (uint8_t)from, (uint8_t)to, flags, PIECE_NONE);
        }
    }

    if (us == SIDE_WHITE) {
        if ((pos->castling_rights & CASTLE_WHITE_KING) != 0U) {
            bool empty = ((pos->all_occupied & (bb_square(5) | bb_square(6))) == 0ULL);
            bool safe = !engine_is_square_attacked(pos, 4, them) &&
                        !engine_is_square_attacked(pos, 5, them) &&
                        !engine_is_square_attacked(pos, 6, them);
            if (empty && safe) {
                add_move(list, 4, 6, MOVE_FLAG_KING_CASTLE, PIECE_NONE);
            }
        }

        if ((pos->castling_rights & CASTLE_WHITE_QUEEN) != 0U) {
            bool empty = ((pos->all_occupied & (bb_square(1) | bb_square(2) | bb_square(3))) == 0ULL);
            bool safe = !engine_is_square_attacked(pos, 4, them) &&
                        !engine_is_square_attacked(pos, 3, them) &&
                        !engine_is_square_attacked(pos, 2, them);
            if (empty && safe) {
                add_move(list, 4, 2, MOVE_FLAG_QUEEN_CASTLE, PIECE_NONE);
            }
        }
    } else {
        if ((pos->castling_rights & CASTLE_BLACK_KING) != 0U) {
            bool empty = ((pos->all_occupied & (bb_square(61) | bb_square(62))) == 0ULL);
            bool safe = !engine_is_square_attacked(pos, 60, them) &&
                        !engine_is_square_attacked(pos, 61, them) &&
                        !engine_is_square_attacked(pos, 62, them);
            if (empty && safe) {
                add_move(list, 60, 62, MOVE_FLAG_KING_CASTLE, PIECE_NONE);
            }
        }

        if ((pos->castling_rights & CASTLE_BLACK_QUEEN) != 0U) {
            bool empty = ((pos->all_occupied & (bb_square(57) | bb_square(58) | bb_square(59))) == 0ULL);
            bool safe = !engine_is_square_attacked(pos, 60, them) &&
                        !engine_is_square_attacked(pos, 59, them) &&
                        !engine_is_square_attacked(pos, 58, them);
            if (empty && safe) {
                add_move(list, 60, 58, MOVE_FLAG_QUEEN_CASTLE, PIECE_NONE);
            }
        }
    }
}

/* Generates pseudo-legal move list before king-safety filtering. */
static void generate_pseudo_legal_moves(const Position* pos, MoveList* list) {
    Side us = pos->side_to_move;

    list->count = 0;
    generate_pawn_moves(pos, us, list);
    generate_knight_moves(pos, us, list);
    generate_slider_moves(pos, us, PIECE_BISHOP, list);
    generate_slider_moves(pos, us, PIECE_ROOK, list);
    generate_slider_moves(pos, us, PIECE_QUEEN, list);
    generate_king_moves(pos, us, list);
}

/* Clears any piece from a side at one square. */
static void clear_piece_at(Position* pos, Side side, int square) {
    Bitboard mask = ~bb_square(square);

    for (int piece = 0; piece < 6; ++piece) {
        pos->pieces[side][piece] &= mask;
    }
}

/* Updates castling rights after king/rook moves or rook captures. */
static void update_castling_rights(Position* pos, Side us, PieceType moved_piece, int from, int to) {
    if (moved_piece == PIECE_KING) {
        if (us == SIDE_WHITE) {
            pos->castling_rights &= (uint8_t)~(CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN);
        } else {
            pos->castling_rights &= (uint8_t)~(CASTLE_BLACK_KING | CASTLE_BLACK_QUEEN);
        }
    }

    if (moved_piece == PIECE_ROOK) {
        if (from == 0) {
            pos->castling_rights &= (uint8_t)~CASTLE_WHITE_QUEEN;
        } else if (from == 7) {
            pos->castling_rights &= (uint8_t)~CASTLE_WHITE_KING;
        } else if (from == 56) {
            pos->castling_rights &= (uint8_t)~CASTLE_BLACK_QUEEN;
        } else if (from == 63) {
            pos->castling_rights &= (uint8_t)~CASTLE_BLACK_KING;
        }
    }

    /* Captured rook squares also invalidate corresponding castling rights. */
    if (to == 0) {
        pos->castling_rights &= (uint8_t)~CASTLE_WHITE_QUEEN;
    } else if (to == 7) {
        pos->castling_rights &= (uint8_t)~CASTLE_WHITE_KING;
    } else if (to == 56) {
        pos->castling_rights &= (uint8_t)~CASTLE_BLACK_QUEEN;
    } else if (to == 63) {
        pos->castling_rights &= (uint8_t)~CASTLE_BLACK_KING;
    }
}

/* Applies move without legality re-check (used by search and legal filtering). */
static bool apply_move_internal(Position* pos, Move move) {
    Side us = pos->side_to_move;
    Side them = (us == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE;
    Side found_side;
    PieceType moved_piece;
    bool is_capture = false;

    if (move.from >= BOARD_SQUARES || move.to >= BOARD_SQUARES) {
        return false;
    }

    if (!position_piece_at(pos, move.from, &found_side, &moved_piece) || found_side != us) {
        return false;
    }

    if ((move.flags & MOVE_FLAG_EN_PASSANT) != 0U) {
        int cap_square = (us == SIDE_WHITE) ? (move.to - 8) : (move.to + 8);
        if (cap_square < 0 || cap_square >= BOARD_SQUARES) {
            return false;
        }

        clear_piece_at(pos, them, cap_square);
        is_capture = true;
    } else {
        Side cap_side;
        PieceType cap_piece;

        if (position_piece_at(pos, move.to, &cap_side, &cap_piece) && cap_side == them) {
            clear_piece_at(pos, them, move.to);
            is_capture = true;
        }
    }

    pos->pieces[us][moved_piece] &= ~bb_square(move.from);

    {
        PieceType placed_piece = moved_piece;

        if ((move.flags & MOVE_FLAG_PROMOTION) != 0U && moved_piece == PIECE_PAWN) {
            if (move.promotion >= PIECE_KNIGHT && move.promotion <= PIECE_QUEEN) {
                placed_piece = (PieceType)move.promotion;
            } else {
                placed_piece = PIECE_QUEEN;
            }
        }

        pos->pieces[us][placed_piece] |= bb_square(move.to);
    }

    if ((move.flags & MOVE_FLAG_KING_CASTLE) != 0U) {
        if (us == SIDE_WHITE) {
            pos->pieces[SIDE_WHITE][PIECE_ROOK] &= ~bb_square(7);
            pos->pieces[SIDE_WHITE][PIECE_ROOK] |= bb_square(5);
        } else {
            pos->pieces[SIDE_BLACK][PIECE_ROOK] &= ~bb_square(63);
            pos->pieces[SIDE_BLACK][PIECE_ROOK] |= bb_square(61);
        }
    } else if ((move.flags & MOVE_FLAG_QUEEN_CASTLE) != 0U) {
        if (us == SIDE_WHITE) {
            pos->pieces[SIDE_WHITE][PIECE_ROOK] &= ~bb_square(0);
            pos->pieces[SIDE_WHITE][PIECE_ROOK] |= bb_square(3);
        } else {
            pos->pieces[SIDE_BLACK][PIECE_ROOK] &= ~bb_square(56);
            pos->pieces[SIDE_BLACK][PIECE_ROOK] |= bb_square(59);
        }
    }

    update_castling_rights(pos, us, moved_piece, move.from, move.to);

    if ((move.flags & MOVE_FLAG_DOUBLE_PAWN) != 0U && moved_piece == PIECE_PAWN) {
        pos->en_passant_square = (int8_t)((us == SIDE_WHITE) ? (move.to - 8) : (move.to + 8));
    } else {
        pos->en_passant_square = -1;
    }

    if (moved_piece == PIECE_PAWN || is_capture) {
        pos->halfmove_clock = 0;
    } else {
        pos->halfmove_clock++;
    }

    if (us == SIDE_BLACK) {
        pos->fullmove_number++;
    }

    pos->side_to_move = them;
    position_refresh_occupancy(pos);
    pos->zobrist_key = position_compute_zobrist(pos);

    return true;
}

/* Generates legal moves by filtering pseudo-legal moves that expose own king. */
void generate_legal_moves(const Position* pos, MoveList* list) {
    MoveList pseudo;
    Side moving_side = pos->side_to_move;

    generate_pseudo_legal_moves(pos, &pseudo);
    list->count = 0;

    for (int i = 0; i < pseudo.count; ++i) {
        Position next = *pos;
        if (!apply_move_internal(&next, pseudo.moves[i])) {
            continue;
        }

        if (!engine_in_check(&next, moving_side) && list->count < MAX_MOVES) {
            list->moves[list->count++] = pseudo.moves[i];
        }
    }
}

/* Applies a move without generating legal list. */
bool engine_apply_move(Position* pos, Move move) {
    return apply_move_internal(pos, move);
}

/* Validates move against legal list and applies the canonical legal version. */
bool engine_make_move(Position* pos, Move move) {
    MoveList legal;

    generate_legal_moves(pos, &legal);
    for (int i = 0; i < legal.count; ++i) {
        if (move_same_for_validation(move, legal.moves[i])) {
            return apply_move_internal(pos, legal.moves[i]);
        }
    }

    return false;
}

/* True when side king square is currently attacked by opponent. */
bool engine_in_check(const Position* pos, Side side) {
    int king_square = engine_find_king_square(pos, side);
    if (king_square < 0) {
        return false;
    }

    return engine_is_square_attacked(pos, king_square, (side == SIDE_WHITE) ? SIDE_BLACK : SIDE_WHITE);
}
