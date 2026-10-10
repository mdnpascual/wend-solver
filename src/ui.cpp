#include "ui.h"
#include "imgui.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <string>

// ── Word color palette ────────────────────────────────────────────────────────
const uint32_t UI::WORD_COLORS[UI::NUM_WORD_COLORS] = {
    IM_COL32(220,  80,  80, 255),   // 0 red
    IM_COL32( 60, 160, 230, 255),   // 1 blue
    IM_COL32(210, 160,  30, 255),   // 2 amber
    IM_COL32(100, 200,  90, 255),   // 3 green
    IM_COL32(180,  80, 220, 255),   // 4 purple
    IM_COL32( 50, 200, 180, 255),   // 5 teal
    IM_COL32(230, 130,  50, 255),   // 6 orange
    IM_COL32(210, 210,  60, 255),   // 7 yellow
};

// ── Constructor / Destructor ──────────────────────────────────────────────────

UI::UI(Dictionary& dict, LMStudio& lm, ClipboardImporter& importer)
    : dict_(dict), lm_(lm), importer_(importer) {
    grid_.clear();
}

UI::~UI() {
    if (solver_ && solver_->is_running())
        solver_->stop();
}

// ── Solution highlight helpers ────────────────────────────────────────────────

void UI::select_solution(int idx) {
    selected_solution_ = idx;
    rebuild_cell_colors();
}

void UI::rebuild_cell_colors() {
    // Reset all to -1 (no highlight)
    for (int r = 0; r < GRID_SIZE; ++r)
        for (int c = 0; c < GRID_SIZE; ++c)
            cell_word_idx_[r][c] = -1;

    if (selected_solution_ < 0 || selected_solution_ >= (int)solutions_.size())
        return;

    const Solution& sol = solutions_[selected_solution_];
    for (int wi = 0; wi < (int)sol.paths.size(); ++wi)
        for (auto [r, c] : sol.paths[wi])
            cell_word_idx_[r][c] = wi;
}

// ── Main render ───────────────────────────────────────────────────────────────

void UI::render() {
    poll_results();
    handle_clipboard_import();

    ImGuiIO& io = ImGui::GetIO();

    // Three-column layout — standalone top-level windows, no host wrapper.
    // NoNav flags kept off so keyboard input routes correctly.
    const float left_w   = 1400.0f;
    const float middle_w = 440.0f;
    const float right_w  = io.DisplaySize.x - left_w - middle_w;

    constexpr ImGuiWindowFlags panel_flags =
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar;

    // ── Grid panel ────────────────────────────────────────────────────────────
    // If a cell is selected, keep keyboard focus on the grid window so that
    // typed characters and arrow keys are not consumed by other widgets.
    if (selected_row_ >= 0 && selected_col_ >= 0)
        ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(left_w, io.DisplaySize.y), ImGuiCond_Always);
    ImGui::Begin("Grid", nullptr, panel_flags);
    draw_grid_panel();
    ImGui::End();

    // ── Controls panel ────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(left_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(middle_w, io.DisplaySize.y), ImGuiCond_Always);
    ImGui::Begin("Controls", nullptr, panel_flags);
    draw_controls_panel();
    ImGui::End();

    // ── Results panel ─────────────────────────────────────────────────────────
    // Steal focus once when new solutions arrive so Up/Down works immediately.
    // After that single frame, never force focus — let the user click freely.
    if (focus_results_once_) {
        ImGui::SetNextWindowFocus();
        focus_results_once_ = false;
    }
    ImGui::SetNextWindowPos(ImVec2(left_w + middle_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(right_w, io.DisplaySize.y), ImGuiCond_Always);
    ImGui::Begin("Results", nullptr, panel_flags);
    draw_results_panel();
    ImGui::End();
}

// ── Clipboard import (Ctrl+V) ─────────────────────────────────────────────────

bool UI::import_busy() const {
    return importer_.busy();
}

void UI::handle_clipboard_import() {
    ImGuiIO& io = ImGui::GetIO();

    // Detect Ctrl+V, but only when no text-input widget is capturing keys
    // (so pasting into the length/hint fields still works normally).
    if (!io.WantTextInput && io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_V, /*repeat=*/false)) {
        if (!importer_.busy()) {
            std::string err;
            if (!importer_.begin_from_clipboard(err)) {
                status_msg_ = "Import failed: " + err;
            } else {
                // Deselect the grid so no stray typing lands mid-import.
                selected_row_ = -1;
                selected_col_ = -1;
                status_msg_.clear();
            }
        }
    }

    // Poll the importer for a completed result.
    if (importer_.status() == ClipboardImporter::Status::Done) {
        // Capture the importer's status message before take_result() resets it
        // — it carries any low-confidence warnings from reconciliation.
        std::string import_msg = importer_.status_message();
        Grid loaded;
        std::vector<int> word_lengths;
        bool lengths_confident = false;
        if (importer_.take_result(loaded, word_lengths, lengths_confident)) {
            grid_ = loaded;
            // Fresh board: clear any prior solve state. Do NOT auto-solve.
            if (solver_ && solver_->is_running()) solver_->stop();
            solutions_.clear();
            select_solution(-1);
            focus_results_once_ = false;
            selected_row_ = -1;
            selected_col_ = -1;

            // Populate the word-length hint + min/max fields from the detected
            // hint squares, if any were found.
            bool lengths_uncertain = false;
            if (!word_lengths.empty()) {
                std::string csv;
                int lo = word_lengths[0], hi = word_lengths[0];
                for (size_t i = 0; i < word_lengths.size(); ++i) {
                    if (i) csv += ',';
                    csv += std::to_string(word_lengths[i]);
                    lo = std::min(lo, word_lengths[i]);
                    hi = std::max(hi, word_lengths[i]);
                }
                std::snprintf(word_lens_buf_, sizeof(word_lens_buf_), "%s", csv.c_str());
                std::snprintf(min_len_buf_, sizeof(min_len_buf_), "%d", lo);
                std::snprintf(max_len_buf_, sizeof(max_len_buf_), "%d", hi);
                lengths_uncertain = !lengths_confident;
            }

            // If reconciliation flagged uncertain cells, keep that detailed
            // message; otherwise use the standard review prompt.
            if (import_msg.find("uncertain") != std::string::npos) {
                status_msg_ = import_msg;
            } else if (lengths_uncertain) {
                status_msg_ = "Grid loaded. Word lengths detected but uncertain "
                              "(they don't sum to the filled cells) — please verify.";
            } else {
                status_msg_ = "Grid loaded from clipboard. Review, then press Solve.";
            }
        }
    }
}

// ── Keyboard handling for grid (called each frame from render) ────────────────

void UI::handle_grid_keyboard() {
    if (selected_row_ < 0 || selected_col_ < 0) return;
    if (import_busy()) return;  // grid editing disabled during import

    ImGuiIO& io = ImGui::GetIO();

    // ── Arrow key navigation ──────────────────────────────────────────────────
    // Use ImGui key repeat so holding an arrow key scrolls through cells.
    struct { ImGuiKey key; int dr; int dc; } dirs[] = {
        { ImGuiKey_UpArrow,    -1,  0 },
        { ImGuiKey_DownArrow,  +1,  0 },
        { ImGuiKey_LeftArrow,   0, -1 },
        { ImGuiKey_RightArrow,  0, +1 },
    };
    for (auto& d : dirs) {
        if (ImGui::IsKeyPressed(d.key, /*repeat=*/true)) {
            int nr = selected_row_ + d.dr;
            int nc = selected_col_ + d.dc;
            if (nr >= 0 && nr < GRID_SIZE && nc >= 0 && nc < GRID_SIZE) {
                selected_row_ = nr;
                selected_col_ = nc;
            }
        }
    }

    // ── Letter input from character queue ────────────────────────────────────
    // ImGui collects typed characters in InputQueueCharacters each frame.
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        ImWchar c = io.InputQueueCharacters[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A'; // normalise to upper
        if (c >= 'A' && c <= 'Z') {
            grid_.cells[selected_row_][selected_col_].filled = true;
            grid_.cells[selected_row_][selected_col_].letter = (char)c;
        }
    }

    // ── Delete / Backspace clears the cell ───────────────────────────────────
    if (ImGui::IsKeyPressed(ImGuiKey_Delete,    /*repeat=*/false) ||
        ImGui::IsKeyPressed(ImGuiKey_Backspace,  /*repeat=*/false))
    {
        grid_.cells[selected_row_][selected_col_] = Cell{};
    }

    // ── Escape deselects ─────────────────────────────────────────────────────
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, /*repeat=*/false)) {
        selected_row_ = -1;
        selected_col_ = -1;
    }
}

// ── Grid Panel ────────────────────────────────────────────────────────────────

void UI::draw_grid_panel() {
    // Process keyboard input first, before any ImGui widget can steal it
    handle_grid_keyboard();

    const bool busy = import_busy();

    // ── Clipboard import status banner ────────────────────────────────────────
    // Vision model readiness (left) and import progress (right).
    {
        LMStudio::State ls = lm_.state();
        ImVec4 lcol = (ls == LMStudio::State::Ready)   ? ImVec4(0.3f,1.0f,0.5f,1.0f)
                    : (ls == LMStudio::State::Error)   ? ImVec4(1.0f,0.4f,0.4f,1.0f)
                    :                                    ImVec4(1.0f,0.85f,0.3f,1.0f);
        const char* lname = (ls == LMStudio::State::Ready) ? "Ready"
                          : (ls == LMStudio::State::Error) ? "Error"
                          : (ls == LMStudio::State::Starting) ? "Starting..."
                          :                                    "Idle";
        ImGui::TextUnformatted("Vision model:");
        ImGui::SameLine();
        ImGui::TextColored(lcol, "%s", lname);
        if (!lm_.status_message().empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", lm_.status_message().c_str());
        }

        ClipboardImporter::Status is = importer_.status();
        if (is == ClipboardImporter::Status::Queued ||
            is == ClipboardImporter::Status::Processing) {
            ImGui::TextColored(ImVec4(1.0f,0.85f,0.3f,1.0f), "%s",
                importer_.status_message().c_str());
        } else if (is == ClipboardImporter::Status::Failed) {
            ImGui::TextColored(ImVec4(1.0f,0.4f,0.4f,1.0f), "Import error: %s",
                importer_.error().c_str());
        }
    }

    ImGui::Separator();

    // Header hint
    if (selected_row_ >= 0 && selected_col_ >= 0) {
        const Cell& sel = grid_.cells[selected_row_][selected_col_];
        if (sel.filled)
            ImGui::Text("Selected [%d,%d]: %c  |  Type to replace  |  Del to clear  |  Arrows to move  |  Esc to deselect",
                selected_row_, selected_col_, sel.letter);
        else
            ImGui::Text("Selected [%d,%d]: (empty)  |  Type a letter  |  Arrows to move  |  Esc to deselect",
                selected_row_, selected_col_);
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Draw the grid
    const float cell_size = 76.0f;
    const float pad       = 4.0f;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2      origin    = ImGui::GetCursorScreenPos();

    for (int r = 0; r < GRID_SIZE; ++r) {
        for (int c = 0; c < GRID_SIZE; ++c) {
            ImVec2 tl = ImVec2(
                origin.x + c * (cell_size + pad),
                origin.y + r * (cell_size + pad));
            ImVec2 br = ImVec2(tl.x + cell_size, tl.y + cell_size);

            bool is_selected = (r == selected_row_ && c == selected_col_);
            const Cell& cell = grid_.cells[r][c];

            // Background color
            ImU32 bg_col;
            if (is_selected)
                bg_col = IM_COL32(80, 140, 220, 255);
            else if (cell_word_idx_[r][c] >= 0)
                bg_col = (ImU32)WORD_COLORS[cell_word_idx_[r][c] % NUM_WORD_COLORS];
            else if (cell.filled)
                bg_col = IM_COL32(60, 170, 90, 255);
            else
                bg_col = IM_COL32(45, 45, 48, 255);

            // Dim the whole grid while an import is in progress.
            int alpha = busy ? 110 : 255;
            if (busy) {
                // Rebuild the color with reduced alpha.
                bg_col = (bg_col & 0x00FFFFFF) | ((ImU32)alpha << 24);
            }

            draw_list->AddRectFilled(tl, br, bg_col, 4.0f);
            draw_list->AddRect(tl, br, IM_COL32(100, 100, 100, busy ? 120 : 200), 4.0f, 0, 1.5f);

            // Letter
            if (cell.filled) {
                char text[2] = {cell.letter, '\0'};
                // Draw the glyph large so it fills most of the cell,
                // leaving only minimal padding.
                ImFont* font = ImGui::GetFont();
                float glyph_size = cell_size * 0.9f;
                ImVec2 text_size = font->CalcTextSizeA(glyph_size, FLT_MAX, 0.0f, text);
                ImVec2 text_pos = ImVec2(
                    tl.x + (cell_size - text_size.x) * 0.5f,
                    tl.y + (cell_size - text_size.y) * 0.5f);
                draw_list->AddText(font, glyph_size, text_pos,
                    IM_COL32(255, 255, 255, busy ? 130 : 255), text);
            }

            // Invisible button for click detection — disabled during import.
            ImGui::SetCursorScreenPos(tl);
            ImGui::PushID(r * GRID_SIZE + c);
            if (busy) {
                // Consume the space without making it clickable.
                ImGui::Dummy(ImVec2(cell_size, cell_size));
            } else if (ImGui::InvisibleButton("##cell", ImVec2(cell_size, cell_size))) {
                if (is_selected) {
                    // Second click on the same cell → deselect
                    selected_row_ = -1;
                    selected_col_ = -1;
                } else {
                    selected_row_ = r;
                    selected_col_ = c;
                }
            }
            ImGui::PopID();
        }
    }

    // Advance cursor past the grid
    ImGui::SetCursorScreenPos(ImVec2(
        origin.x,
        origin.y + GRID_SIZE * (cell_size + pad) + 8.0f));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Filled cells: %d", grid_.filled_count());
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Clear All")) {
        grid_.clear();
        selected_row_ = -1;
        selected_col_ = -1;
    }
    ImGui::EndDisabled();
}

// ── Controls Panel ────────────────────────────────────────────────────────────

void UI::draw_controls_panel() {
    ImGui::TextUnformatted("Solver Settings");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-1);
    ImGui::LabelText("##ml_label", "Min word length");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##min_len", min_len_buf_, sizeof(min_len_buf_),
        ImGuiInputTextFlags_CharsDecimal);

    ImGui::Spacing();
    ImGui::LabelText("##mxl_label", "Max word length");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##max_len", max_len_buf_, sizeof(max_len_buf_),
        ImGuiInputTextFlags_CharsDecimal);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Word lengths hint\n(e.g. 3,4,5)");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##word_lens", word_lens_buf_, sizeof(word_lens_buf_));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    bool is_running = solver_ && solver_->is_running();
    bool busy = importer_.busy();

    if (!is_running) {
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Solve", ImVec2(-1, 40))) {
            SolverConfig cfg;
            std::string err;
            if (build_config(cfg, err)) {
                status_msg_ = "Solving...";
                solutions_.clear();
                select_solution(-1);
                focus_results_once_ = false;
                solver_ = std::make_unique<Solver>(grid_, cfg, dict_);
                solver_->start();
            } else {
                status_msg_ = "Error: " + err;
            }
        }
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Stop", ImVec2(-1, 40))) {
            solver_->stop();
            status_msg_ = "Stopped.";
        }
    }

    ImGui::Spacing();
    if (!status_msg_.empty()) {
        if (!is_running && solver_)
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.3f, 1.0f), "%s", status_msg_.c_str());
        else
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.5f, 1.0f), "%s", status_msg_.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Dictionary: %s", dict_.is_loaded() ? "Loaded" : "NOT LOADED");
    if (dict_.is_loaded())
        ImGui::Text("%d words", dict_.word_count());

    ImGui::Spacing();
    ImGui::Text("Solutions found: %d", (int)solutions_.size());

    // Update status when solver finishes on its own
    if (is_running == false && solver_ && status_msg_ == "Solving...") {
        status_msg_ = "Done. " + std::to_string(solutions_.size()) + " solution(s) found.";
    }
}

// ── Results Panel ─────────────────────────────────────────────────────────────

void UI::draw_results_panel() {
    ImGui::TextUnformatted("Solutions");
    ImGui::Separator();

    if (solutions_.empty()) {
        ImGui::TextDisabled("No solutions yet.");
        return;
    }

    ImGui::TextDisabled("Up/Down to select  |  %d solution(s)", (int)solutions_.size());
    ImGui::Spacing();

    // ── Keyboard navigation (Up/Down) when this window is focused ────────────
    // Only handle if no grid cell is selected (so grid arrows still work normally)
    if (selected_row_ < 0 && selected_col_ < 0) {
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, /*repeat=*/true)) {
                int next = (selected_solution_ < 0) ? 0
                         : std::min(selected_solution_ + 1, (int)solutions_.size() - 1);
                if (next != selected_solution_) select_solution(next);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, /*repeat=*/true)) {
                int next = (selected_solution_ < 0) ? 0
                         : std::max(selected_solution_ - 1, 0);
                if (next != selected_solution_) select_solution(next);
            }
        }
    }

    // ── Scrollable list ───────────────────────────────────────────────────────
    ImGui::BeginChild("##results_scroll", ImVec2(0, 0), false,
        ImGuiWindowFlags_HorizontalScrollbar);

    for (int i = 0; i < (int)solutions_.size(); ++i) {
        const Solution& sol = solutions_[i];

        bool is_sel = (i == selected_solution_);

        // Highlight the selected solution row
        if (is_sel)
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.25f, 0.45f, 0.75f, 1.0f));

        // Header: all words joined with color swatches
        std::string header = "  #" + std::to_string(i + 1) + ":  ";
        for (int wi = 0; wi < (int)sol.words.size(); ++wi) {
            if (wi > 0) header += "  +  ";
            header += sol.words[wi];
        }

        ImGui::PushID(i);

        // Use Selectable so the whole row is clickable and highlightable
        bool clicked = ImGui::Selectable(header.c_str(), is_sel,
            ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 0));
        if (clicked) {
            select_solution(is_sel ? -1 : i); // click selected → deselect
        }

        // Draw colored word-indicator squares to the left of the text
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 row_min = ImGui::GetItemRectMin();
            float sq = 10.0f;
            float x = row_min.x + 2.0f;
            float y = row_min.y + (ImGui::GetItemRectSize().y - sq) * 0.5f;
            for (int wi = 0; wi < (int)sol.words.size(); ++wi) {
                ImU32 col = (ImU32)WORD_COLORS[wi % NUM_WORD_COLORS];
                dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sq, y + sq), col, 2.0f);
                x += sq + 3.0f;
            }
        }

        if (is_sel) {
            ImGui::PopStyleColor(); // Header

            // Expand details inline under the selected solution
            ImGui::Indent(12.0f);
            for (int wi = 0; wi < (int)sol.words.size(); ++wi) {
                ImGui::PushID(wi);
                // Colored bullet using word color
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 cp = ImGui::GetCursorScreenPos();
                float sq = 10.0f;
                dl->AddRectFilled(cp, ImVec2(cp.x + sq, cp.y + sq),
                    (ImU32)WORD_COLORS[wi % NUM_WORD_COLORS], 2.0f);
                ImGui::SetCursorScreenPos(ImVec2(cp.x + sq + 6.0f, cp.y));

                ImGui::Text("%s", sol.words[wi].c_str());

                std::string coord_str;
                for (auto [r, c] : sol.paths[wi]) {
                    if (!coord_str.empty()) coord_str += " \xE2\x86\x92 ";
                    coord_str += "(" + std::to_string(r) + "," + std::to_string(c) + ")";
                }
                ImGui::SetCursorScreenPos(ImVec2(cp.x + sq + 6.0f, ImGui::GetCursorScreenPos().y));
                ImGui::TextWrapped("%s", coord_str.c_str());
                ImGui::PopID();
            }
            ImGui::Unindent(12.0f);

            // Auto-scroll to keep selected solution visible
            ImGui::SetScrollHereY(0.5f);
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

// ── Poll ──────────────────────────────────────────────────────────────────────

void UI::poll_results() {
    if (!solver_) return;
    std::vector<Solution> incoming;
    solver_->results.drain(incoming);
    if (incoming.empty()) return;

    for (auto& sol : incoming) {
        // Build a sorted word-set fingerprint for dedup
        std::vector<std::string> key = sol.words;
        std::sort(key.begin(), key.end());

        // Check against already-accepted solutions
        bool duplicate = false;
        for (auto& existing : solutions_) {
            std::vector<std::string> ekey = existing.words;
            std::sort(ekey.begin(), ekey.end());
            if (ekey == key) { duplicate = true; break; }
        }
        if (!duplicate) {
            solutions_.push_back(std::move(sol));
            focus_results_once_ = true;
        }
    }
}

// ── Config Builder ────────────────────────────────────────────────────────────

bool UI::build_config(SolverConfig& out, std::string& err) {
    // Parse min
    try {
        out.min_word_len = std::stoi(min_len_buf_);
    } catch (...) {
        err = "Invalid min word length."; return false;
    }
    // Parse max
    try {
        out.max_word_len = std::stoi(max_len_buf_);
    } catch (...) {
        err = "Invalid max word length."; return false;
    }
    if (out.min_word_len < 1) { err = "Min length must be >= 1."; return false; }
    if (out.max_word_len < out.min_word_len) { err = "Max must be >= min."; return false; }

    // Parse optional word lengths hint
    out.word_lengths.clear();
    std::string lens_str(word_lens_buf_);
    if (!lens_str.empty()) {
        std::stringstream ss(lens_str);
        std::string token;
        int sum = 0;
        while (std::getline(ss, token, ',')) {
            // Trim
            token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
            if (token.empty()) continue;
            int val;
            try { val = std::stoi(token); }
            catch (...) { err = "Invalid word length value: " + token; return false; }
            if (val < 1) { err = "Word length must be >= 1."; return false; }
            out.word_lengths.push_back(val);
            sum += val;
        }
        int filled = grid_.filled_count();
        if (!out.word_lengths.empty() && sum != filled) {
            err = "Length hint sum (" + std::to_string(sum) +
                  ") != filled cells (" + std::to_string(filled) + ").";
            return false;
        }
    }

    if (grid_.filled_count() == 0) { err = "Grid is empty."; return false; }
    if (!dict_.is_loaded()) { err = "Dictionary not loaded."; return false; }

    return true;
}
