/* nmcs_extend.c - Extend an existing snake using Nested Monte Carlo Search.
 *
 * Independent sibling of ../pruned_bfs_search/ and ../stochastic_prune/. Per
 * project policy (see ../../notes/search-strategy-ideas.md and ../../CLAUDE.md),
 * neither existing tool is edited in place; this is a structurally different
 * algorithm, built from its own copies of the shared utility modules, sharing no
 * code or mutable state with either.
 *
 * ALGORITHM (see notes/search-strategy-ideas.md idea #5; D. Kinny, "A New
 * Approach to the Snake-In-The-Box Problem," ECAI 2012; T. Cazenave, "Nested
 * Search versus Limited Discrepancy Search," arXiv:2210.00216, for general NMCS
 * mechanics):
 *
 *   playout(state):
 *     repeatedly pick a legal extension and apply it until no legal extension
 *     remains. Move choice uses Kinny's heuristic: among legal moves, if any
 *     leads to a state with EXACTLY ONE further legal continuation (a "forced"
 *     state), pick uniformly among those; otherwise pick uniformly among all
 *     legal moves. This is a randomized policy (unlike the deterministic beam
 *     search), so repeated playouts from the same state differ.
 *
 *   nmcs(state, level):
 *     if level == 0: return playout(state)
 *     else:
 *       current = state; memorized_best = playout(state)   # safe fallback
 *       while current has a legal move:
 *         for each legal move m from current:
 *           child = apply(current, m)
 *           result = nmcs(child, level - 1)
 *           keep the move+result with the longest result
 *         if the best result beats memorized_best, memorized_best = it
 *         current = apply(current, best move)   # commit ONE step only
 *       return whichever of {current, memorized_best} is longer
 *
 * Cost grows roughly as (branching factor)^level x depth per playout, where
 * branching factor is bounded by the dimension (small, <= ~20 for the
 * dimensions this project targets) -- NOT by the path length. So level 0/1 are
 * cheap; level 2+ gets expensive fast. Unlike the beam-search tools, NMCS's
 * memory footprint is O(depth) per recursive call stack, not O(beam width) --
 * it trades RAM for repeated-rollout compute, which is the point of trying it
 * (see notes doc: raw memory scaling hit real limits and caused swap thrashing
 * this session).
 *
 * Usage:
 *   ./nmcs_extend <target_dimension> [--level L] [--seed N] [--trials T]
 *                 [seed_file]
 *   --level L   : NMCS nesting level (default 1). 0 = plain random playout
 *                 (with the forced-move heuristic), no search at all.
 *   --seed N    : RNG seed (default: time-based).
 *   --trials T  : run T independent searches, keep the best (default 1).
 *   seed_file   : text file of transition integers (default extend_input.txt).
 */
#define _POSIX_C_SOURCE 200809L

#include "snake_node.h"
#include "canonical.h"
#include "transitions.h"
#include "validation.h"
#include "snake_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_DIM 64

/* ---- RNG helper: uniform integer in [0, n) ----------------------------- */
static long rand_below(long n)
{
    if (n <= 1) return 0;
    return (long)((double)rand() / ((double)RAND_MAX + 1.0) * (double)n);
}

/* ---- Legal-move enumeration for a node --------------------------------- *
 * Returns the count of dims (from get_legal_next_dimensions) for which
 * snake_node_can_extend is actually true, written into out (caller-owned,
 * capacity >= dimension + 1). */
static long legal_extendable_moves(const SnakeNode *node, int *out)
{
    int legal_dims[MAX_DIM + 1];
    long n_legal = get_legal_next_dimensions(node->transition_sequence,
                                             node->length, legal_dims);
    long count = 0;
    for (long k = 0; k < n_legal; k++) {
        if (snake_node_can_extend(node, legal_dims[k])) {
            out[count++] = legal_dims[k];
        }
    }
    return count;
}

/* ---- Playout: randomized walk to a dead end, Kinny's forced-move bias -- */
static bool do_playout(const SnakeNode *start, SnakeNode *out)
{
    SnakeNode current;
    if (!snake_node_init(&current, start->transition_sequence, start->length,
                         start->dimension)) {
        return false;
    }

    int candidates[MAX_DIM + 1];
    int forced_candidates[MAX_DIM + 1];

    for (;;) {
        long n_candidates = legal_extendable_moves(&current, candidates);
        if (n_candidates == 0) {
            break;  /* dead end: playout complete */
        }

        /* Kinny's heuristic: prefer moves leading to a "forced" (exactly one
         * further legal move) state. Materialize each candidate child to check
         * its own legal-move count (dimension-bounded, cheap: at most
         * dimension+1 candidates, each an O(1) bitmap check per lookahead). */
        long n_forced = 0;
        for (long i = 0; i < n_candidates; i++) {
            SnakeNode child;
            if (!snake_node_create_child(&current, candidates[i], &child)) {
                continue;
            }
            int lookahead[MAX_DIM + 1];
            long n_further = legal_extendable_moves(&child, lookahead);
            snake_node_free(&child);
            if (n_further == 1) {
                forced_candidates[n_forced++] = candidates[i];
            }
        }

        int chosen_dim;
        if (n_forced > 0) {
            chosen_dim = forced_candidates[rand_below(n_forced)];
        } else {
            chosen_dim = candidates[rand_below(n_candidates)];
        }

        SnakeNode next;
        if (!snake_node_create_child(&current, chosen_dim, &next)) {
            break;  /* shouldn't happen: chosen_dim was verified extendable */
        }
        snake_node_free(&current);
        current = next;
    }

    *out = current;
    return true;
}

/* ---- NMCS: recursive nested search with memoization -------------------- */
static bool nmcs(const SnakeNode *state, int level, SnakeNode *out)
{
    if (level <= 0) {
        return do_playout(state, out);
    }

    SnakeNode current;
    if (!snake_node_init(&current, state->transition_sequence, state->length,
                         state->dimension)) {
        return false;
    }

    SnakeNode memorized_best;
    if (!do_playout(state, &memorized_best)) {
        snake_node_free(&current);
        return false;
    }

    int candidates[MAX_DIM + 1];
    for (;;) {
        long n_candidates = legal_extendable_moves(&current, candidates);
        if (n_candidates == 0) {
            break;
        }

        bool have_best_child = false;
        SnakeNode best_child;
        SnakeNode best_result;
        size_t best_score = 0;

        for (long i = 0; i < n_candidates; i++) {
            SnakeNode child;
            if (!snake_node_create_child(&current, candidates[i], &child)) {
                continue;
            }
            SnakeNode result;
            if (!nmcs(&child, level - 1, &result)) {
                snake_node_free(&child);
                continue;
            }
            size_t score = snake_node_get_length(&result);
            if (!have_best_child || score > best_score) {
                if (have_best_child) {
                    snake_node_free(&best_child);
                    snake_node_free(&best_result);
                }
                best_child = child;
                best_result = result;
                best_score = score;
                have_best_child = true;
            } else {
                snake_node_free(&child);
                snake_node_free(&result);
            }
        }

        if (!have_best_child) {
            break;  /* no candidate produced a usable child (allocation failure) */
        }

        if (best_score > snake_node_get_length(&memorized_best)) {
            snake_node_free(&memorized_best);
            memorized_best = best_result;
        } else {
            snake_node_free(&best_result);
        }

        snake_node_free(&current);
        current = best_child;
    }

    if (snake_node_get_length(&memorized_best) > snake_node_get_length(&current)) {
        snake_node_free(&current);
        *out = memorized_best;
    } else {
        snake_node_free(&memorized_best);
        *out = current;
    }
    return true;
}

/* ---- Seed loading (same simplified single-text-file loader as
 * stochastic_extend.c -- not the .bin/directory support extend_snake.c has,
 * since this tool is for experimenting with a different algorithm, not bulk
 * seed ingestion). --------------------------------------------------------- */

static void canonicalize_transitions(int *seq, size_t len)
{
    int map[MAX_DIM];
    for (int i = 0; i < MAX_DIM; i++) map[i] = -1;
    int next = 0;
    for (size_t i = 0; i < len; i++) {
        int d = seq[i];
        if (d < 0 || d >= MAX_DIM) continue;
        if (map[d] == -1) map[d] = next++;
        seq[i] = map[d];
    }
}

static bool load_seed(const char *path, int dimension, int **out_trans,
                      size_t *out_len)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "Could not open seed file '%s'\n", path);
        return false;
    }
    long cap = (long)1 << dimension;
    int *buf = (int *)malloc((size_t)cap * sizeof(int));
    if (buf == NULL) { fclose(f); return false; }

    long count = 0;
    int value;
    while (fscanf(f, "%d", &value) == 1) {
        if (count >= cap) {
            fprintf(stderr, "Seed '%s' too long (> %ld transitions)\n", path, cap);
            free(buf);
            fclose(f);
            return false;
        }
        buf[count++] = value;
    }
    fclose(f);

    if (count == 0) {
        fprintf(stderr, "Seed '%s' has no transitions\n", path);
        free(buf);
        return false;
    }

    char msg[256];
    if (!validate_transition_sequence(buf, (size_t)count, dimension, msg,
                                      sizeof(msg))) {
        fprintf(stderr, "Invalid seed '%s': %s\n", path, msg);
        free(buf);
        return false;
    }
    canonicalize_transitions(buf, (size_t)count);

    *out_trans = buf;
    *out_len = (size_t)count;
    return true;
}

/* ---- Driver ------------------------------------------------------------ */

#define GRID_PRINT_MAX_LEN 500

static void print_grid_guarded(const long *vertices, size_t n)
{
    if (n > GRID_PRINT_MAX_LEN) {
        printf("(Hamming grid skipped: %zu vertices exceeds the %d-vertex "
               "print cap to avoid an O(n^2) output blowup.)\n",
               n, GRID_PRINT_MAX_LEN);
        return;
    }
    print_hamming_grid(vertices, n);
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <target_dimension> [--level L] [--seed N] [--trials T]\n"
        "                              [seed_file]\n"
        "  --level L  : NMCS nesting level (default 1). Cost grows roughly as\n"
        "               dimension^level x path_length per trial -- level 2+ is\n"
        "               expensive. Level 0 is a plain randomized playout.\n"
        "  --seed N   : RNG seed (default: time-based).\n"
        "  --trials T : run T independent searches, keep the best (default 1).\n"
        "  seed_file  : text file of transition integers (default\n"
        "               extend_input.txt).\n",
        prog);
}

int main(int argc, char **argv)
{
    snake_io_set_base(argv[0]);
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    int dimension = atoi(argv[1]);
    if (dimension < 1 || dimension >= MAX_DIM) {
        fprintf(stderr, "Target dimension must be in [1, %d), got %d\n",
                MAX_DIM, dimension);
        return 1;
    }

    int level = 1;
    unsigned int rng_seed = (unsigned int)time(NULL);
    bool seed_set = false;
    int trials = 1;
    const char *seed_path = "extend_input.txt";
    bool path_set = false;

    for (int ai = 2; ai < argc; ai++) {
        const char *a = argv[ai];
        if (strcmp(a, "--level") == 0 && ai + 1 < argc) {
            level = atoi(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--seed") == 0 && ai + 1 < argc) {
            rng_seed = (unsigned int)strtoul(argv[++ai], NULL, 10);
            seed_set = true;
            continue;
        }
        if (strcmp(a, "--trials") == 0 && ai + 1 < argc) {
            trials = atoi(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        seed_path = a;
        path_set = true;
        (void)path_set;
    }

    if (level < 0) level = 0;
    if (trials < 1) trials = 1;

    int *seed_trans;
    size_t seed_len;
    if (!load_seed(seed_path, dimension, &seed_trans, &seed_len)) {
        return 1;
    }

    SnakeNode seed_node;
    if (!snake_node_init(&seed_node, seed_trans, seed_len, dimension)) {
        fprintf(stderr, "Failed to initialize seed node\n");
        free(seed_trans);
        return 1;
    }
    free(seed_trans);

    printf("Snake-in-the-box seeded search (Nested Monte Carlo Search)\n");
    printf("Dimension: %d, level: %d, trials: %d\n", dimension, level, trials);
    printf("Seed: %s (length %zu)\n\n", seed_path, seed_len);

    SnakeNode overall_best;
    bool have_overall_best = false;

    for (int t = 0; t < trials; t++) {
        unsigned int this_seed = seed_set ? (rng_seed + (unsigned int)t)
                                          : (unsigned int)(time(NULL) + t * 7919u);
        srand(this_seed);

        clock_t trial_start = clock();
        SnakeNode trial_result;
        bool ok = nmcs(&seed_node, level, &trial_result);
        double trial_time = (double)(clock() - trial_start) / CLOCKS_PER_SEC;

        if (!ok) {
            fprintf(stderr, "Trial %d: search failed.\n", t + 1);
            continue;
        }

        size_t trial_len = snake_node_get_length(&trial_result);
        printf("Trial %d/%d (rng seed %u): length %zu, time %.2fs\n",
               t + 1, trials, this_seed, trial_len, trial_time);

        if (!have_overall_best || trial_len > snake_node_get_length(&overall_best)) {
            if (have_overall_best) snake_node_free(&overall_best);
            overall_best = trial_result;
            have_overall_best = true;
        } else {
            snake_node_free(&trial_result);
        }
    }

    snake_node_free(&seed_node);

    if (!have_overall_best) {
        fprintf(stderr, "All trials failed.\n");
        return 1;
    }

    size_t len = snake_node_get_length(&overall_best);
    printf("\n=== Best across %d trial(s): length %zu ===\n", trials, len);

    printf("Transitions: ");
    for (size_t i = 0; i < len; i++) {
        printf("%d ", overall_best.transition_sequence[i]);
    }
    printf("\n");

    char msg[256];
    long *vertices = (long *)malloc((len + 1) * sizeof(long));
    if (vertices != NULL) {
        long n = transition_to_vertex(overall_best.transition_sequence, len,
                                      dimension, 0, vertices);
        bool valid = validate_snake(vertices, (size_t)n, msg, sizeof(msg));
        printf("Validation:  %s (%s)\n", valid ? "VALID" : "INVALID", msg);
        print_grid_guarded(vertices, (size_t)n);
        free(vertices);
    }

    save_snake_result(overall_best.transition_sequence, len, dimension);

    snake_node_free(&overall_best);
    return 0;
}
