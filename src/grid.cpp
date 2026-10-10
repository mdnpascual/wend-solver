#include "grid.h"

bool Grid::in_bounds(int r, int c) const {
    return r >= 0 && r < GRID_SIZE && c >= 0 && c < GRID_SIZE;
}

int Grid::filled_count() const {
    int count = 0;
    for (int r = 0; r < GRID_SIZE; ++r)
        for (int c = 0; c < GRID_SIZE; ++c)
            if (cells[r][c].filled) ++count;
    return count;
}

std::vector<std::pair<int,int>> Grid::neighbors(int r, int c) const {
    static const int dr[] = {-1, 1,  0, 0};
    static const int dc[] = { 0, 0, -1, 1};
    std::vector<std::pair<int,int>> result;
    result.reserve(4);
    for (int i = 0; i < 4; ++i) {
        int nr = r + dr[i];
        int nc = c + dc[i];
        if (in_bounds(nr, nc) && cells[nr][nc].filled)
            result.emplace_back(nr, nc);
    }
    return result;
}

std::vector<std::pair<int,int>> Grid::filled_cells() const {
    std::vector<std::pair<int,int>> result;
    result.reserve(GRID_SIZE * GRID_SIZE);
    for (int r = 0; r < GRID_SIZE; ++r)
        for (int c = 0; c < GRID_SIZE; ++c)
            if (cells[r][c].filled)
                result.emplace_back(r, c);
    return result;
}

void Grid::clear() {
    for (int r = 0; r < GRID_SIZE; ++r)
        for (int c = 0; c < GRID_SIZE; ++c)
            cells[r][c] = Cell{};
}
