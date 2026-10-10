#pragma once
#include "grid.h"
#include "lmstudio.h"

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <cstdint>

// Imports a Wend puzzle from a clipboard screenshot:
//   clipboard image -> board detection (OpenCV) -> N + hole map ->
//   local VLM letter OCR -> reconciled Grid.
//
// Runs the heavy work on a background thread so the UI stays responsive. The UI
// polls status() each frame and, on Done, takes the result via take_result().
class ClipboardImporter {
public:
    enum class Status {
        Idle,        // nothing happening
        Queued,      // image captured, waiting for the model to become ready
        Processing,  // pipeline running
        Done,        // result ready to be consumed via take_result()
        Failed       // see error()
    };

    explicit ClipboardImporter(LMStudio& lm);
    ~ClipboardImporter();

    // Capture the current clipboard image (Win32) and begin an import.
    // If the model is not ready yet, the image is queued and processing starts
    // automatically once the model becomes ready.
    // Returns false immediately if the clipboard has no valid image.
    bool begin_from_clipboard(std::string& error);

    Status      status() const { return status_.load(); }
    bool        busy() const {
        Status s = status_.load();
        return s == Status::Queued || s == Status::Processing;
    }
    std::string status_message() const;
    std::string error() const;

    // If status()==Done, move the parsed grid out and reset to Idle.
    // Returns false if no result is pending.
    bool take_result(Grid& out);

    // Overload that also returns the detected word-length breakdown (top→bottom,
    // e.g. {6,7,8}) and whether it was confident (lengths summed to the filled
    // cell count). `word_lengths` is empty when detection found nothing.
    bool take_result(Grid& out, std::vector<int>& word_lengths, bool& lengths_confident);

private:
    LMStudio& lm_;

    std::atomic<Status> status_{Status::Idle};
    mutable std::mutex  mtx_;
    std::string         status_msg_;
    std::string         error_msg_;

    // Captured image bytes (encoded PNG) awaiting/undergoing processing.
    std::vector<uint8_t> pending_png_;

    std::thread worker_;
    std::atomic<bool> worker_active_{false};

    Grid   result_;
    bool   have_result_ = false;

    // Detected word-length breakdown from the hint squares below the board.
    std::vector<int> word_lengths_;
    bool             lengths_confident_ = false;

    void set_status(Status s, const std::string& msg);
    void set_error(const std::string& msg);
    void run_pipeline();  // worker body
};
