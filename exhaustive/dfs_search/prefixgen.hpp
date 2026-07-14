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
    
    using LeafFn = std::function<bool(const int *, int, unsigned long long)>;

    void generate(const LeafFn &onLeaf);

    unsigned long long ordinal = 0;

private:
    std::bitset<VERTICES> visited;
    int  snake[PREFIX_LENGTH];
    const LeafFn *cb   = nullptr;
    bool          done = false;
    void walk(int vertex, int length, int transitionCounter);
};
