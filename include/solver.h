#pragma once
#include "grid.h"
#include "dictionary.h"

#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>

constexpr int NUM_THREADS = 16;

struct Solution {
    // Each element is one word's path (grid coords in visit order)
    std::vector<std::vector<std::pair<int,int>>> paths;
    std::vector<std::string> words;
};

struct SolverConfig {
    int              min_word_len = 2;
    int              max_word_len = 15;
    // If non-empty: exact word lengths that must be found (in any order).
    // Their sum must equal the total filled cell count.
    std::vector<int> word_lengths;
};

// Thread-safe result queue
struct ResultQueue {
    std::queue<Solution> queue;
    std::mutex           mtx;

    void push(Solution s) {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(std::move(s));
    }

    int drain(std::vector<Solution>& out) {
        std::lock_guard<std::mutex> lock(mtx);
        int count = 0;
        while (!queue.empty()) {
            out.push_back(std::move(queue.front()));
            queue.pop();
            ++count;
        }
        return count;
    }
};

class Solver {
public:
    Solver(const Grid& grid, const SolverConfig& config, const Dictionary& dict);

    void start();
    void stop();

    bool is_running() const { return running_.load(); }

    ResultQueue results;

private:
    Grid              grid_;
    SolverConfig      config_;
    const Dictionary& dict_;

    std::vector<std::thread> workers_;
    std::atomic<bool>        stop_flag_{false};
    std::atomic<bool>        running_{false};

    // Each worker gets a slice of starting cells for the FIRST word.
    void worker_func(const std::vector<std::pair<int,int>>& start_cells);

    // ── Word-level recursion ──────────────────────────────────────────────────
    // visited_global: cells already consumed by committed words.
    // words_so_far:   words committed so far.
    // paths_so_far:   their corresponding grid paths.
    // remaining_lengths: word lengths still to place (sorted descending for pruning).
    void solve_words(
        bool visited_global[GRID_SIZE][GRID_SIZE],
        int  cells_remaining,
        std::vector<std::string>&                        words_so_far,
        std::vector<std::vector<std::pair<int,int>>>&    paths_so_far,
        std::vector<int>                                 remaining_lengths
    );

    // ── Path-level DFS for one word ───────────────────────────────────────────
    // Builds a single path of exactly `target_len` cells through unvisited cells.
    // On completion (path.size() == target_len and is_word), calls solve_words recursively.
    void build_path(
        bool visited_global[GRID_SIZE][GRID_SIZE],
        bool visited_local[GRID_SIZE][GRID_SIZE],
        std::vector<std::pair<int,int>>& path,
        std::string&                     cur_word,
        int                              target_len,
        int                              cells_remaining_after,
        std::vector<std::string>&                        words_so_far,
        std::vector<std::vector<std::pair<int,int>>>&    paths_so_far,
        std::vector<int>                                 remaining_lengths
    );
};
