// Standalone solver smoke test — no UI, no GLFW needed.
// Loads the dictionary and runs the solver on the sample puzzle.
// Expected solutions include: DEN, SWEET, COBWEB, FENCING, FRAGRANT, BARGAINED
// (one continuous path split into those 6 words, lengths 3,5,6,7,8,9)

#include "../include/grid.h"
#include "../include/dictionary.h"
#include "../include/solver.h"

#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    const char* dict_path = argc > 1 ? argv[1] : "assets/words.txt";

    // Load dictionary
    Dictionary dict;
    int wc = dict.load(dict_path);
    if (wc < 0) { std::cerr << "Failed to load dictionary from: " << dict_path << "\n"; return 1; }
    std::cout << "Dictionary: " << wc << " words\n";

    // Build the sample grid
    // TWS....
    // EEINGRA
    // ABCDEFG
    // R.N.N.R
    // GFEDCBA
    // AINEOEN
    // ....BWT
    Grid grid;
    grid.clear();
    const char* rows[] = { "TWS....", "EEINGRA", "ABCDEFG", "R.N.N.R", "GFEDCBA", "AINEOEN", "....BWT" };
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 7; ++c)
            if (rows[r][c] != '.') {
                grid.cells[r][c].filled = true;
                grid.cells[r][c].letter = rows[r][c];
            }

    std::cout << "Filled cells: " << grid.filled_count() << "\n";

    // Config: word lengths 3,5,6,7,8,9 (sum=38)
    SolverConfig cfg;
    cfg.min_word_len = 3;
    cfg.max_word_len = 9;
    cfg.word_lengths = {3, 5, 6, 7, 8, 9};

    Solver solver(grid, cfg, dict);

    auto t0 = std::chrono::steady_clock::now();
    solver.start();

    // Poll until done
    int total_solutions = 0;
    while (solver.is_running()) {
        std::vector<Solution> batch;
        solver.results.drain(batch);
        for (auto& s : batch) {
            ++total_solutions;
            std::cout << "Solution #" << total_solutions << ": ";
            for (int i = 0; i < (int)s.words.size(); ++i) {
                if (i) std::cout << " | ";
                std::cout << s.words[i];
            }
            std::cout << "\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Drain any last results
    {
        std::vector<Solution> batch;
        solver.results.drain(batch);
        for (auto& s : batch) {
            ++total_solutions;
            std::cout << "Solution #" << total_solutions << ": ";
            for (int i = 0; i < (int)s.words.size(); ++i) {
                if (i) std::cout << " | ";
                std::cout << s.words[i];
            }
            std::cout << "\n";
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double,std::milli>(t1-t0).count();
    std::cout << "Done. " << total_solutions << " solution(s) in " << ms << " ms\n";
    return total_solutions > 0 ? 0 : 1;
}
