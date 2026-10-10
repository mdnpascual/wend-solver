#include "solver.h"
#include <algorithm>
#include <cctype>
#include <numeric>

// ── Constructor ───────────────────────────────────────────────────────────────

Solver::Solver(const Grid& grid, const SolverConfig& config, const Dictionary& dict)
    : grid_(grid), config_(config), dict_(dict) {}

// ── Public API ────────────────────────────────────────────────────────────────

void Solver::start() {
    stop_flag_.store(false);
    running_.store(true);

    auto filled = grid_.filled_cells();
    int  total  = (int)filled.size();
    if (total == 0) { running_.store(false); return; }

    // Build the lengths list to place.
    // If word_lengths is specified, use it (sorted descending so we try long words first
    // — they prune harder and reduce branching).
    // If unconstrained, build all partitions lazily inside solve_words instead;
    // for the thread-split we still distribute starting cells.
    std::vector<int> lengths = config_.word_lengths;
    if (lengths.empty()) {
        // Unconstrained: we don't know lengths upfront; solve_words handles it.
        // Leave lengths empty — solve_words interprets empty as "free".
    } else {
        std::sort(lengths.begin(), lengths.end(), std::greater<int>());
    }

    // Split starting cells across threads.
    // Each worker tries all first-word paths that start from its assigned cells.
    int num_starts = total;
    int batch = (num_starts + NUM_THREADS - 1) / NUM_THREADS;

    workers_.reserve(NUM_THREADS);
    for (int t = 0; t < NUM_THREADS && t * batch < num_starts; ++t) {
        int from = t * batch;
        int to   = std::min(from + batch, num_starts);
        std::vector<std::pair<int,int>> slice(filled.begin() + from, filled.begin() + to);

        workers_.emplace_back([this, slice, lengths]() {
            worker_func(slice);
        });
    }

    std::thread([this]() {
        for (auto& w : workers_)
            if (w.joinable()) w.join();
        running_.store(false);
    }).detach();
}

void Solver::stop() {
    stop_flag_.store(true);
    for (auto& w : workers_)
        if (w.joinable()) w.join();
    workers_.clear();
    running_.store(false);
}

// ── Worker ────────────────────────────────────────────────────────────────────

void Solver::worker_func(const std::vector<std::pair<int,int>>& start_cells) {
    bool visited_global[GRID_SIZE][GRID_SIZE]{};
    std::vector<std::string>                     words_so_far;
    std::vector<std::vector<std::pair<int,int>>> paths_so_far;

    // Build lengths list (sorted descending).
    std::vector<int> lengths = config_.word_lengths;
    if (!lengths.empty())
        std::sort(lengths.begin(), lengths.end(), std::greater<int>());

    int total_cells = grid_.filled_count();

    for (auto& start : start_cells) {
        if (stop_flag_.load()) return;

        // For constrained mode: try starting a path of each possible first-word length.
        // For unconstrained: try all lengths in [min, max].
        std::vector<int> try_lengths;
        if (!lengths.empty()) {
            // Any length in the remaining set can be the first word.
            // Deduplicate to avoid redundant branches.
            std::vector<int> unique_lengths = lengths;
            std::sort(unique_lengths.begin(), unique_lengths.end(), std::greater<int>());
            unique_lengths.erase(std::unique(unique_lengths.begin(), unique_lengths.end()), unique_lengths.end());
            try_lengths = unique_lengths;
        } else {
            for (int l = config_.max_word_len; l >= config_.min_word_len; --l)
                try_lengths.push_back(l);
        }

        for (int target_len : try_lengths) {
            if (stop_flag_.load()) return;
            if (target_len > total_cells) continue;

            // Remove one instance of target_len from remaining_lengths.
            std::vector<int> next_lengths = lengths;
            if (!next_lengths.empty()) {
                auto it = std::find(next_lengths.begin(), next_lengths.end(), target_len);
                if (it == next_lengths.end()) continue; // this length not required
                next_lengths.erase(it);
            }

            // Check first char is a valid trie prefix.
            char first_ch = (char)std::tolower((unsigned char)grid_.cells[start.first][start.second].letter);
            if (!dict_.is_prefix(std::string_view(&first_ch, 1))) continue;

            bool visited_local[GRID_SIZE][GRID_SIZE]{};
            visited_global[start.first][start.second] = true;
            visited_local[start.first][start.second]  = true;

            std::vector<std::pair<int,int>> path;
            path.push_back(start);
            std::string cur_word(1, first_ch);

            int cells_after = total_cells - target_len;

            build_path(
                visited_global, visited_local,
                path, cur_word,
                target_len, cells_after,
                words_so_far, paths_so_far,
                next_lengths
            );

            visited_global[start.first][start.second] = false;
            visited_local[start.first][start.second]  = false;
        }
    }
}

// ── Path-level DFS ────────────────────────────────────────────────────────────

void Solver::build_path(
    bool visited_global[GRID_SIZE][GRID_SIZE],
    bool visited_local[GRID_SIZE][GRID_SIZE],
    std::vector<std::pair<int,int>>& path,
    std::string&                     cur_word,
    int                              target_len,
    int                              cells_remaining_after,
    std::vector<std::string>&                        words_so_far,
    std::vector<std::vector<std::pair<int,int>>>&    paths_so_far,
    std::vector<int>                                 remaining_lengths)
{
    if (stop_flag_.load()) return;

    // Reached target length — check if it's a valid word.
    if ((int)path.size() == target_len) {
        if (!dict_.is_word(cur_word)) return;

        // Commit this word and recurse to place the next.
        words_so_far.push_back(cur_word);
        paths_so_far.push_back(path);

        solve_words(
            visited_global,
            cells_remaining_after,
            words_so_far, paths_so_far,
            remaining_lengths
        );

        words_so_far.pop_back();
        paths_so_far.pop_back();
        return;
    }

    // Prune: if current prefix is not in trie, no word can extend from here.
    // (Already checked before appending each char, but double-check at start.)

    auto [r, c] = path.back();

    static const int dr[] = {-1, 1,  0, 0};
    static const int dc[] = { 0, 0, -1, 1};

    int needed = target_len - (int)path.size(); // cells still needed for this word

    for (int i = 0; i < 4; ++i) {
        if (stop_flag_.load()) return;

        int nr = r + dr[i];
        int nc = c + dc[i];

        if (!grid_.in_bounds(nr, nc))          continue;
        if (!grid_.cells[nr][nc].filled)       continue;
        if (visited_global[nr][nc])            continue;
        if (visited_local[nr][nc])             continue;

        char ch = (char)std::tolower((unsigned char)grid_.cells[nr][nc].letter);
        cur_word.push_back(ch);

        // Trie prefix pruning on the current word segment.
        if (!dict_.is_prefix(cur_word)) {
            cur_word.pop_back();
            continue;
        }

        visited_global[nr][nc] = true;
        visited_local[nr][nc]  = true;
        path.push_back({nr, nc});

        build_path(
            visited_global, visited_local,
            path, cur_word,
            target_len, cells_remaining_after,
            words_so_far, paths_so_far,
            remaining_lengths
        );

        path.pop_back();
        visited_global[nr][nc] = false;
        visited_local[nr][nc]  = false;
        cur_word.pop_back();
    }
}

// ── Word-level recursion ──────────────────────────────────────────────────────

void Solver::solve_words(
    bool visited_global[GRID_SIZE][GRID_SIZE],
    int  cells_remaining,
    std::vector<std::string>&                        words_so_far,
    std::vector<std::vector<std::pair<int,int>>>&    paths_so_far,
    std::vector<int>                                 remaining_lengths)
{
    if (stop_flag_.load()) return;

    // All cells placed — we have a complete solution.
    if (cells_remaining == 0) {
        // Constrained: must have used all required lengths.
        if (!remaining_lengths.empty()) return;

        Solution sol;
        sol.words = words_so_far;
        sol.paths = paths_so_far;
        results.push(std::move(sol));
        return;
    }

    // Unconstrained: if no more cells can form even the shortest word, bail.
    if (remaining_lengths.empty() && cells_remaining < config_.min_word_len) return;

    // Constrained: if remaining lengths don't add up to remaining cells, bail.
    if (!remaining_lengths.empty()) {
        int sum = 0;
        for (int l : remaining_lengths) sum += l;
        if (sum != cells_remaining) return;
    }

    // Find all unvisited cells to use as starting points for the next word.
    auto all_filled = grid_.filled_cells();

    // Determine which lengths to try for the next word.
    std::vector<int> try_lengths;
    if (!remaining_lengths.empty()) {
        // Deduplicate to avoid identical branches.
        std::vector<int> unique_rem = remaining_lengths;
        std::sort(unique_rem.begin(), unique_rem.end(), std::greater<int>());
        unique_rem.erase(std::unique(unique_rem.begin(), unique_rem.end()), unique_rem.end());
        try_lengths = unique_rem;
    } else {
        for (int l = config_.max_word_len; l >= config_.min_word_len; --l)
            try_lengths.push_back(l);
    }

    for (int target_len : try_lengths) {
        if (stop_flag_.load()) return;
        if (target_len > cells_remaining) continue;

        // Remove one instance of target_len from remaining_lengths for recursion.
        std::vector<int> next_lengths = remaining_lengths;
        if (!next_lengths.empty()) {
            auto it = std::find(next_lengths.begin(), next_lengths.end(), target_len);
            if (it == next_lengths.end()) continue;
            next_lengths.erase(it);
        }

        int cells_after = cells_remaining - target_len;

        // Try every unvisited cell as the start of this word.
        for (auto& start : all_filled) {
            if (stop_flag_.load()) return;
            if (visited_global[start.first][start.second]) continue;

            char first_ch = (char)std::tolower((unsigned char)grid_.cells[start.first][start.second].letter);
            if (!dict_.is_prefix(std::string_view(&first_ch, 1))) continue;

            bool visited_local[GRID_SIZE][GRID_SIZE]{};
            visited_global[start.first][start.second] = true;
            visited_local[start.first][start.second]  = true;

            std::vector<std::pair<int,int>> path;
            path.push_back(start);
            std::string cur_word(1, first_ch);

            build_path(
                visited_global, visited_local,
                path, cur_word,
                target_len, cells_after,
                words_so_far, paths_so_far,
                next_lengths
            );

            visited_global[start.first][start.second] = false;
        }
    }
}
