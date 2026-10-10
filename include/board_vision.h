#pragma once
#include <opencv2/core.hpp>
#include <vector>
#include <string>

// Geometry-only vision stage of the clipboard import pipeline. Mirrors the
// verified Python prototype (tools/ocr_hybrid2.py):
//   1. Detect the puzzle board via its warm tan border.
//   2. Detect grid size N by clustering bright letter-glyph centroids.
//   3. Classify each cell as Hole / Letter / Empty.
// Letter *identity* is filled in later by the VLM; this stage owns structure.

namespace board_vision {

enum class CellKind { Empty, Letter, Hole };

struct BoardResult {
    bool ok = false;
    std::string error;

    cv::Mat board;                 // cropped board image (BGR)
    cv::Rect box;                  // board bounding box in the source screenshot
    int n = 0;                     // grid size (N x N)
    std::vector<std::vector<CellKind>> cells;  // n x n classification

    // Number of filled (Letter) cells — the word lengths should sum to this.
    int filled_count() const;
};

// Run board detection + N detection + cell classification on a full screenshot.
BoardResult analyze(const cv::Mat& screenshot);

// Detect the word-length hint rows shown below the board (rows of fixed-size
// gray rounded squares; each row = one word, square count = that word's length).
//
//   screenshot: the full source image (NOT the cropped board).
//   analysis:   result of analyze() — provides the board box and filled count.
//   square_template: the reference hint-square image (src/box-template.png),
//                    loaded as BGR. If empty, detection fails gracefully.
//
// Returns the per-word lengths top-to-bottom (e.g. {6,7,8}). Empty on failure.
// `confident` (if non-null) is set true only when the detected lengths sum to
// the board's filled-cell count — the caller can treat !confident as a hint to
// prompt the user to review.
std::vector<int> detect_word_lengths(const cv::Mat& screenshot,
                                     const BoardResult& analysis,
                                     const cv::Mat& square_template,
                                     bool* confident = nullptr);

// Build the layout hint string sent to the VLM, using tokens:
//   '?' = letter cell to read, '#' = hole, '_' = empty.
// Rows separated by '\n', tokens space-separated.
std::string layout_hint(const BoardResult& r);

}  // namespace board_vision
