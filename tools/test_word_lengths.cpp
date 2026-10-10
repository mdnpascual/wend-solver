// Standalone test for word-length hint detection (board_vision::detect_word_lengths).
//
// Runs the geometry pipeline on every sample screenshot, detects the hint-square
// rows below the board, and verifies:
//   1. the detected lengths sum to the board's filled-cell count, and
//   2. the breakdown matches the hand-verified expected values below.
//
// Build (from repo root, in a VS x64 dev prompt):
//   cl /nologo /std:c++17 /EHsc /I include /I C:\opencv\opencv\build\include ^
//     tools\test_word_lengths.cpp src\board_vision.cpp src\grid.cpp ^
//     /Fe:build\test_word_lengths.exe /Fo:build\ ^
//     /link /LIBPATH:C:\opencv\opencv\build\x64\vc16\lib opencv_world4100.lib
//
// Run:
//   .\build\test_word_lengths.exe            (uses default sample_inputs/ + assets/)
//   .\build\test_word_lengths.exe <samples_dir> <template.png>

#include "board_vision.h"
#include <opencv2/imgcodecs.hpp>
#include <cstdio>
#include <string>
#include <vector>
#include <numeric>

struct Expect {
    const char* file;
    std::vector<int> lengths;
};

// Hand-verified word-length breakdowns (top→bottom). Sums equal filled cells.
static const std::vector<Expect> kExpected = {
    {"Screenshot 2026-09-22 070256.png", {6, 7, 8}},
    {"Screenshot 2026-09-23 173824.png", {4, 5, 6, 8}},
    {"Screenshot 2026-09-24 101711.png", {5, 6, 8, 9}},
    {"Screenshot 2026-09-25 112800.png", {4, 5, 7, 8, 9}},
    {"Screenshot 2026-09-26 082703.png", {5, 7, 8, 9, 10}},
    {"Screenshot 2026-09-27 010811.png", {6, 7, 8, 9, 10}},
};

static std::string join(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += ','; s += std::to_string(v[i]); }
    return s;
}

int main(int argc, char** argv) {
    std::string samples = (argc > 1) ? argv[1] : "sample_inputs";
    std::string tmpl_path = (argc > 2) ? argv[2] : "assets/box-template.png";

    cv::Mat tmpl = cv::imread(tmpl_path, cv::IMREAD_COLOR);
    if (tmpl.empty()) {
        std::printf("FATAL: could not load template '%s'\n", tmpl_path.c_str());
        return 2;
    }

    int passed = 0, failed = 0;
    for (const auto& e : kExpected) {
        std::string path = samples + "/" + e.file;
        cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
        if (img.empty()) {
            std::printf("[FAIL] %s: could not load image\n", e.file);
            ++failed;
            continue;
        }

        board_vision::BoardResult r = board_vision::analyze(img);
        if (!r.ok) {
            std::printf("[FAIL] %s: analyze failed: %s\n", e.file, r.error.c_str());
            ++failed;
            continue;
        }

        bool confident = false;
        std::vector<int> got = board_vision::detect_word_lengths(img, r, tmpl, &confident);

        int got_sum = std::accumulate(got.begin(), got.end(), 0);
        int filled = r.filled_count();
        int exp_sum = std::accumulate(e.lengths.begin(), e.lengths.end(), 0);

        bool match = (got == e.lengths);
        bool sum_ok = (got_sum == filled);

        if (match && sum_ok && confident) {
            std::printf("[PASS] %s: lengths=[%s] sum=%d filled=%d\n",
                        e.file, join(got).c_str(), got_sum, filled);
            ++passed;
        } else {
            std::printf("[FAIL] %s: got=[%s] (sum=%d, confident=%d) "
                        "expected=[%s] filled=%d exp_sum=%d\n",
                        e.file, join(got).c_str(), got_sum, (int)confident,
                        join(e.lengths).c_str(), filled, exp_sum);
            ++failed;
        }
    }

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
