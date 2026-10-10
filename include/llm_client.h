#pragma once
#include "board_vision.h"
#include "grid.h"

#include <opencv2/core.hpp>
#include <string>

// Talks to the local LM Studio OpenAI-compatible API to read the letters of a
// Wend board, then reconciles those letters against the authoritative hole map
// from board_vision to produce a filled Grid.
namespace llm_client {

struct OcrResult {
    bool ok = false;
    std::string error;
    Grid grid;              // filled result (on success)
    std::string raw;        // raw model text (for diagnostics)
    // Low-confidence warnings: populated when the VLM's per-row letter count
    // disagrees with board_vision's authoritative layout. Non-empty means the
    // grid may contain misaligned or '?' cells and should be reviewed.
    std::vector<std::string> warnings;
};

// board: cropped board image (BGR) from board_vision.
// analysis: the geometry result (N + hole/letter map) — authoritative.
// base_url: e.g. "http://localhost:1234"; model: e.g. "qwen3-vl-8b-instruct".
OcrResult read_board(const cv::Mat& board,
                     const board_vision::BoardResult& analysis,
                     const std::string& base_url,
                     const std::string& model);

// ── Lower-level helpers (exposed for testing) ─────────────────────────────────

// Base64-encode raw bytes.
std::string base64_encode(const unsigned char* data, size_t len);

// POST a JSON body to url, return response body. ok=false on transport error.
bool http_post_json(const std::string& url, const std::string& body,
                    std::string& response_out, std::string& error_out,
                    int timeout_ms = 180000);

// GET a url, return response body. ok=false on transport error. Short timeout
// by default (used for liveness probes).
bool http_get(const std::string& url, std::string& response_out,
              std::string& error_out, int timeout_ms = 3000);

// Extract choices[0].message.content from an OpenAI-style JSON response.
std::string extract_message_content(const std::string& json);

// Strip chain-of-thought / reasoning artifacts from a model's content string
// before it is reconciled into a grid. Reasoning models (and some VLMs when
// they "hesitate") emit their thinking inline inside `content`, wrapped in
// <think>...</think> (or <thinking>, <reason>, <reasoning>) tags. Those lines
// look like prose but reconcile() would parse their letters as grid rows,
// corrupting the result. This removes any such block (including an unterminated
// trailing one) and returns the remaining answer text.
std::string strip_reasoning(const std::string& content);

// Reconcile a VLM free-form grid (rows of letters/dots) against the hole map.
// Fills letters row-major into Letter cells. Returns the completed Grid.
// If `warnings` is non-null, appends a human-readable message for every row
// whose VLM letter count disagrees with board_vision's authoritative count
// (a signal that the row may be misaligned / contain '?' placeholders).
Grid reconcile(const board_vision::BoardResult& analysis, const std::string& vlm_text,
               std::vector<std::string>* warnings = nullptr);

}  // namespace llm_client
