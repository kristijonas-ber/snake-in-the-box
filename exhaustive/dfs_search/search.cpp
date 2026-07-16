#include "search.hpp"
#include <cstdlib>
#include <cstring>

void Search::place(int vertex, int length)
{
    if (blocked[vertex] <= 1) availCount--;
    visited.set(vertex);
    snake[length] = vertex;
    for (int j = 0; j < N; j++)
    {
        int u = vertex ^ (1 << j);
        if (!visited.test(u) && blocked[u] == 1) availCount--;
        blocked[u]++;
        if (blocked[u] == 2) forbidden.set(u);
    }
}

void Search::unplace(int vertex)
{
    for (int j = 0; j < N; j++)
    {
        int u = vertex ^ (1 << j);
        if (blocked[u] == 2) forbidden.reset(u);
        blocked[u]--;
        if (!visited.test(u) && blocked[u] == 1) availCount++;
    }
    visited.reset(vertex);
    if (blocked[vertex] <= 1) availCount++;
}

void Search::emitSnake(int length)
{
    unsigned char buf[MAX_LENGTH];
    for (int i = 1; i < length; i++)
        buf[i - 1] = (unsigned char)__builtin_ctz(snake[i] ^ snake[i - 1]);
    fwrite(buf, 1, (size_t)(length - 1), outFile);
}

void Search::dfs(int vertex, int length, int transitionCounter)
{
    place(vertex, length);
    vertexCounter++;
    length++;

    if (length > maxLength)
    {
        maxLength = length;
        maxSnakeCounter = 0;
    }
    if (length == maxLength)
        maxSnakeCounter++;

    if (length == targetLength && outFile)
    {
        emitSnake(length);
        emitCount++;
    }

    int target = (targetLength > 0) ? targetLength : maxLength;
    if (length + availCount >= target)
    {
        for (int j = 0; j < N; j++)
        {
            int NextVertex = vertex ^ (1 << j);
            if (visited.test(NextVertex) || forbidden.test(NextVertex))
                continue;
            if (NextVertex >= (1 << transitionCounter))
            {
                if (NextVertex - vertex == (1 << transitionCounter))
                    transitionCounter++;
                else
                    continue;
            }
            dfs(NextVertex, length, transitionCounter);
            if (NextVertex - vertex == (1 << (transitionCounter - 1)))
                transitionCounter--;
        }
    }

    unplace(vertex);
}

// Search the subtree rooted at a prefix of `prefixLen` vertices (>= 1). Rebuilds
// all state from the prefix, then dives from its last vertex.
void Search::dfs_from_partial(const int *vertices, int prefixLen, int transitionCounter)
{
    visited.reset();
    forbidden.reset();
    memset(blocked, 0, sizeof(blocked));
    maxLength = 0;
    maxSnakeCounter = 0;
    emitCount = 0;
    vertexCounter = 0;

    for (int i = 0; i < prefixLen - 1; i++)
    {
        int v = vertices[i];
        snake[i] = v;
        visited.set(v);
        for (int j = 0; j < N; j++)
        {
            int u = v ^ (1 << j);
            blocked[u]++;
            if (blocked[u] == 2) forbidden.set(u);
        }
    }
    availCount = 0;
    for (int v = 0; v < VERTICES; v++)
        if (!visited.test(v) && blocked[v] <= 1) availCount++;

    int last = vertices[prefixLen - 1];
    dfs(last, prefixLen - 1, transitionCounter);
}

void Search::dfs_from_prefix(const int *vertices, int transitionCounter)
{
    dfs_from_partial(vertices, PREFIX_LENGTH, transitionCounter);
}

double Search::knuth_probe(int vertex, int length, int transitionCounter)
{
    visited.set(vertex);
    snake[length] = vertex;
    for (int j = 0; j < N; j++)
    {
        int u = vertex ^ (1 << j);
        blocked[u]++;
        if (blocked[u] == 2) forbidden.set(u);
    }
    length++;

    int kids[N], kidTC[N];
    int nkids = 0;
    for (int j = 0; j < N; j++)
    {
        int next = vertex ^ (1 << j);
        if (visited.test(next) || forbidden.test(next)) continue;
        int useTC = transitionCounter;
        if (next >= (1 << transitionCounter))
        {
            if (next - vertex == (1 << transitionCounter))
                useTC = transitionCounter + 1;
            else
                continue;
        }
        kids[nkids]  = next;
        kidTC[nkids] = useTC;
        nkids++;
    }

    double est;
    if (nkids == 0)
        est = 1.0;
    else
    {
        int pick = rand() % nkids;
        double sub = knuth_probe(kids[pick], length, kidTC[pick]);
        est = 1.0 + (double)nkids * sub;
    }

    for (int j = 0; j < N; j++)
    {
        int u = vertex ^ (1 << j);
        if (blocked[u] == 2) forbidden.reset(u);
        blocked[u]--;
    }
    visited.reset(vertex);
    return est;
}

double Search::knuth_estimate_prefix(const int *vertices, int transitionCounter, int probes)
{
    if (probes <= 0) return 0.0;

    visited.reset();
    forbidden.reset();
    memset(blocked, 0, sizeof(blocked));
    for (int i = 0; i < PREFIX_LENGTH - 1; i++)
    {
        int v = vertices[i];
        snake[i] = v;
        visited.set(v);
        for (int j = 0; j < N; j++)
        {
            int u = v ^ (1 << j);
            blocked[u]++;
            if (blocked[u] == 2) forbidden.set(u);
        }
    }
    int last = vertices[PREFIX_LENGTH - 1];

    double sum = 0.0;
    for (int p = 0; p < probes; p++)
        sum += knuth_probe(last, PREFIX_LENGTH - 1, transitionCounter);
    return sum / (double)probes;
}
