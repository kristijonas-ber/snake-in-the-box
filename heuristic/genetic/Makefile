# Makefile for the genetic-algorithm (bit-vector, real crossover) experiment.
#
# Independent sibling of ../pruned_bfs_search/, ../stochastic_prune/, ../nmcs/,
# ../nmcs_fitness/, and ../nmcs_lookahead/ -- see genetic_extend.c's top
# comment and ../../notes/search-strategy-ideas.md for why this exists as its
# own tool.
#
# Note this tool does NOT link snake_node.c/fitness.c -- its chromosome is a
# raw bit-vector, not a SnakeNode, so it only needs the transition/vertex
# conversion and validation utilities plus snake_io for saving results.
#
# Targets:
#   make       - build ./genetic_extend
#   make clean - remove build artifacts

CC     = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra

OBJ    = hypercube.o transitions.o validation.o canonical.o snake_io.o \
         genetic_extend.o
TARGET = genetic_extend

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
