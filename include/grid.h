#pragma once
#include <vector>
#include <utility>
#include <string>

constexpr int GRID_SIZE = 15;

struct Cell {
    bool filled  = false;
    char letter  = ' ';  // 'A'–'Z' when filled
};

struct Grid {
    Cell cells[GRID_SIZE][GRID_SIZE]{};

    // Number of filled (non-hole) cells
    int filled_count() const;

    // True if (r,c) is inside the 15x15 bounds
    bool in_bounds(int r, int c) const;

    // Orthogonal neighbors that are filled
    std::vector<std::pair<int,int>> neighbors(int r, int c) const;

    // All filled cell positions, in row-major order
    std::vector<std::pair<int,int>> filled_cells() const;

    // Clear all cells
    void clear();
};
