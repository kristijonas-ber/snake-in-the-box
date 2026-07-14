/* prefixgen.hpp — streaming canonical prefix generator.
 *
 * The one job of this module: walk the canonical-augmentation DFS to depth
 * PREFIX_LENGTH and hand each prefix, one at a time, to a callback — WITHOUT
 * ever storing a table of prefixes. Resident state is O(PREFIX_LENGTH): a
 * recursion of depth PREFIX_LENGTH (~18 frames) plus one 2^N-bit `visited`
 * bitset (32 bytes at N=8). "Traversing the whole tree" is a TIME cost; RAM is
 * flat no matter how many billions of prefixes exist. This is what removes
 * v13's array-in-RAM ceiling.
 *
 * The generator owns its OWN visited bitset + snake path (encapsulated, not the
 * file-static globals v13 shared between generation and search) — that
 * separation is exactly what lets it live in its own translation unit.
 */
#pragma once
#include "config.hpp"
#include <bitset>
#include <functional>

struct Prefix {
    int vertices[PREFIX_LENGTH];
    int transitionCounter;
};

class PrefixGen {
public:
    /* Called once per generated prefix, in canonical order:
     *   onLeaf(vertices[PREFIX_LENGTH], transitionCounter, ordinal)
     * Return true to keep walking, false to stop early. `ordinal` is the
     * absolute 0-based index of this prefix in the canonical order. */
    using LeafFn = std::function<bool(const int *, int, unsigned long long)>;

    /* Run one full deterministic walk, resetting state first. */
    void generate(const LeafFn &onLeaf);

    unsigned long long ordinal = 0;   /* # prefixes emitted so far this walk */

private:
    std::bitset<VERTICES> visited;
    int  snake[PREFIX_LENGTH];
    const LeafFn *cb   = nullptr;
    bool          done = false;
    void walk(int vertex, int length, int transitionCounter);
};
