# Wend Solver — Project Steering

## What is Wend?
Wend is a word puzzle. A grid (up to 15×15) has individual letters placed on cells; some cells are empty (holes). The solver must find a **set of independent word paths** that:
- Together visit **every filled cell exactly once** (partition the grid)
- Each word is its own **independent connected path** (orthogonal neighbors, no revisiting within a word)
- Paths from different words do **not** need to be connected to each other
- Each path's letter sequence must be a **valid English word**

The user can provide the expected word-length breakdown (e.g. "3,5" meaning a 3-letter word and a 5-letter word) to constrain the search. Without it the solver tries all splits within min/max bounds.

## Project Goals (MVP — complete)
- C++ desktop application
- Dear ImGui + GLFW/OpenGL UI
- 15×15 interactive grid: click to select cell, type letter, arrow keys to navigate, Del to clear, Esc to deselect
- Min/max word-length text inputs
- Word-length hint text input (format: comma-separated lengths, e.g. `3,4,5`)
- Brute-force multithreaded solver (16 threads)
- Solutions streamed to UI as soon as found (non-blocking display)
- Duplicate solutions (same word set, different order) filtered out at display time
- Selecting a solution colors each word's cells a distinct color on the grid
- Up/Down arrows in the Results panel cycles the selected solution and recolors the grid
- Standard English word list bundled as `assets/words.txt`

## Architecture
```
src/
  main.cpp          — entry point, GLFW/ImGui init, main loop
  grid.cpp/.h       — Grid model: cells, adjacency
  solver.cpp/.h     — Multithreaded DFS solver, solution queue
  dictionary.cpp/.h — Word list loader and trie lookup
  ui.cpp/.h         — ImGui panels: grid editor, controls, results
assets/
  words.txt         — Standard English word list (one word per line)
third_party/        CMake FetchContent targets (ImGui, GLFW, glad) + english-words submodule
CMakeLists.txt
BUILD.md            — Build & run instructions
```

## Solver Design
- **Word-level outer recursion** (`solve_words`): picks the next word to place, tries every unvisited cell as its start, removes used length from the remaining set, recurses.
- **Path-level inner DFS** (`build_path`): grows one word's path through unvisited cells, pruning via trie prefix check on each step.
- **Threading**: 16 workers each own a slice of starting cells for the first word; no shared mutable state during search.
- **Result queue**: `std::mutex`-protected, drained by UI thread every frame.
- **Stop signal**: `std::atomic<bool>` checked at every DFS node.
- **Deduplication**: solutions with identical word sets (regardless of order) are dropped at the UI poll stage.

## Build System
- CMake 3.20+
- C++17
- FetchContent for Dear ImGui (v1.91.6-docking), GLFW (3.4), glad (OpenGL 3.3 core)
- Target: `wend_solver` executable
- CMake bundled with VS2022 — see BUILD.md for exact commands

## Coding Conventions
- snake_case for variables and functions
- PascalCase for structs/classes
- Solver state is immutable once solve starts (grid copied into Solver)
- Solutions posted to `std::mutex`-protected `ResultQueue`, drained by UI thread
- `std::atomic<bool>` for stop/pause signals
- `std::thread` preferred over platform APIs
- No exceptions in hot paths; use return values

## Word List
- Source: dwyl/english-words `words_alpha.txt` (~370k words, no proper nouns, letters only), vendored as a git submodule at `third_party/english-words`
- Not committed directly; CMake copies `words_alpha.txt` -> `assets/words.txt` next to the exe at build time
- Loaded into a **trie** for O(k) prefix and exact-match checks
- Prefix pruning: abort any path whose current prefix has no continuations in the trie

## UI Conventions
- Three fixed panels: Grid (left, 700px), Controls (middle, 220px), Results (right, remainder)
- Grid cell selection: click to select, type to assign letter, arrows to move, Del/Backspace to clear, Esc to deselect
- Results panel: `Selectable` rows, Up/Down to navigate, colored word swatches per row
- Word colors: 8-color palette (red, blue, amber, green, purple, teal, orange, yellow), cycles if >8 words
- Focus management: Results panel steals focus once when first solution arrives; after that focus is free

## Future Features
See `.kiro/specs/` for detailed specs.

### Clipboard screenshot import (planned)
- Trigger: **Ctrl+V** anywhere in the app
- Implementation: read clipboard image via Win32 API (or GLFW clipboard), decode with **OpenCV**, parse the grid layout using image processing (cell detection, letter OCR or color segmentation)
- Dependency: OpenCV (add via CMake FetchContent or vcpkg)
- New source files: `src/clipboard_import.cpp/.h`
- On success: populate the Grid model and update the UI grid display
- On failure: show an error in the status bar
- See `.kiro/specs/clipboard-import.md` for full design
