/* config.hpp — shared compile-time configuration for dfs_search.
 *
 * ONE header, included by every translation unit, so all TUs agree on N,
 * PREFIX_LENGTH, the slice knobs, and the MPI message layout. The Makefile
 * MUST pass any -D overrides to every .cpp (a divergent -D across TUs would be
 * a silent ABI mismatch on the shared struct/array sizes).
 *
 * Every knob is #ifndef-guarded so `-DN=8 -DPREFIX_LENGTH=19 ...` overrides it.
 */
#pragma once

#if defined(_MSC_VER)
#include <intrin.h>
#endif

/* Portable count-trailing-zeros (MSVC has no __builtin_ctz). x must be nonzero,
 * which it always is here (consecutive snake vertices differ in exactly one bit). */
static inline int sib_ctz(unsigned x)
{
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanForward(&i, (unsigned long)x);
    return (int)i;
#else
    return __builtin_ctz(x);
#endif
}

/* ---- problem size ---- */
#ifndef N
#define N 6
#endif
#define VERTICES   (1 << N)
#define MAX_LENGTH VERTICES

#ifndef PREFIX_LENGTH
#define PREFIX_LENGTH 11
#endif

/* ---- slice selection (the v14 core) ----
 *
 * The prefix generator is a DETERMINISTIC canonical walk: it emits prefixes in
 * one fixed order, so prefix i is well defined without knowing the total count.
 * A run computes only the prefixes in ITS slice; nothing is ever stored beyond
 * O(PREFIX_LENGTH) generator state.
 *
 *   Stripe mode  (SLICE_COUNT > 1): dispatch prefix i iff i % SLICE_COUNT == SLICE_ID.
 *                The union over SLICE_ID in [0, SLICE_COUNT) is the full search.
 *                Best load spread (canonically-adjacent heavy prefixes fall in
 *                different slices).
 *   Window mode  (SLICE_COUNT == 1): dispatch prefix i iff
 *                ROOT_OFFSET <= i < ROOT_OFFSET + ROOT_COUNT  (ROOT_COUNT <= 0 => to end).
 *
 * Defaults (SLICE_COUNT=1, ROOT_OFFSET=0, ROOT_COUNT=-1) => one slice = full search.
 */
#ifndef SLICE_COUNT
#define SLICE_COUNT 1
#endif
#ifndef SLICE_ID
#define SLICE_ID 0
#endif
#ifndef ROOT_OFFSET
#define ROOT_OFFSET 0
#endif
#ifndef ROOT_COUNT
#define ROOT_COUNT (-1)
#endif

/* ---- Knuth random-probing estimator (per prefix; optional) ---- */
#ifndef KNUTH_PROBES
#define KNUTH_PROBES 0
#endif
#ifndef PROBE_ONLY
#define PROBE_ONLY 0
#endif

/* ---- MPI message layout ---- */
#define TAG_REQUEST 1
#define TAG_TASK    2
#define TAG_STOP    3
#define TAG_RESULT  4

#define TASK_INTS   (PREFIX_LENGTH + 1)   /* [vertices..., transitionCounter] */
#define RESULT_DBLS 5                     /* [maxLength, count, search, runtime, knuth] */

#define BAR_WIDTH   40
#define CKPT_PERIOD 2.0

/* Deterministic slice membership test. The ordinal is 64-bit: at PL~18/N=8 the
 * prefix count exceeds 2^31, so a 32-bit counter would overflow. */
static inline bool inSlice(unsigned long long i)
{
#if SLICE_COUNT > 1
    return (i % (unsigned long long)SLICE_COUNT) == (unsigned long long)SLICE_ID;
#else
    if (i < (unsigned long long)ROOT_OFFSET) return false;
#if ROOT_COUNT > 0
    if (i >= (unsigned long long)ROOT_OFFSET + (unsigned long long)ROOT_COUNT) return false;
#endif
    return true;
#endif
}
