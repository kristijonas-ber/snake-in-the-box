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

CXX      = mpicxx
CXX_PLAIN = c++                    # generator is MPI-free: build it without mpicxx
CXXFLAGS = -std=c++17 -O3 -Wall
DEFS    ?=

# Shared engine objects, plus one object per driver.
CORE       = prefixgen.o search.o
BIN        = dfs_search           # dispatcher build (rank 0 streams, workers search)
REPLAY_BIN = dfs_search_replay    # worker-side replay (dispatcher-free, parallel gen)
GEN_BIN    = prefixgen_tool       # standalone generator: prefixes -> .pfx files (no MPI)
FILES_BIN  = dfs_from_files       # MPI DFS over prefixes read from .pfx batch files
SUBTREE_BIN = subtree_search      # exhaustive search of one subtree from a prefix (no MPI)

all: $(BIN) $(REPLAY_BIN) $(GEN_BIN) $(FILES_BIN) $(SUBTREE_BIN)

$(BIN): $(CORE) driver_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(REPLAY_BIN): $(CORE) driver_replay.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Generator links only the prefix walker; no search engine, no MPI at all, so it
# builds and runs on a machine without an MPI install.
$(GEN_BIN): prefixgen.o driver_prefixgen.o
	$(CXX_PLAIN) $(CXXFLAGS) $^ -o $@

$(FILES_BIN): $(CORE) driver_fromfiles.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# Subtree search needs only the engine; no MPI.
$(SUBTREE_BIN): search.o driver_subtree.o
	$(CXX_PLAIN) $(CXXFLAGS) $^ -o $@

# The MPI-free drivers are compiled with the plain compiler too.
driver_prefixgen.o: driver_prefixgen.cpp config.hpp prefixgen.hpp pfxio.hpp
	$(CXX_PLAIN) $(CXXFLAGS) $(DEFS) -c $< -o $@

driver_subtree.o: driver_subtree.cpp config.hpp search.hpp
	$(CXX_PLAIN) $(CXXFLAGS) $(DEFS) -c $< -o $@

%.o: %.cpp config.hpp prefixgen.hpp search.hpp pfxio.hpp
	$(CXX) $(CXXFLAGS) $(DEFS) -c $< -o $@

clean:
	rm -f *.o $(BIN) $(REPLAY_BIN) $(GEN_BIN) $(FILES_BIN) $(SUBTREE_BIN)

.PHONY: all clean
