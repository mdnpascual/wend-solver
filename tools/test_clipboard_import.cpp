// Integration test for the clipboard-import OCR pipeline.
//
//   screenshot (PNG) -> board_vision::analyze -> llm_client::read_board
//   -> reconciled Grid, compared against a hand-verified expected grid.
//
// Expected grids are loaded from tools/expected_grids.txt (see that file for the
// format), so no rebuild is needed to add/adjust cases. Requires a running LM
// Studio server with the configured vision model.
//
// Build (from repo root, adjust OpenCV paths to your SDK):
//   cl /std:c++17 /EHsc /I include /I C:\opencv\opencv\build\include ^
//      tools\test_clipboard_import.cpp src\board_vision.cpp src\llm_client.cpp ^
//      src\grid.cpp /Fe:build\test_clipboard_import.exe ^
//      /link /LIBPATH:C:\opencv\opencv\build\x64\vc16\lib opencv_world4100.lib winhttp.lib
//
// Run:
//   build\test_clipboard_import.exe [sample_inputs_dir] [expected_file] [base_url] [model]

#include "board_vision.h"
#include "llm_client.h"
#include "grid.h"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Expected grids, hand-verified against the screenshots. '.' = hole/empty.
struct Expected {
    std::string file;
    std::vector<std::string> rows;  // uppercase letters, '.' for non-letter cells
};

// Load expected grids from a data file. Returns empty on failure (with a
// message on stderr). Format documented in tools/expected_grids.txt.
static std::vector<Expected> load_expected(const std::string& path) {
    std::vector<Expected> out;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "ERROR: could not open expected-grids file: " << path << "\n";
        return out;
    }
    std::string line;
    while (std::getline(in, line)) {
        // Strip trailing CR (Windows line endings) and surrounding whitespace.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t'))
            line.pop_back();
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;         // blank
        if (line[s] == '#') continue;                 // comment
        if (line[s] == '@') {                          // new board
            out.push_back(Expected{line.substr(s + 1), {}});
        } else if (!out.empty()) {                     // a row for the current board
            std::string row;
            for (char c : line.substr(s))
                if (c != ' ' && c != '\t') row += (char)std::toupper((unsigned char)c);
            if (!row.empty()) out.back().rows.push_back(row);
        }
    }
    return out;
}

// Render a Grid into the same '.'/letter row format for comparison + display.
static std::vector<std::string> grid_to_rows(const Grid& g, int n) {
    std::vector<std::string> rows;
    for (int r = 0; r < n; ++r) {
        std::string line;
        for (int c = 0; c < n; ++c) {
            const Cell& cell = g.cells[r][c];
            line += (cell.filled && cell.letter) ? (char)cell.letter : '.';
        }
        rows.push_back(line);
    }
    return rows;
}

int main(int argc, char* argv[]) {
    std::string dir = (argc > 1) ? argv[1] : "sample_inputs";
    std::string expected_file = (argc > 2) ? argv[2] : "tools/expected_grids.txt";
    std::string base_url = (argc > 3) ? argv[3] : "http://localhost:1234";
    std::string model = (argc > 4) ? argv[4] : "qwen3-vl-8b-instruct";

    std::vector<Expected> expected = load_expected(expected_file);
    if (expected.empty()) {
        std::cerr << "No expected grids loaded — nothing to test.\n";
        return 2;
    }

    int passed = 0, failed = 0;

    for (const Expected& e : expected) {
        fs::path path = fs::path(dir) / e.file;
        std::cout << "==== " << e.file << " ====\n";

        cv::Mat img = cv::imread(path.string(), cv::IMREAD_COLOR);
        if (img.empty()) {
            std::cout << "  FAIL: could not read image at " << path.string() << "\n";
            ++failed;
            continue;
        }

        board_vision::BoardResult analysis = board_vision::analyze(img);
        if (!analysis.ok) {
            std::cout << "  FAIL: board_vision: " << analysis.error << "\n";
            ++failed;
            continue;
        }
        const int n = analysis.n;

        llm_client::OcrResult ocr =
            llm_client::read_board(analysis.board, analysis, base_url, model);
        if (!ocr.ok) {
            std::cout << "  FAIL: read_board: " << ocr.error << "\n";
            ++failed;
            continue;
        }

        std::vector<std::string> got = grid_to_rows(ocr.grid, n);

        // Print the reconciled grid.
        for (const std::string& row : got) {
            std::cout << "  ";
            for (size_t i = 0; i < row.size(); ++i) {
                if (i) std::cout << ' ';
                std::cout << row[i];
            }
            std::cout << "\n";
        }
        for (const std::string& w : ocr.warnings)
            std::cout << "  [warn] " << w << "\n";

        // Compare against expectation.
        bool ok = ((int)e.rows.size() == n);
        for (int r = 0; ok && r < n; ++r) {
            std::string exp = e.rows[r];
            for (char& ch : exp) ch = (char)std::toupper((unsigned char)ch);
            if (exp != got[r]) ok = false;
        }

        if (ok) {
            std::cout << "  PASS\n";
            ++passed;
        } else {
            std::cout << "  FAIL: reconciled grid does not match expected:\n";
            for (const std::string& row : e.rows)
                std::cout << "    expected: " << row << "\n";
            ++failed;
        }
        std::cout << "\n";
    }

    std::cout << "==== SUMMARY: " << passed << " passed, " << failed << " failed ====\n";
    return failed == 0 ? 0 : 1;
}
