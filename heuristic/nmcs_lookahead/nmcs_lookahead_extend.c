/* nmcs_lookahead_extend.c - NMCS with a MULTI-STEP LOOKAHEAD fitness signal for
 * move selection, instead of the 1-step fitness used (and found useless) in
 * ../nmcs_fitness/, or the blind forced-move heuristic in ../nmcs/.
 *
 * Independent sibling of ../pruned_bfs_search/, ../stochastic_prune/, ../nmcs/,
 * and ../nmcs_fitness/. Per project policy (../../notes/search-strategy-ideas.md,
 * ../../CLAUDE.md), none of the existing tools are edited in place.
 *
 * WHY (see CLAUDE.md's 2026-07-26 "nmcs_fitness" log): a debug build proved that
 * every legal candidate at a single step has EXACTLY the same resulting fitness
 * (count of unmarked vertices) -- a structural property of the hypercube's local
 * symmetry, not a bug. 1-step fitness therefore has zero discriminating power
 * for move selection. This variant fixes that the way Kris specified: probe
 * each immediate candidate several steps further ahead (with a cheap random
 * continuation, not another recursive search) before comparing fitness, since
 * by then the specific vertices marked along different branches will have
 * diverged enough to actually differ.
 *
 * DESIGN:
 *   - For each of the (dimension-bounded, so cheap) immediate legal candidates
 *     at a real decision point: materialize the 1-step child, then run a
 *     `lookahead` K-step probe forward from a COPY of it (uniform-random
 *     continuation each probe step -- cheap and only needs to break symmetry,
 *     not find an optimal continuation). Read the probe's ending fitness. This
 *     is the score used for softmax move selection over the ORIGINAL immediate
 *     candidates (the probe copies are discarded either way; only the 1-step
 *     child that was actually selected is kept and committed to).
 *   - Selection is still softmax, not greedy argmax, for the same reason as
 *     ../nmcs_fitness/: pure greedy would collapse to a single deterministic
 *     path per trial and destroy the point of independent randomized trials.
 *   - Cost is linear in `lookahead` (K extra cheap probe steps per candidate
 *     per real step), NOT exponential like nesting -- deliberately, since
 *     nesting is what made vanilla NMCS's level 2 expensive for little gain.
 *
 * COMPUTE SAFETY (per Kris's explicit instruction -- do not let this stall
 * silently like the earlier dim18 4 GB retry did):
 *   - `--max-seconds S` is a hard WALL-CLOCK DEADLINE, checked periodically
 *     inside every loop that could run long (the probe loop, the playout loop,
 *     and the nested-search loop), reset fresh at the start of EACH trial. If
 *     exceeded, the search stops immediately and returns whatever best result
 *     it has so far rather than continuing, hanging, or crashing. This is
 *     enforced by the tool itself, not left to an external convention.
 *   - `--lookahead K` is clamped to a sane range to prevent an accidental
 *     runaway from a typo.
 *   - Recommended usage: always test a new dimension with a small trial count
 *     and short --max-seconds first, confirm actual per-trial timing, before
 *     committing to a bigger sweep -- exactly the protocol used to validate
 *     this tool (see CLAUDE.md's test log).
 *
 * Usage:
 *   ./nmcs_lookahead_extend <target_dimension> [--level L] [--lookahead K]
 *       [--temperature T] [--max-seconds S] [--seed N] [--trials T2] [seed_file]
 *   --level L        : NMCS nesting level (default 0).
 *   --lookahead K    : probe depth in steps (default 3, clamped to [1, 50]).
 *   --temperature T  : softmax temperature over probed fitness (default 5.0).
 *   --max-seconds S  : per-trial wall-clock budget (default 30.0). A trial
 *                      that exceeds this returns its best-so-far result
 *                      instead of continuing.
 *   --seed N         : RNG seed (default: time-based).
 *   --trials T2      : run T2 independent searches, keep the best (default 1).
 *   seed_file        : text file of transition integers (default
 *                      extend_input.txt).
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
#include <math.h>

#define MAX_DIM 64

/* ---- Per-trial wall-clock deadline (compute safety) --------------------
 * Reset once per trial in main(); checked from inside the probe/playout/
 * nesting loops so a runaway trial stops itself instead of needing an
 * external timeout. */
static clock_t g_trial_deadline;
static bool g_deadline_hit;

static void deadline_reset(double max_seconds)
{
    g_trial_deadline = clock() + (clock_t)(max_seconds * (double)CLOCKS_PER_SEC);
    g_deadline_hit = false;
}

static bool deadline_exceeded(void)
{
    if (g_deadline_hit) return true;
    if (clock() >= g_trial_deadline) {
        g_deadline_hit = true;
    }
    return g_deadline_hit;
}

/* ---- RNG helper --------------------------------------------------------- */
static double rand_uniform(void)
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

static long rand_below(long n)
{
    if (n <= 1) return 0;
    long v = (long)(rand_uniform() * (double)n);
    return (v >= n) ? n - 1 : v;
}

/* ---- Legal-move enumeration --------------------------------------------- */
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

/* ---- K-step probe: cheap uniform-random continuation, return ending
 * fitness. Operates on its own copy -- never mutates `start`. Stops early
 * (returns whatever fitness it has) on a dead end or a deadline hit, both of
 * which are valid outcomes, not errors. */
static long probe_fitness(const SnakeNode *start, int lookahead)
{
    SnakeNode current;
    if (!snake_node_init(&current, start->transition_sequence, start->length,
                         start->dimension)) {
        return start->fitness;
    }

    int candidates[MAX_DIM + 1];
    for (int step = 0; step < lookahead; step++) {
        if (deadline_exceeded()) break;

        long n_candidates = legal_extendable_moves(&current, candidates);
        if (n_candidates == 0) break;  /* dead end reached during the probe */

        int pick = candidates[rand_below(n_candidates)];
        SnakeNode next;
        if (!snake_node_create_child(&current, pick, &next)) break;
        snake_node_free(&current);
        current = next;
    }

    long result = current.fitness;
    snake_node_free(&current);
    return result;
}

/* ---- Lookahead-fitness-weighted (softmax) playout ----------------------- */
static bool do_playout(const SnakeNode *start, int lookahead, double temperature,
                       SnakeNode *out)
{
    SnakeNode current;
    if (!snake_node_init(&current, start->transition_sequence, start->length,
                         start->dimension)) {
        return false;
    }

    int candidates[MAX_DIM + 1];
    SnakeNode children[MAX_DIM + 1];
    double weights[MAX_DIM + 1];
    long probed[MAX_DIM + 1];

    for (;;) {
        if (deadline_exceeded()) break;

        long n_candidates = legal_extendable_moves(&current, candidates);
        if (n_candidates == 0) break;

        bool ok_all = true;
        for (long i = 0; i < n_candidates; i++) {
            if (!snake_node_create_child(&current, candidates[i], &children[i])) {
                ok_all = false;
                break;
            }
            probed[i] = probe_fitness(&children[i], lookahead);
        }
        if (!ok_all) break;

        long max_probed = probed[0];
        for (long i = 1; i < n_candidates; i++) {
            if (probed[i] > max_probed) max_probed = probed[i];
        }

        double weight_sum = 0.0;
        for (long i = 0; i < n_candidates; i++) {
            double delta = (double)(probed[i] - max_probed);
            double w = (temperature > 0.0) ? exp(delta / temperature)
                                           : (delta == 0.0 ? 1.0 : 0.0);
            weights[i] = w;
            weight_sum += w;
        }

        long chosen = 0;
        if (weight_sum <= 0.0) {
            chosen = rand_below(n_candidates);
        } else {
            double r = rand_uniform() * weight_sum;
            double running = 0.0;
            for (long i = 0; i < n_candidates; i++) {
                running += weights[i];
                chosen = i;
                if (r <= running) break;
            }
        }

        for (long i = 0; i < n_candidates; i++) {
            if (i != chosen) snake_node_free(&children[i]);
        }
        snake_node_free(&current);
        current = children[chosen];
    }

    *out = current;
    return true;
}

/* ---- NMCS: recursive nested search with memoization ---------------------
 * Structurally identical to ../nmcs/ and ../nmcs_fitness/'s nmcs() -- only the
 * base-case playout policy differs. Deadline-checked at the top of the outer
 * loop too, so a slow nested search also self-terminates. */
static bool nmcs(const SnakeNode *state, int level, int lookahead,
                 double temperature, SnakeNode *out)
{
    if (level <= 0 || deadline_exceeded()) {
        return do_playout(state, lookahead, temperature, out);
    }

    SnakeNode current;
    if (!snake_node_init(&current, state->transition_sequence, state->length,
                         state->dimension)) {
        return false;
    }

    SnakeNode memorized_best;
    if (!do_playout(state, lookahead, temperature, &memorized_best)) {
        snake_node_free(&current);
        return false;
    }

    int candidates[MAX_DIM + 1];
    for (;;) {
        if (deadline_exceeded()) break;

        long n_candidates = legal_extendable_moves(&current, candidates);
        if (n_candidates == 0) break;

        bool have_best_child = false;
        SnakeNode best_child;
        SnakeNode best_result;
        size_t best_score = 0;

        for (long i = 0; i < n_candidates; i++) {
            if (deadline_exceeded()) break;
            SnakeNode child;
            if (!snake_node_create_child(&current, candidates[i], &child)) {
                continue;
            }
            SnakeNode result;
            if (!nmcs(&child, level - 1, lookahead, temperature, &result)) {
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

        if (!have_best_child) break;

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

/* ---- Seed loading (same simplified loader as the other siblings) ------- */

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

/* ---- Driver -------------------------------------------------------------- */

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
        "Usage: %s <target_dimension> [--level L] [--lookahead K]\n"
        "    [--temperature T] [--max-seconds S] [--seed N] [--trials T2]\n"
        "    [seed_file]\n"
        "  --level L        : NMCS nesting level (default 0).\n"
        "  --lookahead K    : probe depth in steps (default 3, clamped to\n"
        "                     [1, 50]).\n"
        "  --temperature T  : softmax temperature over probed fitness\n"
        "                     (default 5.0).\n"
        "  --max-seconds S  : per-trial wall-clock budget (default 30.0) --\n"
        "                     a trial exceeding this returns its best-so-far\n"
        "                     result instead of continuing. Compute safety\n"
        "                     net, not a soft suggestion.\n"
        "  --seed N         : RNG seed (default: time-based).\n"
        "  --trials T2      : run T2 independent searches, keep the best\n"
        "                     (default 1).\n"
        "  seed_file        : text file of transition integers (default\n"
        "                     extend_input.txt).\n",
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

    int level = 0;
    int lookahead = 3;
    double temperature = 5.0;
    double max_seconds = 30.0;
    unsigned int rng_seed = (unsigned int)time(NULL);
    bool seed_set = false;
    int trials = 1;
    const char *seed_path = "extend_input.txt";

    for (int ai = 2; ai < argc; ai++) {
        const char *a = argv[ai];
        if (strcmp(a, "--level") == 0 && ai + 1 < argc) {
            level = atoi(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--lookahead") == 0 && ai + 1 < argc) {
            lookahead = atoi(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--temperature") == 0 && ai + 1 < argc) {
            temperature = atof(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--max-seconds") == 0 && ai + 1 < argc) {
            max_seconds = atof(argv[++ai]);
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
    }

    if (level < 0) level = 0;
    if (lookahead < 1) lookahead = 1;
    if (lookahead > 50) lookahead = 50;
    if (max_seconds <= 0.0) max_seconds = 1.0;
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

    printf("Snake-in-the-box seeded search (NMCS, multi-step lookahead "
           "fitness)\n");
    printf("Dimension: %d, level: %d, lookahead: %d, temperature: %.3f, "
           "max-seconds/trial: %.1f, trials: %d\n",
           dimension, level, lookahead, temperature, max_seconds, trials);
    printf("Seed: %s (length %zu)\n\n", seed_path, seed_len);

    SnakeNode overall_best;
    bool have_overall_best = false;
    int deadline_hits = 0;

    for (int t = 0; t < trials; t++) {
        unsigned int this_seed = seed_set ? (rng_seed + (unsigned int)t)
                                          : (unsigned int)(time(NULL) + t * 7919u);
        srand(this_seed);
        deadline_reset(max_seconds);

        clock_t trial_start = clock();
        SnakeNode trial_result;
        bool ok = nmcs(&seed_node, level, lookahead, temperature, &trial_result);
        double trial_time = (double)(clock() - trial_start) / CLOCKS_PER_SEC;
        bool hit_deadline = g_deadline_hit;
        if (hit_deadline) deadline_hits++;

        if (!ok) {
            fprintf(stderr, "Trial %d: search failed.\n", t + 1);
            continue;
        }

        size_t trial_len = snake_node_get_length(&trial_result);
        printf("Trial %d/%d (rng seed %u): length %zu, time %.2fs%s\n",
               t + 1, trials, this_seed, trial_len, trial_time,
               hit_deadline ? "  [DEADLINE HIT -- best-so-far returned]" : "");

        if (!have_overall_best || trial_len > snake_node_get_length(&overall_best)) {
            if (have_overall_best) snake_node_free(&overall_best);
            overall_best = trial_result;
            have_overall_best = true;
        } else {
            snake_node_free(&trial_result);
        }
    }

    snake_node_free(&seed_node);

    if (deadline_hits > 0) {
        printf("\nNOTE: %d/%d trial(s) hit the %.1fs per-trial deadline and "
               "returned early.\n", deadline_hits, trials, max_seconds);
    }

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
