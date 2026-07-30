# Makefile for the multi-step lookahead fitness NMCS experiment.
#
# Independent sibling of ../pruned_bfs_search/, ../stochastic_prune/, ../nmcs/,
# and ../nmcs_fitness/ -- see nmcs_lookahead_extend.c's top comment and
# ../../notes/search-strategy-ideas.md for why this exists as its own tool.
#
# Targets:
#   make       - build ./nmcs_lookahead_extend
#   make clean - remove build artifacts

CC     = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra

OBJ    = hypercube.o transitions.o validation.o canonical.o \
         snake_node.o fitness.o snake_io.o nmcs_lookahead_extend.o
TARGET = nmcs_lookahead_extend
LIBS   = -lm

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
