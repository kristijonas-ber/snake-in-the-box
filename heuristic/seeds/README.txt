HOW TO ADD SNAKES TO EXTEND
===========================

Put ONE snake per file. Create as many files as you have snakes.

FORMAT of each snake file (e.g. seeds/my_snake.txt):
  - Space- or newline-separated TRANSITION integers.
  - A transition = the hypercube dimension (bit position) flipped at each step:
    for consecutive vertices v[i], v[i+1], the transition is log2(v[i] XOR v[i+1]).
  - This is exactly what the beam and the exhaustive solvers print after
    "Transitions:".
  - The file must contain ONLY integers — NO comments, NO other text, or it is
    rejected. (That's why these instructions live in this separate README, which
    is never passed to the tool.)
  - Every transition value must be < the target dimension you extend into.

If your source gives VERTEX numbers instead of transitions, convert each step to
log2(v[i] XOR v[i-1]). If it gives a hex string (papers often do, e.g. "0132..."),
each hex digit is one transition (0-9, a-f = 10-15) — expand to decimals here.

EXAMPLE: seeds/example_dim5_len13.txt contains a real length-13 snake in
dimension 5:
    0 1 2 3 0 2 4 0 1 2 0 3 1

RUN (from the pruned_bfs_c folder):
  make extend_snake
  ./extend_snake <target_dim> <mem_gb> [--both-ends] <seed files...>

  # extend several pasted snakes into dimension 8, growing both ends:
  ./extend_snake 8 18.0 --both-ends seeds/snake_a.txt seeds/snake_b.txt

  # or point at all .txt at once with a shell glob:
  ./extend_snake 8 18.0 --both-ends seeds/*.txt

  # .bin files from the exhaustive solvers work too (each holds many snakes):
  ./extend_snake 8 18.0 --both-ends ../../exhaustive/job_outputs/snakes_v8/7D_L51_rank*.bin

All supplied snakes are injected as seeds at once (multi-seed); the tool prints
the longest snake it manages to grow, with VALID/INVALID validation. Remember it
is a heuristic LOWER-BOUND tool — it cannot prove a maximum.
