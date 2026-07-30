/* genetic_extend.c - Genetic algorithm with real crossover for snake-in-the-box,
 * extending an existing snake into a higher dimension.
 *
 * Independent sibling of ../pruned_bfs_search/, ../stochastic_prune/, ../nmcs/,
 * ../nmcs_fitness/, and ../nmcs_lookahead/. Per project policy
 * (../../notes/search-strategy-ideas.md, ../../CLAUDE.md), none of the
 * existing tools are edited in place.
 *
 * DESIGN, sourced from the actual primary literature (not a guess) -- see
 * CLAUDE.md's 2026-07-26 "genetic algorithm" research log for the full
 * citation trail:
 *   - W.D. Potter, R.W. Robinson, J.A. Miller, K. Kochut, D.Z. Redys, "Using
 *     the Genetic Algorithm to Find Snake-in-the-Box Codes," IEA/AIE 1994
 *     (the original method).
 *   - P.A. Diaz-Gomez, D.F. Hougen, "The Snake in the Box Problem:
 *     Mathematical Conjecture and a Genetic Algorithm Approach," GECCO 2006
 *     (a direct replication with full method detail -- read in full, not just
 *     the abstract, precisely because this crossover/validity mechanism is
 *     easy to hand-wave in a summary).
 *
 * THE CORE DESIGN DECISION (this is the part that matters): the chromosome is
 * a flat BIT VECTOR of length 2^dimension, one bit per hypercube vertex --
 * bit v = 1 means "vertex v is a member of the snake's vertex set." This is
 * NOT a transition sequence or node-order encoding. The reason: any bit
 * combination is a syntactically valid bitstring, so ORDINARY crossover
 * (here: single-point) can NEVER produce a malformed chromosome the way
 * crossing two orderings/sequences would (revisited vertices, broken
 * adjacency). What crossover CAN produce is a vertex set that doesn't form a
 * valid induced path -- and that is handled entirely by the FITNESS FUNCTION,
 * not by a repair operator: fitness walks from a designated head vertex along
 * same-set edges, and the walk simply stops (scoring only the valid prefix)
 * at the first violation, revisit, or dead end. Invalid genome material past
 * that point isn't rejected, just economically ignored by the score -- this
 * is the actual mechanism the source papers use, faithfully reimplemented
 * here (their Equation 1).
 *
 * SEEDING (matches this project's existing extend-from-seed convention
 * exactly, and is also what the source paper does): to extend a dimension-d
 * seed into dimension d+1, the chromosome is 2^(d+1) bits. The first 2^d bits
 * are fixed to the seed's vertex membership and never mutated or crossed --
 * only bits >= 2^d (the newly available dimension) evolve. Per the paper: at
 * initialization, the "mirror" bit (v + 2^d) of every INTERIOR seed vertex v
 * (i.e. not the seed's head or tail) is forced to 0, since that vertex
 * already has its 2 allowed snake-neighbors and turning on its new-dimension
 * mirror would trivially create a 3-neighbor violation. The seed's head and
 * tail mirrors ARE freely randomizable (those vertices have room for exactly
 * one more edge -- extending into the new dimension from an endpoint is
 * exactly how a legitimate one-edge dimensional extension looks).
 *
 * FITNESS (their Equation 1, reimplemented faithfully):
 *   F = ((sum(MP) - Penalty) / sum(MP)) * ((Length_S + 1) / #P)
 *   MP[v]    = count of v's hypercube-neighbors that are also set (bit=1).
 *   Penalty  = count of vertices with MP>2 (over-connected)
 *            + count of set vertices with MP==0 (isolated)
 *            + count of unset vertices with MP==0 ("lazy": paper's term)
 *            + max(0, num_degree1_vertices - 2) (more than one head/tail pair)
 *   Length_S = length of the walk from the designated head (chosen as: the
 *              lowest-index degree-1 vertex, i.e. one with exactly one set
 *              neighbor; if none exists, the lowest-index set vertex) along
 *              set-membership edges, stopping at a revisit, a vertex with
 *              MP != <=2 structure that breaks the path, or a dead end.
 *   #P       = total count of set bits.
 * Not specified in the source: the exact mutation rate. Using the standard
 * GA default of 1/L (L = length of the mutable region) and flagging this
 * explicitly as a convention fill, not a sourced parameter.
 *
 * SELECTION: tournament of 2, sourced exactly from the paper: 75% chance the
 * higher-fitness parent wins, 25% chance the lower-fitness one does
 * (deliberately not a hard tournament -- preserves some diversity).
 *
 * CROSSOVER: single-point, confined to the mutable (post-seed) region only.
 *
 * COMPUTE SAFETY (same discipline as ../nmcs_lookahead/, per Kris's standing
 * instruction not to let a run stall silently): a hard wall-clock
 * `--max-seconds` deadline, checked once per generation (cheap to check at
 * that granularity), causing the run to return its best individual so far
 * instead of continuing.
 *
 * Usage:
 *   ./genetic_extend <target_dimension> [--population P] [--generations G]
 *       [--mutation-rate R] [--max-seconds S] [--seed N] [seed_file]
 *   --population P    : population size (default 200 -- smaller than the
 *                        paper's 1000/10000 for a cheap first test; scale up
 *                        once timing is confirmed sane).
 *   --generations G   : generation count (default 200).
 *   --mutation-rate R : per-bit mutation probability in the mutable region
 *                        (default -1 meaning "use 1/L", the standard
 *                        convention since the source doesn't specify one).
 *   --max-seconds S   : wall-clock budget for the whole run (default 30.0).
 *                        Checked once per generation; the run returns its
 *                        best-so-far individual if exceeded.
 *   --seed N          : RNG seed (default: time-based).
 *   seed_file         : text file of transition integers (default
 *                        extend_input.txt).
 */
#define _POSIX_C_SOURCE 200809L

#include "transitions.h"
#include "validation.h"
#include "snake_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned char u8;

/* ---- RNG helpers --------------------------------------------------------- */
static double rand_uniform(void) { return (double)rand() / ((double)RAND_MAX + 1.0); }
static long rand_below(long n) {
    if (n <= 1) return 0;
    long v = (long)(rand_uniform() * (double)n);
    return (v >= n) ? n - 1 : v;
}

/* ---- Chromosome: bit vector, one byte per vertex for simplicity (clarity
 * over memory density -- dimensions this project targets keep 2^dim in the
 * low millions at most, which is fine as a byte array). ------------------- */
typedef struct {
    u8 *bits;      /* bits[v] = 1 iff vertex v is in the snake's vertex set */
    long n;        /* 2^dimension */
    int dimension;
} Chromosome;

static bool chromosome_alloc(Chromosome *c, int dimension) {
    c->dimension = dimension;
    c->n = 1L << dimension;
    c->bits = (u8 *)calloc((size_t)c->n, 1);
    return c->bits != NULL;
}

static void chromosome_free(Chromosome *c) { free(c->bits); c->bits = NULL; }

static void chromosome_copy(Chromosome *dst, const Chromosome *src) {
    memcpy(dst->bits, src->bits, (size_t)src->n);
}

/* Neighbor v^(1<<d) for d in [0, dimension). */
static long neighbor(long v, int d) { return v ^ (1L << d); }

/* ---- Fitness (Equation 1 from the source paper, reimplemented) --------- */
typedef struct {
    double fitness;
    long length_s;   /* best walk length found (edges) */
    long head;       /* vertex the best walk started from */
} FitnessResult;

/* Count of v's in-chromosome neighbors (MP[v] in the paper's notation). */
static int neighbor_count(const Chromosome *c, long v) {
    int cnt = 0;
    for (int d = 0; d < c->dimension; d++) {
        if (c->bits[neighbor(v, d)]) cnt++;
    }
    return cnt;
}

/* Walk from `head` along set-membership edges, stopping at a revisit, a
 * vertex whose neighbor-count structure breaks the path (0 or >=3 completed
 * neighbors from the walk's perspective -- practically: no unvisited
 * in-chromosome neighbor to continue to), or a dead end. Returns edges
 * walked. `visited` is caller-provided scratch of length c->n, zeroed by this
 * function on the vertices it touches (cheap: only touches walked vertices). */
static long walk_length(const Chromosome *c, long head, u8 *visited_scratch,
                        long *touched, long *n_touched)
{
    long cur = head;
    visited_scratch[cur] = 1;
    touched[(*n_touched)++] = cur;
    long len = 0;

    for (;;) {
        long next = -1;
        int found = 0;
        for (int d = 0; d < c->dimension; d++) {
            long nb = neighbor(cur, d);
            if (c->bits[nb] && !visited_scratch[nb]) {
                found++;
                next = nb;
            }
        }
        /* A valid path interior vertex has exactly one unvisited in-set
         * neighbor to continue to (the other is where we came from). If
         * there's more than one, the path branches (a chord/violation from
         * the walk's perspective) -- stop here rather than guess. */
        if (found != 1) break;
        visited_scratch[next] = 1;
        touched[(*n_touched)++] = next;
        cur = next;
        len++;
    }
    return len;
}

static FitnessResult evaluate_fitness(const Chromosome *c, u8 *visited_scratch,
                                      long *touched_scratch)
{
    long sum_mp = 0;
    long penalty = 0;
    long num_set = 0;
    long degree1_count = 0;
    long first_degree1 = -1;
    long first_set = -1;

    for (long v = 0; v < c->n; v++) {
        int mp = neighbor_count(c, v);
        sum_mp += mp;
        if (c->bits[v]) {
            num_set++;
            if (first_set < 0) first_set = v;
            if (mp > 2) penalty++;               /* over-connected */
            if (mp == 0) penalty++;               /* isolated point */
            if (mp == 1) {
                degree1_count++;
                if (first_degree1 < 0) first_degree1 = v;
            }
        } else {
            if (mp == 0) penalty++;               /* "lazy point" per the paper */
        }
    }
    if (degree1_count > 2) penalty += (degree1_count - 2);

    long head = (first_degree1 >= 0) ? first_degree1 : first_set;
    long length_s = 0;
    if (head >= 0) {
        long n_touched = 0;
        length_s = walk_length(c, head, visited_scratch, touched_scratch, &n_touched);
        for (long i = 0; i < n_touched; i++) visited_scratch[touched_scratch[i]] = 0;
    }

    FitnessResult r;
    r.length_s = length_s;
    r.head = (head >= 0) ? head : 0;
    if (sum_mp <= 0 || num_set <= 0) {
        r.fitness = 0.0;
        return r;
    }
    double connectivity_term = (double)(sum_mp - penalty) / (double)sum_mp;
    double length_term = (double)(length_s + 1) / (double)num_set;
    r.fitness = connectivity_term * length_term;
    return r;
}

/* ---- GA operators -------------------------------------------------------- */

/* Single-point crossover confined to [mutable_start, n). Produces two
 * children from two parents; the fixed seed region is copied unchanged. */
static void crossover(const Chromosome *p1, const Chromosome *p2,
                      Chromosome *c1, Chromosome *c2, long mutable_start)
{
    memcpy(c1->bits, p1->bits, (size_t)mutable_start);
    memcpy(c2->bits, p2->bits, (size_t)mutable_start);

    long mutable_len = p1->n - mutable_start;
    long point = mutable_start + rand_below(mutable_len);

    memcpy(c1->bits + mutable_start, p1->bits + mutable_start,
          (size_t)(point - mutable_start));
    memcpy(c1->bits + point, p2->bits + point, (size_t)(p1->n - point));

    memcpy(c2->bits + mutable_start, p2->bits + mutable_start,
          (size_t)(point - mutable_start));
    memcpy(c2->bits + point, p1->bits + point, (size_t)(p1->n - point));
}

static void mutate(Chromosome *c, long mutable_start, double rate)
{
    for (long v = mutable_start; v < c->n; v++) {
        if (rand_uniform() < rate) {
            c->bits[v] = c->bits[v] ? 0 : 1;
        }
    }
}

/* Tournament of 2: 75% chance the higher-fitness parent wins, 25% the lower
 * (exact spec from the source paper). */
static long tournament_select(const double *fitness, long pop_size)
{
    long a = rand_below(pop_size);
    long b = rand_below(pop_size);
    long better = (fitness[a] >= fitness[b]) ? a : b;
    long worse = (better == a) ? b : a;
    return (rand_uniform() < 0.75) ? better : worse;
}

/* ---- Seed loading (same simplified loader as the other siblings) -------- */

static bool load_seed_vertices(const char *path, int seed_dimension,
                               long **out_verts, long *out_count)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "Could not open seed file '%s'\n", path);
        return false;
    }
    long cap = (long)1 << seed_dimension;
    int *trans = (int *)malloc((size_t)cap * sizeof(int));
    if (trans == NULL) { fclose(f); return false; }

    long count = 0;
    int value;
    while (fscanf(f, "%d", &value) == 1) {
        if (count >= cap) {
            fprintf(stderr, "Seed '%s' too long\n", path);
            free(trans);
            fclose(f);
            return false;
        }
        trans[count++] = value;
    }
    fclose(f);

    char msg[256];
    if (!validate_transition_sequence(trans, (size_t)count, seed_dimension, msg,
                                      sizeof(msg))) {
        fprintf(stderr, "Invalid seed '%s': %s\n", path, msg);
        free(trans);
        return false;
    }

    long *verts = (long *)malloc((size_t)(count + 1) * sizeof(long));
    if (verts == NULL) { free(trans); return false; }
    long n = transition_to_vertex(trans, (size_t)count, seed_dimension, 0, verts);
    free(trans);
    if (n < 0) { free(verts); return false; }

    *out_verts = verts;
    *out_count = n;
    return true;
}

/* ---- Driver -------------------------------------------------------------- */

static bool is_number(const char *s) {
    char *end;
    strtod(s, &end);
    return *s != '\0' && *end == '\0';
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <target_dimension> [--population P] [--generations G]\n"
        "    [--mutation-rate R] [--max-seconds S] [--seed N] [seed_file]\n"
        "  --population P    : population size (default 200).\n"
        "  --generations G   : generation count (default 200).\n"
        "  --mutation-rate R : per-bit mutation prob in the mutable region\n"
        "                      (default: 1/mutable_length, since the source\n"
        "                      paper does not specify a rate).\n"
        "  --max-seconds S   : wall-clock budget for the run (default 30.0).\n"
        "  --seed N          : RNG seed (default: time-based).\n"
        "  seed_file         : text file of transition integers (default\n"
        "                      extend_input.txt).\n",
        prog);
}

int main(int argc, char **argv)
{
    snake_io_set_base(argv[0]);
    if (argc < 2) { usage(argv[0]); return 1; }

    int dimension = atoi(argv[1]);
    if (dimension < 2 || dimension > 24) {
        fprintf(stderr, "Target dimension must be in [2, 24], got %d "
                        "(24 keeps 2^24-byte chromosomes -- ~16MB -- sane)\n",
                dimension);
        return 1;
    }

    long population_size = 200;
    long generations = 200;
    double mutation_rate = -1.0;  /* sentinel: fill with 1/mutable_len later */
    double max_seconds = 30.0;
    unsigned int rng_seed = (unsigned int)time(NULL);
    const char *seed_path = "extend_input.txt";

    for (int ai = 2; ai < argc; ai++) {
        const char *a = argv[ai];
        if (strcmp(a, "--population") == 0 && ai + 1 < argc) {
            population_size = atol(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--generations") == 0 && ai + 1 < argc) {
            generations = atol(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--mutation-rate") == 0 && ai + 1 < argc) {
            mutation_rate = atof(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--max-seconds") == 0 && ai + 1 < argc) {
            max_seconds = atof(argv[++ai]);
            continue;
        }
        if (strcmp(a, "--seed") == 0 && ai + 1 < argc) {
            rng_seed = (unsigned int)strtoul(argv[++ai], NULL, 10);
            continue;
        }
        if (strcmp(a, "--help") == 0) { usage(argv[0]); return 0; }
        (void)is_number;
        seed_path = a;
    }

    if (population_size < 4) population_size = 4;
    if (generations < 1) generations = 1;
    if (max_seconds <= 0.0) max_seconds = 1.0;
    srand(rng_seed);

    int seed_dimension = dimension - 1;
    long *seed_verts;
    long seed_count;
    if (!load_seed_vertices(seed_path, seed_dimension, &seed_verts, &seed_count)) {
        return 1;
    }

    long mutable_start = 1L << seed_dimension;
    long n = 1L << dimension;
    long mutable_len = n - mutable_start;
    if (mutation_rate < 0.0) mutation_rate = 1.0 / (double)mutable_len;

    printf("Snake-in-the-box genetic algorithm (bit-vector encoding, "
          "real crossover)\n");
    printf("Target dimension: %d (extending from seed dimension %d)\n",
          dimension, seed_dimension);
    printf("Population: %ld, generations: %ld, mutation rate: %.6f, "
          "max-seconds: %.1f\n", population_size, generations, mutation_rate,
          max_seconds);
    printf("Seed: %s (%ld vertices, %ld edges)\n\n", seed_path, seed_count,
          seed_count - 1);

    /* Build the fixed seed membership bitmap, and mark which seed vertices
     * are interior (degree 2 within the seed) vs. endpoints (degree 1: the
     * head and tail). */
    u8 *seed_bits = (u8 *)calloc((size_t)mutable_start, 1);
    u8 *seed_is_endpoint = (u8 *)calloc((size_t)mutable_start, 1);
    for (long i = 0; i < seed_count; i++) seed_bits[seed_verts[i]] = 1;
    if (seed_count >= 1) {
        seed_is_endpoint[seed_verts[0]] = 1;
        seed_is_endpoint[seed_verts[seed_count - 1]] = 1;
    }

    /* Population init. */
    Chromosome *pop = (Chromosome *)malloc((size_t)population_size * sizeof(Chromosome));
    Chromosome *next_pop = (Chromosome *)malloc((size_t)population_size * sizeof(Chromosome));
    for (long i = 0; i < population_size; i++) {
        chromosome_alloc(&pop[i], dimension);
        chromosome_alloc(&next_pop[i], dimension);
        memcpy(pop[i].bits, seed_bits, (size_t)mutable_start);
        for (long v = mutable_start; v < n; v++) {
            long seed_v = v - mutable_start;  /* the vertex this mirrors */
            if (seed_bits[seed_v] && !seed_is_endpoint[seed_v]) {
                pop[i].bits[v] = 0;  /* forced off: interior seed vertex mirror */
            } else {
                pop[i].bits[v] = (rand_uniform() < 0.5) ? 1 : 0;
            }
        }
    }
    free(seed_bits);
    free(seed_is_endpoint);
    free(seed_verts);

    u8 *visited_scratch = (u8 *)calloc((size_t)n, 1);
    long *touched_scratch = (long *)malloc((size_t)n * sizeof(long));
    double *fitness = (double *)malloc((size_t)population_size * sizeof(double));
    long *lengths = (long *)malloc((size_t)population_size * sizeof(long));

    long best_length_ever = seed_count - 1;
    Chromosome best_ever;
    chromosome_alloc(&best_ever, dimension);
    memcpy(best_ever.bits, pop[0].bits, (size_t)n);
    long best_head_ever = 0;

    clock_t start_time = clock();
    clock_t deadline = start_time + (clock_t)(max_seconds * (double)CLOCKS_PER_SEC);
    bool hit_deadline = false;
    long gen_completed = 0;

    for (long gen = 0; gen < generations; gen++) {
        if (clock() >= deadline) { hit_deadline = true; break; }

        long gen_best_idx = 0;
        for (long i = 0; i < population_size; i++) {
            FitnessResult r = evaluate_fitness(&pop[i], visited_scratch, touched_scratch);
            fitness[i] = r.fitness;
            lengths[i] = r.length_s;
            if (r.length_s > best_length_ever) {
                best_length_ever = r.length_s;
                memcpy(best_ever.bits, pop[i].bits, (size_t)n);
                best_head_ever = r.head;
            }
            if (lengths[i] > lengths[gen_best_idx]) gen_best_idx = i;
        }

        if (gen % 20 == 0 || gen == generations - 1) {
            printf("Generation %ld: best length this gen = %ld, best ever = %ld\n",
                  gen, lengths[gen_best_idx], best_length_ever);
        }

        /* Elitism: carry the single best individual over unchanged (not in
         * the source paper -- a standard, low-risk addition -- flagged here
         * for transparency). */
        chromosome_copy(&next_pop[0], &pop[gen_best_idx]);
        long filled = 1;
        while (filled < population_size) {
            long pa = tournament_select(fitness, population_size);
            long pb = tournament_select(fitness, population_size);
            if (filled + 1 < population_size) {
                crossover(&pop[pa], &pop[pb], &next_pop[filled], &next_pop[filled + 1],
                         mutable_start);
                mutate(&next_pop[filled], mutable_start, mutation_rate);
                mutate(&next_pop[filled + 1], mutable_start, mutation_rate);
                filled += 2;
            } else {
                chromosome_copy(&next_pop[filled], &pop[pa]);
                mutate(&next_pop[filled], mutable_start, mutation_rate);
                filled += 1;
            }
        }

        for (long i = 0; i < population_size; i++) {
            Chromosome tmp = pop[i];
            pop[i] = next_pop[i];
            next_pop[i] = tmp;
        }
        gen_completed = gen + 1;
    }

    if (hit_deadline) {
        printf("\nNOTE: hit the %.1fs wall-clock deadline after %ld/%ld "
              "generations -- returning best-so-far.\n", max_seconds,
              gen_completed, generations);
    }

    printf("\n=== Best found: length %ld ===\n", best_length_ever);

    /* Reconstruct the vertex sequence by walking from best_head_ever, and
     * save via the same convention as the other tools. */
    long *walk_verts = (long *)malloc((size_t)n * sizeof(long));
    long wv = 0;
    {
        u8 *visited = (u8 *)calloc((size_t)n, 1);
        long cur = best_head_ever;
        visited[cur] = 1;
        walk_verts[wv++] = cur;
        for (;;) {
            long next = -1;
            int found = 0;
            for (int d = 0; d < dimension; d++) {
                long nb = neighbor(cur, d);
                if (best_ever.bits[nb] && !visited[nb]) { found++; next = nb; }
            }
            if (found != 1) break;
            visited[next] = 1;
            walk_verts[wv++] = next;
            cur = next;
        }
        free(visited);
    }

    int *out_trans = (int *)malloc((size_t)(wv > 0 ? wv - 1 : 1) * sizeof(int));
    long n_trans = (wv >= 2) ? vertex_to_transition(walk_verts, (size_t)wv, out_trans) : 0;

    if (n_trans > 0) {
        char msg[256];
        bool valid = validate_transition_sequence(out_trans, (size_t)n_trans, dimension,
                                                  msg, sizeof(msg));
        printf("Length: %ld edges. Validation: %s (%s)\n", n_trans,
              valid ? "VALID" : "INVALID", msg);
        printf("Transitions: ");
        for (long i = 0; i < n_trans; i++) printf("%d ", out_trans[i]);
        printf("\n");
        save_snake_result(out_trans, (size_t)n_trans, dimension);
    } else {
        fprintf(stderr, "Best individual produced no usable walk.\n");
    }

    free(out_trans);
    free(walk_verts);
    chromosome_free(&best_ever);
    free(visited_scratch);
    free(touched_scratch);
    free(fitness);
    free(lengths);
    for (long i = 0; i < population_size; i++) {
        chromosome_free(&pop[i]);
        chromosome_free(&next_pop[i]);
    }
    free(pop);
    free(next_pop);
    return 0;
}
