#pragma once
#include "grid.h"
#include "solver.h"
#include "dictionary.h"
#include "lmstudio.h"
#include "clipboard_import.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

class UI {
public:
    UI(Dictionary& dict, LMStudio& lm, ClipboardImporter& importer);
    ~UI();

    // Render one ImGui frame. Call every frame from the main loop.
    void render();

private:
    Dictionary&        dict_;
    LMStudio&          lm_;
    ClipboardImporter& importer_;

    // Grid state
    Grid grid_;

    // Controls state
    char min_len_buf_[8]      = "2";
    char max_len_buf_[8]      = "15";
    char word_lens_buf_[256]  = "";  // e.g. "3,4,5"

    // Which cell is selected for letter input (-1 = none)
    int selected_row_ = -1;
    int selected_col_ = -1;

    // Process direct keyboard input while a cell is selected (called each frame)
    void handle_grid_keyboard();

    // Clipboard import: detect Ctrl+V and poll the importer each frame.
    void handle_clipboard_import();

    // True while an import is queued/processing — used to gate grid editing.
    bool import_busy() const;

    // Solver
    std::unique_ptr<Solver> solver_;
    std::vector<Solution>   solutions_;
    std::string             status_msg_;

    // ── Solution highlight ────────────────────────────────────────────────────
    // Index of the currently selected solution (-1 = none).
    int selected_solution_ = -1;

    // Set to true once when new solutions arrive — results panel steals focus once,
    // then clears so the user can freely interact with other panels.
    bool focus_results_once_ = false;

    // Per-cell word index for the selected solution (-1 = not part of solution).
    // Built by rebuild_cell_colors() whenever selected_solution_ changes.
    int cell_word_idx_[GRID_SIZE][GRID_SIZE]{};

    // Rebuild cell_word_idx_ from solutions_[selected_solution_].
    void rebuild_cell_colors();

    // Set selected_solution_ and rebuild colors.
    void select_solution(int idx);

    // Per-word colors (cycles for more than NUM_WORD_COLORS words).
    // Stored as uint32_t (IM_COL32 values) — defined in ui.cpp.
    static const int      NUM_WORD_COLORS = 8;
    static const uint32_t WORD_COLORS[NUM_WORD_COLORS];

    // Draw panels
    void draw_grid_panel();
    void draw_controls_panel();
    void draw_results_panel();

    // Poll for new solutions from the solver
    void poll_results();

    // Parse and validate config from UI buffers
    bool build_config(SolverConfig& out_config, std::string& err);
};
