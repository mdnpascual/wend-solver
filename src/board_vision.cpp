#include "board_vision.h"
#include "grid.h"  // GRID_SIZE

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace board_vision {

namespace {

// Find the board bounding box via the warm (tan) border.
// Mirrors Python: mask = (R-B > 20) & (R > 90); largest external contour bbox.
bool find_board_box(const cv::Mat& img, cv::Rect& out) {
    cv::Mat mask(img.rows, img.cols, CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < img.rows; ++y) {
        const cv::Vec3b* row = img.ptr<cv::Vec3b>(y);
        uint8_t* mrow = mask.ptr<uint8_t>(y);
        for (int x = 0; x < img.cols; ++x) {
            int b = row[x][0], r = row[x][2];
            if ((r - b) > 20 && r > 90) mrow[x] = 255;
        }
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return false;

    double best_area = -1.0;
    cv::Rect best;
    for (auto& c : contours) {
        double a = cv::contourArea(c);
        if (a > best_area) { best_area = a; best = cv::boundingRect(c); }
    }
    // Sanity: roughly square, reasonably large.
    if (best.width < 50 || best.height < 50) return false;
    double aspect = (double)best.width / (double)best.height;
    if (aspect < 0.6 || aspect > 1.7) return false;
    out = best;
    return true;
}

// Cluster sorted 1-D values: start a new cluster when the gap exceeds `gap`.
// Returns cluster mean centers.
std::vector<double> cluster_1d(std::vector<double> vals, double gap) {
    std::vector<double> centers;
    if (vals.empty()) return centers;
    std::sort(vals.begin(), vals.end());
    std::vector<double> cur{vals[0]};
    auto flush = [&]() {
        double s = std::accumulate(cur.begin(), cur.end(), 0.0);
        centers.push_back(s / (double)cur.size());
    };
    for (size_t i = 1; i < vals.size(); ++i) {
        if (vals[i] - cur.back() <= gap) cur.push_back(vals[i]);
        else { flush(); cur.clear(); cur.push_back(vals[i]); }
    }
    flush();
    return centers;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2) ? v[n/2] : 0.5 * (v[n/2 - 1] + v[n/2]);
}

// N from centroid spacing: round(size / median(diffs of sorted centers)).
int spacing_n(const std::vector<double>& centers, double size) {
    if (centers.size() < 2) return (int)centers.size();
    std::vector<double> sorted = centers;
    std::sort(sorted.begin(), sorted.end());
    std::vector<double> diffs;
    for (size_t i = 1; i < sorted.size(); ++i) diffs.push_back(sorted[i] - sorted[i-1]);
    double step = median(diffs);
    if (step <= 0.0) return (int)centers.size();
    return (int)std::lround(size / step);
}

// Detect N by clustering bright letter-glyph centroids.
int detect_n(const cv::Mat& board) {
    cv::Mat gray;
    cv::cvtColor(board, gray, cv::COLOR_BGR2GRAY);
    const double bw = board.cols;

    cv::Mat bright;
    cv::threshold(gray, bright, 130, 255, cv::THRESH_BINARY);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5,5));
    cv::dilate(bright, bright, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bright, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<double> cxs, cys;
    for (auto& c : contours) {
        if (cv::contourArea(c) < 60) continue;
        cv::Rect r = cv::boundingRect(c);
        cxs.push_back(r.x + r.width  / 2.0);
        cys.push_back(r.y + r.height / 2.0);
    }
    if (cxs.empty()) return 0;

    double gap = bw * 0.06;
    std::vector<double> xc = cluster_1d(cxs, gap);
    std::vector<double> yc = cluster_1d(cys, gap);

    int n = std::max({ (int)xc.size(), (int)yc.size(),
                       spacing_n(xc, board.cols), spacing_n(yc, board.rows) });
    return n;
}

// Classify each cell: Hole / Letter / Empty. Mirrors Python classify().
std::vector<std::vector<CellKind>> classify(const cv::Mat& board, int n) {
    cv::Mat gray;
    cv::cvtColor(board, gray, cv::COLOR_BGR2GRAY);
    const double ch = (double)board.rows / n;
    const double cw = (double)board.cols / n;

    std::vector<std::vector<CellKind>> grid(n, std::vector<CellKind>(n, CellKind::Empty));

    for (int rr = 0; rr < n; ++rr) {
        for (int cc = 0; cc < n; ++cc) {
            int y0 = (int)(rr * ch), y1 = (int)((rr + 1) * ch);
            int x0 = (int)(cc * cw), x1 = (int)((cc + 1) * cw);
            y1 = std::min(y1, gray.rows); x1 = std::min(x1, gray.cols);
            if (y1 <= y0 || x1 <= x0) continue;
            cv::Mat cell = gray(cv::Range(y0, y1), cv::Range(x0, x1));

            int chh = cell.rows, cww = cell.cols;
            int k = std::max(2, (int)(std::min(chh, cww) * 0.12));

            // Background estimate = the DARKEST of the four corner-patch medians.
            // A hole is uniformly light in every corner, so its darkest corner
            // is still light. A letter cell always has at least one corner
            // showing the true (dark) cell background. Pooling all four corners
            // into one median (the old approach) failed on cells packed tight
            // against the board's tan border: the bright border bled into two
            // corners and dragged the pooled median above the hole threshold,
            // misclassifying perimeter letters as holes (see 7x7 samples).
            auto patch_median = [&](int py0, int py1, int px0, int px1) -> double {
                std::vector<uchar> px;
                for (int y = py0; y < py1; ++y)
                    for (int x = px0; x < px1; ++x)
                        px.push_back(cell.at<uchar>(y, x));
                if (px.empty()) return 255.0;
                std::sort(px.begin(), px.end());
                size_t m = px.size();
                return (m % 2) ? (double)px[m/2]
                               : 0.5 * (px[m/2 - 1] + px[m/2]);
            };
            double bg_med = std::min({
                patch_median(0, k, 0, k),
                patch_median(0, k, cww - k, cww),
                patch_median(chh - k, chh, 0, k),
                patch_median(chh - k, chh, cww - k, cww)
            });

            bool is_hole = bg_med > 55.0;

            // Center bright-pixel count (20%..80% region).
            int cy0 = (int)(chh * 0.2), cy1 = (int)(chh * 0.8);
            int cx0 = (int)(cww * 0.2), cx1 = (int)(cww * 0.8);
            int bright_px = 0;
            for (int y = cy0; y < cy1; ++y)
                for (int x = cx0; x < cx1; ++x)
                    if (cell.at<uchar>(y, x) > 130) ++bright_px;

            bool has_letter = (!is_hole) && (bright_px >= 15);

            grid[rr][cc] = is_hole ? CellKind::Hole
                         : (has_letter ? CellKind::Letter : CellKind::Empty);
        }
    }
    return grid;
}

// Cluster sorted 1-D values into groups (gap-based). Unlike cluster_1d this
// returns the members of each cluster (so callers can count per cluster).
std::vector<std::vector<double>> cluster_1d_groups(std::vector<double> vals, double gap) {
    std::vector<std::vector<double>> groups;
    if (vals.empty()) return groups;
    std::sort(vals.begin(), vals.end());
    groups.push_back({vals[0]});
    for (size_t i = 1; i < vals.size(); ++i) {
        if (vals[i] - groups.back().back() <= gap) groups.back().push_back(vals[i]);
        else groups.push_back({vals[i]});
    }
    return groups;
}

}  // namespace

BoardResult analyze(const cv::Mat& screenshot) {
    BoardResult res;
    if (screenshot.empty() || screenshot.cols < 30 || screenshot.rows < 30) {
        res.error = "Image too small to detect a grid";
        return res;
    }
    cv::Mat img = screenshot;
    if (img.channels() == 4) cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);
    else if (img.channels() == 1) cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);

    cv::Rect box;
    if (!find_board_box(img, box)) {
        res.error = "Could not detect a puzzle grid in the image";
        return res;
    }
    res.board = img(box).clone();
    res.box = box;

    int n = detect_n(res.board);
    if (n < 2 || n > GRID_SIZE) {
        res.error = "Could not determine grid size";
        return res;
    }
    res.n = n;
    res.cells = classify(res.board, n);
    res.ok = true;
    return res;
}

std::string layout_hint(const BoardResult& r) {
    std::string s;
    for (int rr = 0; rr < r.n; ++rr) {
        for (int cc = 0; cc < r.n; ++cc) {
            if (cc) s += ' ';
            switch (r.cells[rr][cc]) {
                case CellKind::Letter: s += '?'; break;
                case CellKind::Hole:   s += '#'; break;
                default:               s += '_'; break;
            }
        }
        if (rr + 1 < r.n) s += '\n';
    }
    return s;
}

int BoardResult::filled_count() const {
    int c = 0;
    for (const auto& row : cells)
        for (CellKind k : row)
            if (k == CellKind::Letter) ++c;
    return c;
}

std::vector<int> detect_word_lengths(const cv::Mat& screenshot,
                                     const BoardResult& analysis,
                                     const cv::Mat& square_template,
                                     bool* confident) {
    if (confident) *confident = false;
    std::vector<int> lengths;

    if (screenshot.empty() || square_template.empty() || !analysis.ok)
        return lengths;

    // Normalize both images to single-channel gray for matchTemplate.
    cv::Mat src = screenshot;
    if (src.channels() == 4) cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);
    else if (src.channels() == 1) cv::cvtColor(src, src, cv::COLOR_GRAY2BGR);
    cv::Mat tmpl = square_template;
    if (tmpl.channels() == 4) cv::cvtColor(tmpl, tmpl, cv::COLOR_BGRA2BGR);
    else if (tmpl.channels() == 1) cv::cvtColor(tmpl, tmpl, cv::COLOR_GRAY2BGR);

    const int tw = tmpl.cols, th = tmpl.rows;
    if (tw < 4 || th < 4) return lengths;

    // Hint region: below the board, spanning the board's width. Extend a
    // generous band downward; the Undo/Hint buttons sit lower and are rejected
    // later by the row-gap test. Clamp to image bounds.
    const cv::Rect& b = analysis.box;
    int y0 = b.y + b.height;
    int y1 = std::min(screenshot.rows, y0 + (int)(b.height * 0.55));
    int x0 = std::max(0, b.x);
    int x1 = std::min(screenshot.cols, b.x + b.width);
    if (y1 - y0 < th || x1 - x0 < tw) return lengths;

    cv::Mat region = src(cv::Range(y0, y1), cv::Range(x0, x1));
    cv::Mat gray_r, gray_t;
    cv::cvtColor(region, gray_r, cv::COLOR_BGR2GRAY);
    cv::cvtColor(tmpl,   gray_t, cv::COLOR_BGR2GRAY);

    cv::Mat score;
    cv::matchTemplate(gray_r, gray_t, score, cv::TM_CCOEFF_NORMED);

    // Collect candidate matches above threshold.
    const float kThreshold = 0.6f;
    struct Match { int x, y; float s; };
    std::vector<Match> matches;
    for (int y = 0; y < score.rows; ++y) {
        const float* srow = score.ptr<float>(y);
        for (int x = 0; x < score.cols; ++x)
            if (srow[x] >= kThreshold) matches.push_back({x, y, srow[x]});
    }
    if (matches.empty()) return lengths;

    // Non-maximum suppression: template matching yields a cluster of high-score
    // pixels per real square, so collapse near-coincident hits. Keep the
    // highest-scoring match, discard any other whose center is within ~0.6*tw.
    std::sort(matches.begin(), matches.end(),
              [](const Match& a, const Match& c) { return a.s > c.s; });
    const double min_dist = tw * 0.6;
    const double min_dist_sq = min_dist * min_dist;
    std::vector<cv::Point2d> centers;  // de-duplicated square centers
    std::vector<char> used(matches.size(), 0);
    for (size_t i = 0; i < matches.size(); ++i) {
        if (used[i]) continue;
        double cx = matches[i].x + tw / 2.0;
        double cy = matches[i].y + th / 2.0;
        centers.emplace_back(cx, cy);
        for (size_t j = i + 1; j < matches.size(); ++j) {
            if (used[j]) continue;
            double dx = (matches[j].x + tw / 2.0) - cx;
            double dy = (matches[j].y + th / 2.0) - cy;
            if (dx * dx + dy * dy < min_dist_sq) used[j] = 1;
        }
    }
    if (centers.empty()) return lengths;

    // Group centers into rows by Y (gap < ~0.5*th), count per row.
    std::vector<double> cys;
    for (auto& c : centers) cys.push_back(c.y);
    auto row_groups = cluster_1d_groups(cys, th * 0.5);

    struct Row { double y; int count; };
    std::vector<Row> rows;
    for (auto& g : row_groups) {
        double ymin = *std::min_element(g.begin(), g.end());
        double ymax = *std::max_element(g.begin(), g.end());
        int cnt = 0;
        for (auto& c : centers)
            if (c.y >= ymin - 1.0 && c.y <= ymax + 1.0) ++cnt;
        double yavg = std::accumulate(g.begin(), g.end(), 0.0) / (double)g.size();
        rows.push_back({yavg, cnt});
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& c) { return a.y < c.y; });

    // Reject the Undo/Hint buttons: legit hint rows are spaced at the constant
    // square pitch (~th). A row separated from the previous by a gap well beyond
    // the pitch is a button below the hint block — cut it and everything after.
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0 && (rows[i].y - rows[i-1].y) > th * 1.6) break;
        lengths.push_back(rows[i].count);
    }

    if (confident) {
        int sum = std::accumulate(lengths.begin(), lengths.end(), 0);
        *confident = !lengths.empty() && sum == analysis.filled_count();
    }
    return lengths;
}

}  // namespace board_vision
