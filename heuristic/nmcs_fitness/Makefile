# Makefile for the fitness-weighted NMCS experiment.
#
# Independent sibling of ../pruned_bfs_search/, ../stochastic_prune/, and
# ../nmcs/ -- see nmcs_fitness_extend.c's top comment and
# ../../notes/search-strategy-ideas.md for why this exists as its own tool.
#
# Targets:
#   make       - build ./nmcs_fitness_extend
#   make clean - remove build artifacts

CC     = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra

OBJ    = hypercube.o transitions.o validation.o canonical.o \
         snake_node.o fitness.o snake_io.o nmcs_fitness_extend.o
TARGET = nmcs_fitness_extend
LIBS   = -lm

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
