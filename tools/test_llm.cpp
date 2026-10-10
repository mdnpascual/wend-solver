// End-to-end test of board_vision + llm_client against the live LM Studio model.
#include "board_vision.h"
#include "llm_client.h"
#include <opencv2/imgcodecs.hpp>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s <image>...\n", argv[0]); return 2; }
    for (int i = 1; i < argc; ++i) {
        cv::Mat img = cv::imread(argv[i], cv::IMREAD_COLOR);
        if (img.empty()) { std::printf("%s: load failed\n", argv[i]); continue; }
        auto a = board_vision::analyze(img);
        std::printf("=== %s ===\n", argv[i]);
        if (!a.ok) { std::printf("  vision error: %s\n", a.error.c_str()); continue; }
        std::printf("  N=%d, calling model...\n", a.n);
        auto r = llm_client::read_board(a.board, a, "http://localhost:1234", "qwen3-vl-8b-instruct");
        if (!r.ok) { std::printf("  llm error: %s\n", r.error.c_str()); continue; }
        std::printf("  [raw model output]\n");
        std::printf("%s\n", r.raw.c_str());
        std::printf("  [reconciled grid]\n");
        for (int rr = 0; rr < a.n; ++rr) {
            std::printf("  ");
            for (int cc = 0; cc < a.n; ++cc) {
                const Cell& cell = r.grid.cells[rr][cc];
                char ch = cell.filled ? cell.letter
                        : (a.cells[rr][cc] == board_vision::CellKind::Hole ? '.' : ' ');
                std::printf("%c ", ch);
            }
            std::printf("\n");
        }
    }
    return 0;
}
