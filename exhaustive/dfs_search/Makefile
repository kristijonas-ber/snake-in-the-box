# dfs_search — streaming, table-free deterministic prefix distribution.
#
# Multi-TU C++ build. The knob defines (N, PREFIX_LENGTH, SLICE_ID, ...) are
# passed to EVERY compile via DEFS so all translation units agree on the shared
# struct/array sizes. Override on the command line, e.g.:
#
#   make DEFS="-DN=8 -DPREFIX_LENGTH=18 -DSLICE_COUNT=64 -DSLICE_ID=0"
#
# then run one slice:   mpirun -n 5 ./dfs_search
# cover the whole search by running SLICE_ID = 0..SLICE_COUNT-1 (any machines).

CXX     = mpicxx
CXXFLAGS= -std=c++17 -O3 -Wall
DEFS    ?=

# Shared engine objects, plus one object per driver.
CORE       = prefixgen.o search.o
BIN        = dfs_search           # dispatcher build (rank 0 streams, workers search)
REPLAY_BIN = dfs_search_replay    # worker-side replay (dispatcher-free, parallel gen)

all: $(BIN) $(REPLAY_BIN)

$(BIN): $(CORE) driver_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(REPLAY_BIN): $(CORE) driver_replay.o
	$(CXX) $(CXXFLAGS) $^ -o $@

%.o: %.cpp config.hpp prefixgen.hpp search.hpp
	$(CXX) $(CXXFLAGS) $(DEFS) -c $< -o $@

clean:
	rm -f *.o $(BIN) $(REPLAY_BIN)

.PHONY: all clean
