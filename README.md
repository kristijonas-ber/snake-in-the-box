# Snake-in-the-Box

A repo for tackling the **Snake-in-the-Box (SIB) problem**: finding the longest *snake* (an induced path with no shortcuts) in an $n$-dimensional hypercube graph $Q_n$.

This repository contains two approaches to the problem:

## 1. Exact Search: [`exhaustive/`](exhaustive/)
A parallel exhaustive search MPI algorithm that the finds and enumerates all longest snakes for a given dimension.

* **Core Algorithm:** [`exhaustive/dfs_search/`](exhaustive/dfs_search/README.md) utilizes depth-first search, canonical-augmentation (a symmetry-reduction rule that only extends paths already in canonical form, skipping their symmetric duplicates), $O(1)$ array valid neighbor lookup, and deterministic prefix generator.
* **Outputs:** Raw results are stored in `exhaustive/job_outputs/`.
* **Feasibility:** Practical on a single machine up to about `N=6`; deeper dimensions need the MPI prefix-slice split across processes/machines described in the track README.

## 2. Approximate Search: [`heuristic/`](heuristic/)
A heuristic search algorithm that explores snakes with highest *fitness* until they cannot be extended.

* **Core Algorithm:** [`heuristic/pruned_bfs_search/`](heuristic/pruned_bfs_search/README.md) utilizes breadth-first search, *fitness* test, and snake validator.
* **Outputs:** Found snakes are stored in `heuristic/snakes/` and their transition sequences are stored in `heuristic/seeds/`.
* **Tools:** Includes `extend_snake` for seeding the beam into higher dimensions; `priming` for building snakes from the ground up.
* **Feasibility:** Scales to `N=15` and beyond in minutes (see the dimension-15 seeds in `heuristic/seeds/`) but only ever returns a lower bound, never a proof of the true maximum.

---


## Prerequisites & Building

Generally, you will need:

* **C Compiler** (`GCC`)
* **MPI Implementation** (`OpenMPI`)
* **Make**

Each algorithm builds independently. Navigate to any leaf folder and check its `README.md` for the exact build and run commands.


## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

This project builds upon the work of others. I would like to credit the following repositories:

* **Exhaustive Search:** The foundational approach for the exact search track was inspired by the algorithm developed by Ekaterina Simakova in [kat-devs/final-year-project](https://github.com/kat-devs/final-year-project) (no code from this repository is reused — the implementation here is original).
* **Heuristic Search:** The pruned-BFS beam search (`heuristic/pruned_bfs_search/`) is a direct C translation of the original Python `snake_in_box` package by Daniel Ari Friedman, available at [docxology/snake](https://github.com/docxology/snake) (MIT License) — see [LICENSE](LICENSE) for the full attribution notice.

Development of this repository was assisted by AI coding tools. All reported snakes are given as transition sequences that can be independently verified.


## Structure
```text
snake-in-the-box/
├── exhaustive/
│   ├── dfs_search/
│   └── job_outputs/
└── heuristic/
    ├── pruned_bfs_search/
    ├── seeds/
    └── snakes/
```