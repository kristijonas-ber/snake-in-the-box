/* nmcs_fitness_extend.c - Nested Monte Carlo Search with FITNESS-WEIGHTED
 * (softmax) move selection, instead of vanilla NMCS's forced-move+uniform
 * playout policy.
 *
 * Independent sibling of ../pruned_bfs_search/, ../stochastic_prune/, and
 * ../nmcs/. Per project policy (../../notes/search-strategy-ideas.md,
 * ../../CLAUDE.md), none of the existing tools are edited in place.
 *
 * WHY (see CLAUDE.md's 2026-07-26 "NMCS across more dimensions" log): vanilla
 * NMCS's relative gap to the beam-search baseline widened as dimension grew
 * (83% -> 77% -> 74% -> 70% of baseline across dim9->10 through dim12->13), and
 * scaling trials/nesting level gave sharply diminishing returns (30x more
 * trials on dim9->10 bought +4% length; level 2 cost ~80x more than level 1 for
 * only +8%). Diagnosis: vanilla NMCS's playout never consults the fitness
 * signal (count of unmarked/reachable vertices) that the beam search uses to
 * rank every candidate -- it only compares *finished* playout lengths, long
 * after any early bad choice already mattered. This variant fixes that by
 * using the SAME fitness value the beam search computes (SnakeNode.fitness,
 * set automatically by snake_node_create_child) to weight move choice inside
 * the playout itself.
 *
 * DESIGN (see the conversation this was speced in for the full reasoning):
 *   - Move choice = softmax sampling over each candidate's resulting fitness,
 *     NOT greedy argmax. Pure greedy would collapse to a single deterministic
 *     path (beam-search-at-width-1 in disguise) and both (a) likely underperform
 *     even vanilla NMCS -- greedy-on-a-local-signal is known to get stuck in
 *     worse local optima than a wide population explores, which is exactly what
 *     this project's own beam-search plateau already demonstrated -- and (b)
 *     destroy the point of running independent randomized trials at all.
 *   - P(move_i) ~ exp((fitness_i - max_fitness_among_candidates) / temperature).
 *     Subtracting the max before exponentiating keeps this numerically stable
 *     regardless of the absolute fitness scale (which can be large: up to
 *     2^dimension). temperature -> 0 approaches pure greedy (the risky
 *     degenerate case above); temperature -> infinity approaches uniform random
 *     (vanilla NMCS's playout, minus its forced-move bias).
 *   - This subsumes Kinny's "prefer a forced move" heuristic as a special case
 *     for free: a state with exactly one legal move gets probability 1 for it
 *     regardless of temperature, so there is no separate forced-move lookahead
 *     code here (unlike ../nmcs/nmcs_extend.c) -- one unified policy.
 *   - The outer nested-search loop (level >= 1: try every move, recurse one
 *     level down, commit to whichever move's nested result scored longest) is
 *     UNCHANGED from vanilla NMCS -- that argmax-over-nested-results choice is
 *     intrinsic to NMCS's own design (Cazenave/Kinny), already validated by the
 *     vanilla implementation, and not what this variant is testing. Only the
 *     level-0 playout policy differs.
 *
 * Usage:
 *   ./nmcs_fitness_extend <target_dimension> [--level L] [--temperature T]
 *                         [--seed N] [--trials T2] [seed_file]
 *   --level L        : NMCS nesting level (default 0 -- see design notes: this
 *                      isolates "does fitness-guided move choice alone help?"
 *                      from "does nesting help?", and vanilla NMCS's own
 *                      level-2 cost/benefit was poor, so nesting isn't
 *                      defaulted-on here).
 *   --temperature T  : softmax temperature (default 5.0). Lower = greedier,
 *                      higher = closer to uniform random. Fitness differences
 *                      between sibling candidates are typically small integers
 *                      (a handful of newly-marked vertices), so T in roughly
 *                      the 1-50 range is the meaningful tuning band.
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

/* ---- RNG helper: uniform double in [0, 1) ------------------------------ */
static double rand_uniform(void)
{
    return (double)rand() / ((double)RAND_MAX + 1.0);
}

/* ---- Legal-move enumeration for a node --------------------------------- */
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

/* ---- Fitness-weighted (softmax) playout -------------------------------- *
 * At each step, materialize every legal candidate child (this also gives us
 * its fitness for free -- the same value the beam search ranks by), sample
 * one via softmax over fitness, keep it, free the rest. */
static bool do_playout(const SnakeNode *start, double temperature,
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

    for (;;) {
        long n_candidates = legal_extendable_moves(&current, candidates);
        if (n_candidates == 0) {
            break;  /* dead end: playout complete */
        }

        bool ok_all = true;
        long max_fitness = 0;
        for (long i = 0; i < n_candidates; i++) {
            if (!snake_node_create_child(&current, candidates[i], &children[i])) {
                ok_all = false;
                break;
            }
            if (i == 0 || children[i].fitness > max_fitness) {
                max_fitness = children[i].fitness;
            }
        }
        if (!ok_all) {
            /* Allocation failure mid-enumeration: free what we made and stop
             * (treat as a dead end rather than crash). */
            break;
        }

        /* Softmax over (fitness - max_fitness), so the largest exponent is
         * always 0 -- numerically stable regardless of the raw fitness scale
         * (up to 2^dimension). temperature <= 0 is treated as "pure greedy"
         * (temperature -> 0 limit) to avoid division by zero. */
        double weight_sum = 0.0;
        for (long i = 0; i < n_candidates; i++) {
            double delta = (double)(children[i].fitness - max_fitness);
            double w = (temperature > 0.0) ? exp(delta / temperature)
                                           : (delta == 0.0 ? 1.0 : 0.0);
            weights[i] = w;
            weight_sum += w;
        }

        long chosen = 0;
        if (weight_sum <= 0.0) {
            /* Degenerate (shouldn't happen: at least one delta is 0, weight
             * 1.0) -- fall back to uniform to stay safe. */
            chosen = (long)(rand_uniform() * (double)n_candidates);
            if (chosen >= n_candidates) chosen = n_candidates - 1;
        } else {
            double r = rand_uniform() * weight_sum;
            double running = 0.0;
            for (long i = 0; i < n_candidates; i++) {
                running += weights[i];
                if (r <= running) {
                    chosen = i;
                    break;
                }
                chosen = i;  /* guards float rounding landing past the end */
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

/* ---- NMCS: recursive nested search with memoization --------------------
 * Structurally identical to ../nmcs/nmcs_extend.c's nmcs() -- only the base
 * case (do_playout) differs. The outer "try every move, recurse, commit to
 * the best nested result" logic is intentionally unchanged; see the file
 * header for why. */
static bool nmcs(const SnakeNode *state, int level, double temperature,
                 SnakeNode *out)
{
    if (level <= 0) {
        return do_playout(state, temperature, out);
    }

    SnakeNode current;
    if (!snake_node_init(&current, state->transition_sequence, state->length,
                         state->dimension)) {
        return false;
    }

    SnakeNode memorized_best;
    if (!do_playout(state, temperature, &memorized_best)) {
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
            if (!nmcs(&child, level - 1, temperature, &result)) {
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
            break;
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

/* ---- Seed loading (same simplified loader as ../nmcs/nmcs_extend.c) ---- */

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
        "Usage: %s <target_dimension> [--level L] [--temperature T]\n"
        "                              [--seed N] [--trials T2] [seed_file]\n"
        "  --level L       : NMCS nesting level (default 0).\n"
        "  --temperature T : softmax temperature over fitness (default 5.0).\n"
        "                    Lower = greedier, higher = closer to uniform.\n"
        "  --seed N        : RNG seed (default: time-based).\n"
        "  --trials T2     : run T2 independent searches, keep the best\n"
        "                    (default 1).\n"
        "  seed_file       : text file of transition integers (default\n"
        "                    extend_input.txt).\n",
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
    double temperature = 5.0;
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
        if (strcmp(a, "--temperature") == 0 && ai + 1 < argc) {
            temperature = atof(argv[++ai]);
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

    printf("Snake-in-the-box seeded search (Nested Monte Carlo Search, "
           "fitness-weighted playout)\n");
    printf("Dimension: %d, level: %d, temperature: %.3f, trials: %d\n",
           dimension, level, temperature, trials);
    printf("Seed: %s (length %zu)\n\n", seed_path, seed_len);

    SnakeNode overall_best;
    bool have_overall_best = false;

    for (int t = 0; t < trials; t++) {
        unsigned int this_seed = seed_set ? (rng_seed + (unsigned int)t)
                                          : (unsigned int)(time(NULL) + t * 7919u);
        srand(this_seed);

        clock_t trial_start = clock();
        SnakeNode trial_result;
        bool ok = nmcs(&seed_node, level, temperature, &trial_result);
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
