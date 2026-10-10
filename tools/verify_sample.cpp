// Quick standalone verifier — compile and run to check the solver logic
// against the known sample puzzle.
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

static const int GS = 7; // sample grid is 7x7
static const char* ROWS[7] = {
    "TWS....",
    "EEINGRA",
    "ABCDEFG",
    "R.N.N.R",
    "GFEDCBA",
    "AINEOEN",
    "....BWT"
};

char cell(int r, int c) {
    if (r < 0 || r >= GS || c < 0 || c >= GS) return '.';
    char ch = ROWS[r][c];
    return ch;
}

bool adj(int r1,int c1,int r2,int c2){return std::abs(r1-r2)+std::abs(c1-c2)==1;}

struct P { int r,c; };

void check_path(const char* name, std::vector<P> path) {
    std::string word;
    bool ok = true;
    for (int i = 0; i < (int)path.size(); i++) {
        char ch = cell(path[i].r, path[i].c);
        if (ch == '.') { std::cout << name << ": HOLE at ["<<path[i].r<<","<<path[i].c<<"]\n"; ok=false; }
        word += ch;
        if (i > 0 && !adj(path[i-1].r,path[i-1].c,path[i].r,path[i].c)) {
            std::cout << name << ": NOT ADJACENT at step "<<i<<" ["<<path[i-1].r<<","<<path[i-1].c<<"] -> ["<<path[i].r<<","<<path[i].c<<"]\n";
            ok = false;
        }
    }
    std::cout << name << ": \"" << word << "\" " << (ok?"OK":"FAIL") << "\n";
}

int main() {
    // Print the grid for reference
    std::cout << "Grid:\n";
    int total = 0;
    for (int r=0;r<GS;r++){
        for (int c=0;c<GS;c++){
            char ch=cell(r,c);
            std::cout << (ch=='.'?'.':ch);
            if(ch!='.') total++;
        }
        std::cout<<"\n";
    }
    std::cout << "Total filled: " << total << "\n\n";

    // Known solution words with their paths
    // DEN
    check_path("DEN",       {{4,3},{5,3},{5,2}});
    // SWEET: S(0,2)->W(0,1)->E(1,1)->E(1,0)->T(0,0)
    check_path("SWEET",     {{0,2},{0,1},{1,1},{1,0},{0,0}});
    // COBWEB: C(2,2)->O(5,4)->B(6,4)->W(6,5)->E(5,5)->B(4,5)
    check_path("COBWEB",    {{2,2},{5,4},{6,4},{6,5},{5,5},{4,5}});
    // FENCING: F(2,5)->E(2,4)->N(3,4)->C(4,4)->I(5,1)->N(5,2)->G(1,4) -- probably wrong, let me try
    // F(4,1)->E(4,2)->N(5,2)->C(4,4)? not adjacent...
    // Let's try: F(2,5)->E(1,5) -- E not at [1,5]? [1,5]=R. Hmm.
    // Grid row1: E E I N G R A  => [1,5]=R, [1,4]=G
    // F is at [2,5]. Neighbors of F(2,5): [1,5]=R, [3,5]=. (hole), [2,4]=E, [2,6]=G
    // FENCING: F-E-N-C-I-N-G
    // F(2,5)->E(2,4)->N(3,4)->C(4,4)->I(5,1)? not adjacent to C(4,4)
    // C(4,4) neighbors: D(4,3),B(4,5),E(5,4)? no [5,4]=O, [3,4]=N
    // So F(2,5)->E(2,4)->N(3,4)->C(4,4)->... I? [4,4] neighbors: D(4,3),B(4,5),N(3,4)visited,E? [5,4]=O
    // Hmm. Let me try F(4,1)->E(4,2)->N(3,2)->C(2,2)->I(5,1)?  C(2,2) neighbors: B(2,1),D(2,3),A(1,2)?[1,2]=I! 
    // [1,2]=I. F(4,1)->E(4,2)->N(3,2)->C(2,2)->I(1,2)->N(1,3)->G(1,4)
    check_path("FENCING",   {{4,1},{4,2},{3,2},{2,2},{1,2},{1,3},{1,4}});
    // FRAGRANT: F-R-A-G-R-A-N-T
    // F(2,5)->R(1,5)->A(1,6)->G(2,6)->R(3,6)->A(4,6)->N(5,6)->T(6,6)
    check_path("FRAGRANT",  {{2,5},{1,5},{1,6},{2,6},{3,6},{4,6},{5,6},{6,6}});
    // BARGAINED: B-A-R-G-A-I-N-E-D
    // B(2,1)->A(2,0)->R(3,0)->G(4,0)->A(5,0)->I(5,1)->N(5,2)->... wait I used N already for FENCING
    // Each word uses different cells. Let's check if sets overlap
    // FENCING uses: (4,1),(4,2),(3,2),(2,2),(1,2),(1,3),(1,4)
    // BARGAINED: B(2,1)->A(2,0)->R(3,0)->G(4,0)->A(5,0)->I(5,1)->N(5,2)->E(5,3)->D(4,3)
    // DEN used (4,3),(5,3),(5,2) -- N(5,2) and D(4,3) reused! Conflict.
    // So DEN must use different cells. D(2,3)->E(2,4)->N(1,3)?  
    // But (2,4) and (2,3): adj yes. (1,3) adj to (2,3)? |2-1|+|3-3|=1 yes. But N(1,3) 
    // and FENCING also uses (1,3). Conflict again.
    // Let me try DEN: D(4,3)->E(5,3)->N(5,2)? but then BARGAINED can't use those
    // The hint says path must be ONE continuous path using ALL letters.
    // The words are just how the single path is split — it's one path!
    std::cout << "\nNote: all words form ONE continuous path.\n";
    std::cout << "Let's try: DEN+SWEET+COBWEB+FENCING+FRAGRANT+BARGAINED as a single path:\n";
    // Full path attempt (concatenated):
    // BARGAINED first (start from B(2,1)):
    // B(2,1)->A(2,0)->R(3,0)->G(4,0)->A(5,0)->I(5,1)->N(5,2)->E(5,3)->D(4,3) = BARGAINED
    // then DEN continuing from D(4,3)? D already used. 
    // The words can be in any order along the path.
    // We need to find a Hamiltonian path where the letter sequence splits into those 6 words.
    std::cout << "This requires the solver to find it — just verifying the grid is correct.\n";

    return 0;
}
