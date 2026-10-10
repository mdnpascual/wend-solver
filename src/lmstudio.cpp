#include "lmstudio.h"
#include "llm_client.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif

// ── Constructor / Destructor ──────────────────────────────────────────────────
LMStudio::LMStudio(std::string model_id) : model_id_(std::move(model_id)) {}

LMStudio::~LMStudio() {
    if (worker_.joinable()) worker_.join();
}

// ── Status ────────────────────────────────────────────────────────────────────
std::string LMStudio::status_message() const {
    std::lock_guard<std::mutex> l(msg_mtx_);
    return status_msg_;
}
void LMStudio::set_status(const std::string& m) {
    std::lock_guard<std::mutex> l(msg_mtx_);
    status_msg_ = m;
}

// ── Helpers ─────────────────────────────────────────────────────────────────
std::string LMStudio::resolve_lms_exe() const {
#ifdef _WIN32
    // 1. %USERPROFILE%\.lmstudio\bin\lms.exe
    char* home = nullptr; size_t len = 0;
    if (_dupenv_s(&home, &len, "USERPROFILE") == 0 && home) {
        std::string p = std::string(home) + "\\.lmstudio\\bin\\lms.exe";
        free(home);
        DWORD attr = GetFileAttributesA(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
            return p;
    }
    // 2. rely on PATH.
    return "lms.exe";
#else
    return "lms";
#endif
}

bool LMStudio::server_alive() const {
    std::string resp, err;
    return llm_client::http_get(base_url_ + "/v1/models", resp, err, 2500);
}

// ── Process launch (hidden) ───────────────────────────────────────────────────
#ifdef _WIN32
// Run a command line and capture its stdout (hidden window). Returns true if
// the process ran to completion; captured output goes to `out`.
static bool run_capture(const std::string& cmdline, std::string& out, DWORD wait_ms) {
    out.clear();
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};

    std::string mutable_cmd = cmdline;
    BOOL ok = CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);  // parent keeps only the read end
    if (!ok) { CloseHandle(rd); return false; }

    // Drain the pipe.
    char buf[4096]; DWORD read = 0;
    while (ReadFile(rd, buf, sizeof(buf), &read, nullptr) && read > 0)
        out.append(buf, read);
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, wait_ms);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}
#endif

bool LMStudio::model_loaded() const {
#ifdef _WIN32
    // `lms ps` lists RESIDENT (loaded) models. /v1/models is insufficient — it
    // lists all downloaded models regardless of load state, and blindly calling
    // `lms load` on an already-resident model spawns a duplicate instance.
    std::string out;
    if (!run_capture("\"" + lms_exe_ + "\" ps", out, 15000))
        return false;
    return out.find(model_id_) != std::string::npos;
#else
    return false;
#endif
}

// ── Process launch (hidden, no capture) ───────────────────────────────────────
#ifdef _WIN32
// Run a command line and optionally wait for it. Returns true if the process
// was launched (and, when wait=true, exited with code 0).
static bool run_hidden(const std::string& cmdline, bool wait, DWORD wait_ms) {
    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    // CreateProcess may modify the cmdline buffer, so make a mutable copy.
    std::string mutable_cmd = cmdline;
    BOOL ok = CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return false;

    bool result = true;
    if (wait) {
        DWORD w = WaitForSingleObject(pi.hProcess, wait_ms);
        if (w == WAIT_OBJECT_0) {
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            result = (code == 0);
        } else {
            result = false;  // timed out
        }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}
#endif

// ── Public start ──────────────────────────────────────────────────────────────
void LMStudio::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true))
        return;  // already started
    state_.store(State::Starting);
    set_status("Checking vision model...");
    worker_ = std::thread([this] { run_readiness(); });
}

// ── Readiness sequence (worker thread) ────────────────────────────────────────
void LMStudio::run_readiness() {
#ifdef _WIN32
    lms_exe_ = resolve_lms_exe();

    // 1. Ensure the server is alive.
    if (!server_alive()) {
        set_status("Starting LM Studio server...");
        run_hidden("\"" + lms_exe_ + "\" server start", /*wait=*/true, 30000);

        // Poll for the server to come up (up to ~30s).
        bool up = false;
        for (int i = 0; i < 30; ++i) {
            if (server_alive()) { up = true; break; }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!up) {
            set_status("Failed to start LM Studio server. Is LM Studio installed?");
            state_.store(State::Error);
            return;
        }
    }

    // 2. Ensure the model is loaded. Skip the load call if it is already
    //    resident (avoids a redundant/erroring `lms load`).
    if (!model_loaded()) {
        set_status("Loading vision model (" + model_id_ + ")...");
        // --gpu max mirrors our prototype; -y auto-approves for scripting.
        run_hidden("\"" + lms_exe_ + "\" load " + model_id_ + " --gpu max -y",
                   /*wait=*/true, 180000);
    }

    // 3. Confirm the model is reachable.
    bool ready = false;
    for (int i = 0; i < 20; ++i) {
        if (server_alive() && model_loaded()) { ready = true; break; }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (ready) {
        set_status("Vision model ready.");
        state_.store(State::Ready);
    } else {
        // The server is up; even if the model listing check failed, JIT loading
        // may still serve requests. Treat server-up as usable but warn.
        if (server_alive()) {
            set_status("Server up; model may load on first paste.");
            state_.store(State::Ready);
        } else {
            set_status("Vision model failed to load.");
            state_.store(State::Error);
        }
    }
#else
    set_status("Clipboard import is Windows-only.");
    state_.store(State::Error);
#endif
}
