// Standalone test: run the C++ board_vision pipeline on the sample PNGs and
// print N + the layout, so we can confirm parity with the Python prototype.
#include "board_vision.h"
#include <opencv2/imgcodecs.hpp>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s <image>...\n", argv[0]); return 2; }
    for (int i = 1; i < argc; ++i) {
        cv::Mat img = cv::imread(argv[i], cv::IMREAD_COLOR);
        if (img.empty()) { std::printf("%s: FAILED to load\n", argv[i]); continue; }
        auto r = board_vision::analyze(img);
        std::printf("=== %s ===\n", argv[i]);
        if (!r.ok) { std::printf("  error: %s\n", r.error.c_str()); continue; }
        std::printf("  board %dx%d  N=%d\n", r.board.cols, r.board.rows, r.n);
        for (int rr = 0; rr < r.n; ++rr) {
            std::printf("  ");
            for (int cc = 0; cc < r.n; ++cc) {
                char ch = '_';
                switch (r.cells[rr][cc]) {
                    case board_vision::CellKind::Letter: ch = '?'; break;
                    case board_vision::CellKind::Hole:   ch = '.'; break;
                    default: ch = ' '; break;
                }
                std::printf("%c ", ch);
            }
            std::printf("\n");
        }
    }
    return 0;
}
