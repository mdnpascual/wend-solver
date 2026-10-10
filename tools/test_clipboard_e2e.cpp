// End-to-end test of the REAL clipboard path:
//   load sample PNG -> place on Windows clipboard as CF_DIB ->
//   drive ClipboardImporter (same API the UI uses) -> print filled grid.
// This exercises read_clipboard_image() (the only piece not yet tested E2E)
// plus the full importer + board_vision + llm_client integration.
#include "clipboard_import.h"
#include "lmstudio.h"
#include "grid.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <thread>
#include <chrono>

// Put a BGR cv::Mat on the clipboard as a bottom-up 24-bit CF_DIB.
static bool set_clipboard_dib(const cv::Mat& bgr) {
    int w = bgr.cols, h = bgr.rows;
    size_t stride = ((size_t)w * 3 + 3) & ~size_t(3);
    size_t pix_bytes = stride * h;
    size_t total = sizeof(BITMAPINFOHEADER) + pix_bytes;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, total);
    if (!hMem) return false;
    uint8_t* p = (uint8_t*)GlobalLock(hMem);

    BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)p;
    ZeroMemory(bih, sizeof(*bih));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = w;
    bih->biHeight = h;          // positive => bottom-up
    bih->biPlanes = 1;
    bih->biBitCount = 24;
    bih->biCompression = BI_RGB;

    uint8_t* bits = p + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < h; ++y) {
        const cv::Vec3b* srow = bgr.ptr<cv::Vec3b>(h - 1 - y);  // flip to bottom-up
        uint8_t* drow = bits + (size_t)y * stride;
        for (int x = 0; x < w; ++x) {
            drow[x*3+0] = srow[x][0];
            drow[x*3+1] = srow[x][1];
            drow[x*3+2] = srow[x][2];
        }
    }
    GlobalUnlock(hMem);

    if (!OpenClipboard(nullptr)) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_DIB, hMem);  // clipboard owns hMem now
    CloseClipboard();
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: %s <image>...\n", argv[0]); return 2; }

    LMStudio lm("qwen3-vl-8b-instruct");
    lm.start();
    // Wait for readiness (mirrors app startup).
    for (int i = 0; i < 60 && !lm.is_ready() &&
                    lm.state() != LMStudio::State::Error; ++i)
        std::this_thread::sleep_for(std::chrono::seconds(1));
    std::printf("LM state: ready=%d msg=%s\n", lm.is_ready(), lm.status_message().c_str());

    ClipboardImporter importer(lm);

    for (int i = 1; i < argc; ++i) {
        cv::Mat img = cv::imread(argv[i], cv::IMREAD_COLOR);
        if (img.empty()) { std::printf("%s: load failed\n", argv[i]); continue; }
        if (!set_clipboard_dib(img)) { std::printf("clipboard set failed\n"); continue; }

        std::string err;
        std::printf("=== %s ===\n", argv[i]);
        if (!importer.begin_from_clipboard(err)) {
            std::printf("  begin failed: %s\n", err.c_str());
            continue;
        }
        // Poll like the UI would.
        for (int t = 0; t < 120; ++t) {
            auto s = importer.status();
            if (s == ClipboardImporter::Status::Done) {
                Grid g;
                if (importer.take_result(g)) {
                    std::printf("  loaded grid (filled=%d):\n", g.filled_count());
                    // Determine printable extent (up to first all-empty tail).
                    for (int r = 0; r < GRID_SIZE; ++r) {
                        bool any = false;
                        for (int c = 0; c < GRID_SIZE; ++c)
                            if (g.cells[r][c].filled) any = true;
                        if (!any) continue;
                        std::printf("  ");
                        for (int c = 0; c < GRID_SIZE; ++c) {
                            const Cell& cell = g.cells[r][c];
                            std::printf("%c ", cell.filled ? cell.letter : '.');
                        }
                        std::printf("\n");
                    }
                }
                break;
            }
            if (s == ClipboardImporter::Status::Failed) {
                std::printf("  FAILED: %s\n", importer.error().c_str());
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    return 0;
}
