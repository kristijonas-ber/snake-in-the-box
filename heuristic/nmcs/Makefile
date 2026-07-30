# Makefile for the Nested Monte Carlo Search experiment.
#
# Independent sibling of ../pruned_bfs_search/ and ../stochastic_prune/ -- see
# nmcs_extend.c's top comment and ../../notes/search-strategy-ideas.md for why
# this exists as its own tool.
#
# Targets:
#   make       - build ./nmcs_extend
#   make clean - remove build artifacts

CC     = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra

OBJ    = hypercube.o transitions.o validation.o canonical.o \
         snake_node.o fitness.o snake_io.o nmcs_extend.o
TARGET = nmcs_extend

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
