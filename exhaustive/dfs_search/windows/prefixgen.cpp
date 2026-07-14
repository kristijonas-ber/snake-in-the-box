/* prefixgen.cpp — implementation of the streaming canonical prefix generator.
 *
 * `walk` is a faithful copy of v13's recursive prefixGeneration (same
 * TransitionCounter push/undo, same isAdjacentToAnyPrevious + visited pruning),
 * so it enumerates the identical canonical prefix set in the identical order.
 * The only change is the leaf action: instead of storing into a table, it
 * calls back with the prefix and its absolute ordinal.
 */
#include "prefixgen.hpp"

/* Slow O(L) adjacency scan — only used here (shallow walk, run once per pass).
 * v is adjacent to a snake vertex other than the head iff their XOR is a power
 * of two, which would break the induced-path (no-shortcut) rule. */
static bool isAdjacentToAnyPrevious(int vertex, int length, const int *sequence)
{
    for (int i = 0; i < length - 1; i++)
    {
        int x = vertex ^ sequence[i];
        if (x && !(x & (x - 1)))
            return true;
    }
    return false;
}

void PrefixGen::walk(int vertex, int length, int transitionCounter)
{
    if (done) return;

    snake[length] = vertex;
    visited.set(vertex);
    length++;

    if (length == PREFIX_LENGTH)
    {
        unsigned long long idx = ordinal++;
        if (inSlice(idx))
        {
            if (!(*cb)(snake, transitionCounter, idx))
                done = true;
        }
        visited.reset(vertex);
        return;
    }

    for (int j = 0; j < N && !done; j++)
    {
        int NextVertex = vertex ^ (1 << j);
        if (visited.test(NextVertex)) continue;
        if (isAdjacentToAnyPrevious(NextVertex, length, snake)) continue;
        if (NextVertex >= (1 << transitionCounter))
        {
            if (NextVertex - vertex == (1 << transitionCounter))
                transitionCounter++;
            else
                continue;
        }
        walk(NextVertex, length, transitionCounter);
        if (NextVertex - vertex == (1 << (transitionCounter - 1)))
            transitionCounter--;
    }
    visited.reset(vertex);
}

void PrefixGen::generate(const LeafFn &onLeaf)
{
    visited.reset();
    ordinal = 0;
    done    = false;
    cb      = &onLeaf;
    walk(0, 0, 0);
    cb = nullptr;
}
