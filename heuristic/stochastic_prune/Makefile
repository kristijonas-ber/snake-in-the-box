# Makefile for the epsilon-random pruning experiment.
#
# Independent sibling of ../pruned_bfs_search/ -- see stochastic_extend.c's top
# comment and ../../notes/search-strategy-ideas.md for why this exists as its own
# tool rather than a change to the proven deterministic beam search.
#
# Targets:
#   make                    - build ./stochastic_extend
#   make clean              - remove build artifacts

CC     = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra

OBJ    = hypercube.o transitions.o validation.o canonical.o \
         snake_node.o fitness.o snake_io.o stochastic_extend.o
TARGET = stochastic_extend

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
