/* bitops.h - Portable bit-counting helpers (popcount / count-trailing-zeros).
 *
 * The rest of the code was written against the GCC/Clang __builtin_* intrinsics,
 * which MSVC (Visual Studio) does not provide. This header wraps the two
 * primitives the project needs behind 64-bit helpers that map to the right
 * compiler builtin:
 *
 *   sib_popcount64(x) - number of set bits in x
 *   sib_ctz64(x)      - index of the lowest set bit in x (x must be non-zero)
 *
 * On GCC/Clang these compile to __builtin_popcountll / __builtin_ctzll exactly as
 * before. On MSVC they use a software popcount (no POPCNT CPU-feature dependency)
 * and the _BitScanForward64 intrinsic. Using 64-bit types throughout also avoids
 * the silent truncation that a cast to `unsigned long` (32-bit on Windows/LLP64)
 * would cause for high dimensions.
 */
#ifndef BITOPS_H
#define BITOPS_H

#include <stdint.h>

#if defined(_MSC_VER)

#include <intrin.h>

static inline int sib_popcount64(uint64_t x)
{
    /* Software popcount: portable and free of the POPCNT-instruction
     * dependency that __popcnt64 carries. Perf is irrelevant at the
     * dimensions this project targets. */
    int count = 0;
    while (x) {
        x &= x - 1;
        count++;
    }
    return count;
}

static inline int sib_ctz64(uint64_t x)
{
    /* Callers only pass non-zero single-bit values, so the zero-mask case
     * (where _BitScanForward64 returns 0 and leaves index undefined) is not
     * reached. Requires an x64 build (the Visual Studio default). */
    unsigned long index;
    _BitScanForward64(&index, x);
    return (int)index;
}

#else /* GCC / Clang */

static inline int sib_popcount64(uint64_t x)
{
    return __builtin_popcountll(x);
}

static inline int sib_ctz64(uint64_t x)
{
    return __builtin_ctzll(x);
}

#endif

#endif /* BITOPS_H */
