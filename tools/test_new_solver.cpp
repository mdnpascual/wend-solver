#include "../include/grid.h"
#include "../include/dictionary.h"
#include "../include/solver.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <algorithm>

void run(const char* name, Grid& g, SolverConfig& cfg, const Dictionary& dict, int max_sols=5) {
    std::cout << "\n=== " << name << " ===\n";
    std::cout << "Filled: " << g.filled_count() << "  ";
    if(!cfg.word_lengths.empty()){
        int s=0; for(auto l:cfg.word_lengths)s+=l;
        std::cout << "Hint sum=" << s;
    }
    std::cout << "\n";
    Solver solver(g, cfg, dict);
    solver.start();
    int found = 0;
    auto t0 = std::chrono::steady_clock::now();
    while(solver.is_running() || found < max_sols) {
        std::vector<Solution> b; solver.results.drain(b);
        for(auto& s : b) {
            ++found;
            std::cout << "  Sol #" << found << ": ";
            for(int i=0;i<(int)s.words.size();i++){
                if(i) std::cout << " + ";
                std::cout << s.words[i];
            }
            std::cout << "\n";
            if(found >= max_sols){ solver.stop(); break; }
        }
        if(!solver.is_running()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // drain remainder
    { std::vector<Solution> b; solver.results.drain(b);
      for(auto& s:b){ ++found; std::cout<<"  Sol #"<<found<<": ";
        for(int i=0;i<(int)s.words.size();i++){if(i)std::cout<<" + ";std::cout<<s.words[i];}
        std::cout<<"\n"; if(found>=max_sols)break; } }
    auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    std::cout << "  -> " << found << " solution(s) in " << ms << "ms\n";
}

int main(int argc, char* argv[]) {
    const char* dp = argc>1?argv[1]:"assets/words.txt";
    Dictionary dict; dict.load(dp);

    // ── Test 1: tiny known case ───────────────────────────────────────────────
    // Two independent words: CAT (row 0) and DOG (row 1), no shared cells needed.
    // Grid: C A T
    //       D O G
    {
        Grid g; g.clear();
        g.cells[0][0]={true,'C'}; g.cells[0][1]={true,'A'}; g.cells[0][2]={true,'T'};
        g.cells[1][0]={true,'D'}; g.cells[1][1]={true,'O'}; g.cells[1][2]={true,'G'};
        SolverConfig cfg; cfg.min_word_len=3; cfg.max_word_len=3; cfg.word_lengths={3,3};
        run("CAT+DOG (constrained 3,3)", g, cfg, dict);
    }

    // ── Test 2: same grid unconstrained ───────────────────────────────────────
    {
        Grid g; g.clear();
        g.cells[0][0]={true,'C'}; g.cells[0][1]={true,'A'}; g.cells[0][2]={true,'T'};
        g.cells[1][0]={true,'D'}; g.cells[1][1]={true,'O'}; g.cells[1][2]={true,'G'};
        SolverConfig cfg; cfg.min_word_len=3; cfg.max_word_len=6;
        run("CAT+DOG (unconstrained 3-6)", g, cfg, dict, 10);
    }

    // ── Test 3: ATHRHS/EREINE/F...KU/OJ...Q/RAMETI/ITYCHN ───────────────────
    // Expected: SHRINK(6) + FEATHER(7) + MAJORITY(8) + TECHNIQUE(9)
    {
        Grid g; g.clear();
        const char* rows[] = {"ATHRHS","EREINE","F...KU","OJ...Q","RAMETI","ITYCHN"};
        for(int r=0;r<6;r++)
            for(int c=0;c<(int)strlen(rows[r]);c++)
                if(rows[r][c]!='.') g.cells[r][c]={true,rows[r][c]};
        SolverConfig cfg; cfg.min_word_len=6; cfg.max_word_len=9; cfg.word_lengths={6,7,8,9};
        run("SHRINK+FEATHER+MAJORITY+TECHNIQUE (constrained)", g, cfg, dict);
    }

    // ── Test 4: same grid unconstrained ──────────────────────────────────────
    {
        Grid g; g.clear();
        const char* rows[] = {"ATHRHS","EREINE","F...KU","OJ...Q","RAMETI","ITYCHN"};
        for(int r=0;r<6;r++)
            for(int c=0;c<(int)strlen(rows[r]);c++)
                if(rows[r][c]!='.') g.cells[r][c]={true,rows[r][c]};
        SolverConfig cfg; cfg.min_word_len=6; cfg.max_word_len=9;
        run("SHRINK+FEATHER+MAJORITY+TECHNIQUE (unconstrained 6-9)", g, cfg, dict, 10);
    }

    return 0;
}
