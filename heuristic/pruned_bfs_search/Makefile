# Makefile for the snake-in-the-box C translation.
#
# Targets:
#   make               - build the demo executable ./snake_in_box
#   make run           - build and run with defaults (dimension 7)
#   make extend_snake  - build ./extend_snake (seed the beam with an existing snake)
#   make priming       - build ./priming (extend a seed one dimension at a time)
#   make parallel_search - build ./parallel_search (OpenMP per-level parallelism)
#   make parallel_extend - build ./parallel_extend (OpenMP seeded extension)
#   make clean         - remove build artifacts

CC       = cc
CFLAGS   = -std=c11 -O2 -Wall -Wextra
# OpenMP flag for the parallel_search tool. Override on platforms where the flag
# differs (e.g. macOS libomp: OMPFLAGS="-Xpreprocessor -fopenmp -lomp").
OMPFLAGS = -fopenmp
OBJ      = hypercube.o transitions.o validation.o canonical.o \
           snake_node.o fitness.o snake_io.o bfs_pruned.o main.o
TARGET   = snake_in_box

# Shared modules (no driver, no bfs_pruned.o) used by the seeded tools.
SHARED_OBJ = hypercube.o transitions.o validation.o canonical.o \
             snake_node.o fitness.o snake_io.o

EXT_OBJ = $(SHARED_OBJ) extend_snake.o
EXT_TARGET = extend_snake

PRIME_OBJ = $(SHARED_OBJ) priming.o
PRIME_TARGET = priming

# The parallel tools compile their driver with OpenMP; shared modules are plain.
PAR_TARGET = parallel_search
PAREXT_TARGET = parallel_extend

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

$(EXT_TARGET): $(EXT_OBJ)
	$(CC) $(CFLAGS) -o $@ $(EXT_OBJ)

$(PRIME_TARGET): $(PRIME_OBJ)
	$(CC) $(CFLAGS) -o $@ $(PRIME_OBJ)

$(PAR_TARGET): $(SHARED_OBJ) parallel_search.c
	$(CC) $(CFLAGS) $(OMPFLAGS) -o $@ $(SHARED_OBJ) parallel_search.c

$(PAREXT_TARGET): $(SHARED_OBJ) parallel_extend.c
	$(CC) $(CFLAGS) $(OMPFLAGS) -o $@ $(SHARED_OBJ) parallel_extend.c

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) 7

clean:
	rm -f $(OBJ) snake_io.o extend_snake.o priming.o parallel_search.o \
	      parallel_extend.o \
	      $(TARGET) $(EXT_TARGET) $(PRIME_TARGET) $(PAR_TARGET) $(PAREXT_TARGET)

.PHONY: all run clean
