# Spec: Wend Solver MVP

## Requirements

### R1 — Grid Input
- The UI displays a 15×15 grid of cells
- Each cell can be: **empty/hole** (blank), or contain a **single uppercase letter A–Z**
- Clicking an empty cell opens it for letter input; clicking a letter cell cycles to empty
- The effective puzzle is the bounding box of all filled cells (solver ignores outer empty rows/cols)

### R2 — Solver Controls
- **Min word length** text input (integer ≥ 1)
- **Max word length** text input (integer ≥ min)
- **Word lengths hint** text input — optional comma-separated integers whose sum must equal the total number of filled cells (e.g. `3,4,5` for 12 filled cells). If blank, solver tries all valid partitions.
- **Solve** button — starts the solver
- **Stop** button — signals all threads to halt

### R3 — Solver Algorithm
- Input: grid snapshot (copy), word-length constraints, trie
- For each filled cell as a potential path start:
  - DFS over orthogonal neighbors, never revisiting a cell
  - Build the letter string as the path grows
  - Prune immediately if current prefix is not in the trie
  - When all filled cells are visited, check if the complete string (or its word-split variants given the hint) consists entirely of valid dictionary words
  - If valid: post solution to the result queue
- Parallelism: distribute starting cells across 16 `std::thread` workers
- Each worker owns its own DFS stack (no shared mutable state during search)

### R4 — Results Display
- A scrollable panel lists solutions as they arrive
- Each solution shows: the path as a sequence of words, the grid coordinates of each letter
- Solutions are appended in real time without blocking the solver threads
- The UI thread polls the result queue each frame (mutex-locked, drain all pending)

### R5 — Dictionary
- Load `assets/words.txt` at startup into a trie
- Trie nodes store: children[26], is_terminal flag
- Prefix check: O(k) where k = prefix length
- Case-insensitive (normalize to lowercase internally)

### R6 — Word-length hint format (shown to user)
- Label near the input: `Lengths (e.g. 3,4,5 — must sum to total letters)`
- Validation: sum of parsed integers must equal count of filled cells; show error otherwise

## Data Structures

### Cell
```cpp
struct Cell {
    bool  filled;   // false = hole
    char  letter;   // 'A'–'Z' when filled
    int   row, col;
};
```

### Grid
```cpp
struct Grid {
    Cell cells[15][15];
    int  filled_count() const;
    bool in_bounds(int r, int c) const;
    std::vector<std::pair<int,int>> neighbors(int r, int c) const;
};
```

### TrieNode / Dictionary
```cpp
struct TrieNode {
    std::array<TrieNode*, 26> children{};
    bool is_terminal = false;
};
class Dictionary {
public:
    bool load(const std::string& path);
    bool is_prefix(std::string_view s) const;
    bool is_word(std::string_view s) const;
};
```

### Solution
```cpp
struct Solution {
    std::vector<std::pair<int,int>> path;   // grid coords in order
    std::vector<std::string>        words;  // word split
};
```

### SolverConfig
```cpp
struct SolverConfig {
    int min_word_len;
    int max_word_len;
    std::vector<int> word_lengths; // empty = unconstrained
};
```

## Threading Model
- Main thread: ImGui render loop, polls `result_queue` each frame
- Solver launch: divides filled cells into 16 buckets by starting cell index
- Each worker thread runs `solve_from(start_cell, grid_copy, config, trie_ref, &result_queue, &stop_flag)`
- `result_queue`: `std::queue<Solution>` protected by `std::mutex`
- `stop_flag`: `std::atomic<bool>` set by Stop button or when all threads finish
- Worker threads are joined when stop is requested or all complete

## Build
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```
Dependencies fetched automatically via FetchContent:
- glfw (3.4)
- dear imgui (v1.91 docking)
- glad (OpenGL 3.3 core)
