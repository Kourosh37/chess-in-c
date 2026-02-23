#include "engine.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

typedef struct PerftCase {
    const char* name;
    const char* fen;
    int depth;
    uint64_t expected_nodes;
} PerftCase;

typedef struct TacticalCase {
    const char* name;
    const char* fen;
    int depth;
    int max_time_ms;
    const char* expected_moves;
} TacticalCase;

static const PerftCase g_perft_cases_full[] = {
    {
        "Start Position D5",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        5,
        4865609ULL
    },
    {
        "Kiwipete D4",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        4,
        4085603ULL
    },
    {
        "Endgame EP D5",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        5,
        674624ULL
    },
    {
        "Complex Castling D4",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/B1P1P3/5N2/Pp1P1PPP/R2Q1RK1 w kq - 0 1",
        4,
        1371859ULL
    }
};

static const PerftCase g_perft_cases_quick[] = {
    {
        "Start Position D4",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        4,
        197281ULL
    },
    {
        "Kiwipete D3",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        3,
        97862ULL
    },
    {
        "Endgame EP D4",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        4,
        43238ULL
    }
};

static const TacticalCase g_tactical_cases[] = {
    {
        "Mate In 1 (Qxg7#)",
        "7k/6p1/6KQ/8/8/8/8/8 w - - 0 1",
        4,
        800,
        "h6g7"
    },
    {
        "Win Queen Immediately",
        "4k3/8/8/8/3q4/8/8/3QK3 w - - 0 1",
        5,
        1200,
        "d1d4"
    },
    {
        "Opening Book Castling",
        "r1bqkb1r/1ppp1ppp/p1n2n2/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 2 5",
        8,
        1200,
        "e1g1"
    },
    {
        "Take Free Queen",
        "r1b1kbnr/pppp1ppp/2n5/4p3/3q4/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 5",
        5,
        1500,
        "f3d4"
    }
};

/* Portable monotonic-ish millisecond clock for benchmark reporting. */
static uint64_t now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000ULL);
#endif
}

/* Returns nodes count for one legal perft subtree. */
static uint64_t perft_recursive(const Position* pos, int depth) {
    MoveList legal;
    uint64_t nodes = 0ULL;

    if (depth <= 0) {
        return 1ULL;
    }

    generate_legal_moves(pos, &legal);
    if (depth == 1) {
        return (uint64_t)legal.count;
    }

    for (int i = 0; i < legal.count; ++i) {
        Position next = *pos;
        if (!engine_apply_move(&next, legal.moves[i])) {
            continue;
        }
        nodes += perft_recursive(&next, depth - 1);
    }

    return nodes;
}

/* True when move appears in a space-separated expected list. */
static bool move_in_expected_list(const char* expected_moves, const char* best_move) {
    const char* p;

    if (expected_moves == NULL || best_move == NULL || best_move[0] == '\0') {
        return false;
    }

    p = expected_moves;
    while (*p != '\0') {
        char token[8];
        int len = 0;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        while (*p != '\0' && *p != ' ' && *p != '\t' && len < (int)sizeof(token) - 1) {
            token[len++] = *p;
            p++;
        }
        token[len] = '\0';

        if (strcmp(token, best_move) == 0) {
            return true;
        }

        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
    }

    return false;
}

/* Runs one perft suite and returns number of failures. */
static int run_perft_suite(bool quick_mode) {
    const PerftCase* cases = quick_mode ? g_perft_cases_quick : g_perft_cases_full;
    int case_count = quick_mode
        ? (int)(sizeof(g_perft_cases_quick) / sizeof(g_perft_cases_quick[0]))
        : (int)(sizeof(g_perft_cases_full) / sizeof(g_perft_cases_full[0]));
    int failures = 0;

    printf("== Perft Suite (%s) ==\n", quick_mode ? "quick" : "full");

    for (int i = 0; i < case_count; ++i) {
        Position pos;
        uint64_t start_ms;
        uint64_t elapsed_ms;
        uint64_t nodes;

        if (!position_set_from_fen(&pos, cases[i].fen)) {
            printf("[FAIL] %s | invalid FEN\n", cases[i].name);
            failures++;
            continue;
        }

        start_ms = now_ms();
        nodes = perft_recursive(&pos, cases[i].depth);
        elapsed_ms = now_ms() - start_ms;

        if (nodes != cases[i].expected_nodes) {
            printf("[FAIL] %s | depth=%d | expected=%llu got=%llu | %llums\n",
                   cases[i].name,
                   cases[i].depth,
                   (unsigned long long)cases[i].expected_nodes,
                   (unsigned long long)nodes,
                   (unsigned long long)elapsed_ms);
            failures++;
        } else {
            printf("[ OK ] %s | depth=%d | nodes=%llu | %llums\n",
                   cases[i].name,
                   cases[i].depth,
                   (unsigned long long)nodes,
                   (unsigned long long)elapsed_ms);
        }
    }

    printf("\n");
    return failures;
}

/* Runs one tactical suite and returns number of failures. */
static int run_tactical_suite(void) {
    int case_count = (int)(sizeof(g_tactical_cases) / sizeof(g_tactical_cases[0]));
    int failures = 0;

    printf("== Tactical Suite ==\n");

    for (int i = 0; i < case_count; ++i) {
        Position pos;
        SearchLimits limits;
        SearchResult result;
        char best_uci[6] = {0};
        uint64_t start_ms;
        uint64_t elapsed_ms;

        if (!position_set_from_fen(&pos, g_tactical_cases[i].fen)) {
            printf("[FAIL] %s | invalid FEN\n", g_tactical_cases[i].name);
            failures++;
            continue;
        }

        limits.depth = g_tactical_cases[i].depth;
        limits.max_time_ms = g_tactical_cases[i].max_time_ms;
        limits.randomness = 0;

        start_ms = now_ms();
        search_best_move(&pos, &limits, &result);
        elapsed_ms = now_ms() - start_ms;

        move_to_uci(result.best_move, best_uci);

        if (!move_in_expected_list(g_tactical_cases[i].expected_moves, best_uci)) {
            printf("[FAIL] %s | expected={%s} got=%s | depth=%d nodes=%llu score=%d | %llums\n",
                   g_tactical_cases[i].name,
                   g_tactical_cases[i].expected_moves,
                   best_uci,
                   result.depth_reached,
                   (unsigned long long)result.nodes,
                   result.score,
                   (unsigned long long)elapsed_ms);
            failures++;
        } else {
            printf("[ OK ] %s | best=%s | depth=%d nodes=%llu score=%d | %llums\n",
                   g_tactical_cases[i].name,
                   best_uci,
                   result.depth_reached,
                   (unsigned long long)result.nodes,
                   result.score,
                   (unsigned long long)elapsed_ms);
        }
    }

    printf("\n");
    return failures;
}

/* Prints CLI usage for bench tool. */
static void print_usage(const char* exe_name) {
    printf("Usage: %s [--quick] [--perft] [--tactics]\n", exe_name);
    printf("  --quick   Run reduced perft depths (faster)\n");
    printf("  --perft   Run only perft suite\n");
    printf("  --tactics Run only tactical suite\n");
}

int main(int argc, char** argv) {
    bool quick_mode = false;
    bool run_perft = true;
    bool run_tactics = true;
    int failures = 0;

    engine_init();
    engine_reset_transposition_table();

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--quick") == 0) {
            quick_mode = true;
        } else if (strcmp(argv[i], "--perft") == 0) {
            run_tactics = false;
        } else if (strcmp(argv[i], "--tactics") == 0) {
            run_perft = false;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!run_perft && !run_tactics) {
        print_usage(argv[0]);
        return 2;
    }

    if (run_perft) {
        failures += run_perft_suite(quick_mode);
    }
    if (run_tactics) {
        failures += run_tactical_suite();
    }

    if (failures == 0) {
        printf("All engine benchmarks passed.\n");
        return 0;
    }

    printf("Engine benchmark failures: %d\n", failures);
    return 1;
}
