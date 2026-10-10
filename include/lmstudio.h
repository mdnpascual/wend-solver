#pragma once
#include <string>
#include <atomic>
#include <thread>
#include <mutex>

// Manages the local LM Studio server + vision model lifecycle.
//
// On start(), a background thread:
//   1. Checks whether the LM Studio server responds on the API port.
//   2. If not, launches it via the `lms` CLI (`lms server start`).
//   3. Checks whether the target vision model is loaded.
//   4. If not, loads it (`lms load <model>`).
// The public state() reflects progress and is safe to poll from the UI thread.
class LMStudio {
public:
    enum class State {
        Uninitialized,  // not started yet
        Starting,       // server/model coming up
        Ready,          // server up + model loaded, ready for requests
        Error           // gave up; see status_message()
    };

    // model_id: LM Studio model identifier (e.g. "qwen3-vl-8b-instruct").
    explicit LMStudio(std::string model_id);
    ~LMStudio();

    // Kick off the background readiness sequence (non-blocking). Idempotent.
    void start();

    State       state() const { return state_.load(); }
    bool        is_ready() const { return state_.load() == State::Ready; }
    std::string status_message() const;

    // API endpoint base (e.g. "http://localhost:1234").
    const std::string& base_url() const { return base_url_; }
    const std::string& model_id() const { return model_id_; }

private:
    std::string model_id_;
    std::string base_url_ = "http://localhost:1234";
    std::string lms_exe_;  // resolved path to lms.exe

    std::atomic<State> state_{State::Uninitialized};
    mutable std::mutex msg_mtx_;
    std::string        status_msg_;

    std::thread worker_;
    std::atomic<bool> started_{false};

    void set_status(const std::string& msg);
    void run_readiness();

    // Helpers (implemented in lmstudio.cpp)
    std::string resolve_lms_exe() const;
    bool        server_alive() const;
    bool        model_loaded() const;
};
