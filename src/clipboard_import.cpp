#include "clipboard_import.h"
#include "board_vision.h"
#include "llm_client.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <thread>
#include <filesystem>

// Defined in clipboard_image.cpp — reads CF_DIB/CF_DIBV5 into a BGR cv::Mat.
cv::Mat read_clipboard_image();

// Resolve the hint-square template asset relative to the executable / CWD,
// mirroring how the dictionary word list is located in main.cpp.
static std::string find_box_template() {
    for (auto& candidate : {
        std::string("assets/box-template.png"),
        std::string("../assets/box-template.png"),
        std::string("../../assets/box-template.png")
    }) {
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return "assets/box-template.png";  // best guess
}

ClipboardImporter::ClipboardImporter(LMStudio& lm) : lm_(lm) {}

ClipboardImporter::~ClipboardImporter() {
    if (worker_.joinable()) worker_.join();
}

// ── Status helpers ────────────────────────────────────────────────────────────
std::string ClipboardImporter::status_message() const {
    std::lock_guard<std::mutex> l(mtx_);
    return status_msg_;
}
std::string ClipboardImporter::error() const {
    std::lock_guard<std::mutex> l(mtx_);
    return error_msg_;
}
void ClipboardImporter::set_status(Status s, const std::string& m) {
    status_.store(s);
    std::lock_guard<std::mutex> l(mtx_);
    status_msg_ = m;
}
void ClipboardImporter::set_error(const std::string& m) {
    status_.store(Status::Failed);
    std::lock_guard<std::mutex> l(mtx_);
    error_msg_ = m;
    status_msg_ = m;
}

// ── Public: begin an import from the clipboard ────────────────────────────────
bool ClipboardImporter::begin_from_clipboard(std::string& error) {
    // Reject re-entry while a previous import is still active.
    if (busy()) { error = "An import is already in progress."; return false; }

    cv::Mat img = read_clipboard_image();
    if (img.empty()) {
        error = "Clipboard does not contain an image";
        return false;
    }
    if (img.cols < 30 || img.rows < 30) {
        error = "Image too small to detect a grid";
        return false;
    }

    // Encode the captured image to PNG bytes so the worker owns a stable copy
    // (independent of the clipboard, which the user may change).
    std::vector<uchar> png;
    if (!cv::imencode(".png", img, png)) {
        error = "Failed to read the clipboard image";
        return false;
    }

    // Join any finished prior worker before starting a new one.
    if (worker_.joinable()) worker_.join();

    {
        std::lock_guard<std::mutex> l(mtx_);
        pending_png_.assign(png.begin(), png.end());
        error_msg_.clear();
        have_result_ = false;
    }

    // Queue vs. process immediately depending on model readiness.
    if (lm_.is_ready())
        set_status(Status::Processing, "Reading board from image...");
    else
        set_status(Status::Queued, "Waiting for vision model to load...");

    worker_active_.store(true);
    worker_ = std::thread([this] { run_pipeline(); });
    return true;
}

// ── Consume the result ────────────────────────────────────────────────────────
bool ClipboardImporter::take_result(Grid& out) {
    std::vector<int> ignored_lengths;
    bool ignored_conf;
    return take_result(out, ignored_lengths, ignored_conf);
}

bool ClipboardImporter::take_result(Grid& out, std::vector<int>& word_lengths,
                                    bool& lengths_confident) {
    if (status_.load() != Status::Done) return false;
    std::lock_guard<std::mutex> l(mtx_);
    if (!have_result_) return false;
    out = result_;
    word_lengths = word_lengths_;
    lengths_confident = lengths_confident_;
    have_result_ = false;
    status_.store(Status::Idle);
    status_msg_.clear();
    return true;
}

// ── Worker body ───────────────────────────────────────────────────────────────
void ClipboardImporter::run_pipeline() {
    // If queued, wait for the model to become ready (or error out).
    if (status_.load() == Status::Queued) {
        while (true) {
            LMStudio::State st = lm_.state();
            if (st == LMStudio::State::Ready) break;
            if (st == LMStudio::State::Error) {
                set_error("Vision model unavailable: " + lm_.status_message());
                worker_active_.store(false);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        set_status(Status::Processing, "Reading board from image...");
    }

    // Copy out the pending PNG bytes.
    std::vector<uint8_t> png;
    {
        std::lock_guard<std::mutex> l(mtx_);
        png = pending_png_;
    }
    if (png.empty()) {
        set_error("No image to process");
        worker_active_.store(false);
        return;
    }

    // Decode.
    cv::Mat img = cv::imdecode(png, cv::IMREAD_COLOR);
    if (img.empty()) {
        set_error("Failed to decode the clipboard image");
        worker_active_.store(false);
        return;
    }

    // 1. Geometry: board detection + N + hole/letter map.
    board_vision::BoardResult analysis = board_vision::analyze(img);
    if (!analysis.ok) {
        set_error(analysis.error.empty()
                  ? "Could not detect a puzzle grid in the image"
                  : analysis.error);
        worker_active_.store(false);
        return;
    }

    // 2. Letters via the local VLM, reconciled against the hole map.
    llm_client::OcrResult ocr = llm_client::read_board(
        analysis.board, analysis, lm_.base_url(), lm_.model_id());
    if (!ocr.ok) {
        set_error(ocr.error.empty()
                  ? "Could not read letters from grid cells"
                  : ocr.error);
        worker_active_.store(false);
        return;
    }

    // 2b. Word-length hints from the gray squares below the board (pure
    //     OpenCV geometry). Non-fatal: on failure we simply leave the fields
    //     blank rather than aborting the whole import.
    std::vector<int> lengths;
    bool lengths_ok = false;
    {
        cv::Mat tmpl = cv::imread(find_box_template(), cv::IMREAD_COLOR);
        if (!tmpl.empty()) {
            lengths = board_vision::detect_word_lengths(img, analysis, tmpl, &lengths_ok);
        }
    }

    // 3. Publish the filled grid.
    {
        std::lock_guard<std::mutex> l(mtx_);
        result_ = ocr.grid;
        word_lengths_ = lengths;
        lengths_confident_ = lengths_ok;
        have_result_ = true;
    }
    if (!ocr.warnings.empty()) {
        // Low-confidence: board_vision and the VLM disagreed on some row's
        // letter count. The grid is still populated (with '?' where letters
        // were missing), but flag it so the user reviews carefully.
        std::string msg = "Grid loaded, but some cells are uncertain — please review. (";
        for (size_t i = 0; i < ocr.warnings.size(); ++i) {
            if (i) msg += "; ";
            msg += ocr.warnings[i];
        }
        msg += ")";
        set_status(Status::Done, msg);
    } else {
        set_status(Status::Done, "Grid loaded from clipboard.");
    }
    worker_active_.store(false);
}
