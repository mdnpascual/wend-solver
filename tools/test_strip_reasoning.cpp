// Unit test for llm_client::strip_reasoning + reconcile.
//
// Regression for the 2026-09-28 sample: a reasoning-capable VLM emitted its
// chain-of-thought inline in `content` (<think>...</think>). Without stripping,
// reconcile() parsed the prose lines as grid rows and produced garbage while
// board_vision was 100% correct. strip_reasoning() must remove the block so the
// answer grid reconciles cleanly.
//
// Pure string/logic test — no OpenCV image work, no LM Studio.

#include "llm_client.h"
#include "board_vision.h"
#include "grid.h"

#include <cassert>
#include <iostream>
#include <string>

using board_vision::BoardResult;
using board_vision::CellKind;

static int g_failures = 0;
static void check(bool cond, const std::string& what) {
    if (cond) { std::cout << "[PASS] " << what << "\n"; }
    else      { std::cout << "[FAIL] " << what << "\n"; ++g_failures; }
}

// Build the authoritative layout for the 2026-09-28 board.
//   T # E # A
//   S E L I G
//   # T E E #
//   O N S S O
//   C # U # P
static BoardResult make_board_0928() {
    BoardResult b;
    b.ok = true;
    b.n = 5;
    const char* rows[5] = {
        "L.L.L",   // '.' = hole here
        "LLLLL",
        ".LLL.",
        "LLLLL",
        "L.L.L",
    };
    b.cells.assign(5, std::vector<CellKind>(5, CellKind::Empty));
    for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 5; ++c)
            b.cells[r][c] = (rows[r][c] == 'L') ? CellKind::Letter : CellKind::Hole;
    return b;
}

static std::string grid_to_string(const Grid& g, int n) {
    std::string s;
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c)
            s += g.cells[r][c].filled ? g.cells[r][c].letter : '.';
        if (r + 1 < n) s += '\n';
    }
    return s;
}

int main() {
    using namespace llm_client;

    const std::string expected =
        "T.E.A\n"
        "SELIG\n"
        ".TEE.\n"
        "ONSSO\n"
        "C.U.P";

    const std::string answer =
        "T # E # A\n"
        "S E L I G\n"
        "# T E E #\n"
        "O N S S O\n"
        "C # U # P";

    // 1. Clean answer (no reasoning) still reconciles correctly.
    {
        BoardResult b = make_board_0928();
        std::vector<std::string> warns;
        Grid g = reconcile(b, strip_reasoning(answer), &warns);
        check(grid_to_string(g, 5) == expected, "clean answer reconciles");
        check(warns.empty(), "clean answer: no warnings");
    }

    // 2. <think> block before the answer (the real failure mode).
    {
        std::string content =
            "<think>\n"
            "Let me look carefully. First row: T, hole, E, hole, A.\n"
            "Is that an A or an H at the end? Reconsidering... it's A.\n"
            "Row 2 looks like S E L I G. The I could be an l, but it's I.\n"
            "</think>\n" + answer;
        BoardResult b = make_board_0928();
        std::vector<std::string> warns;
        Grid g = reconcile(b, strip_reasoning(content), &warns);
        check(grid_to_string(g, 5) == expected, "<think> block stripped, grid correct");
        check(warns.empty(), "<think> block: no warnings after strip");
    }

    // 3. Case-insensitive <THINKING> tag.
    {
        std::string content =
            "<THINKING>hmm let me count the columns 1 2 3 4 5</THINKING>\n" + answer;
        Grid g = reconcile(make_board_0928(), strip_reasoning(content), nullptr);
        check(grid_to_string(g, 5) == expected, "<THINKING> (uppercase) stripped");
    }

    // 4. Unterminated trailing reasoning (response cut off mid-think): the
    //    answer must come BEFORE the open tag for anything to survive; if a
    //    response is only reasoning, strip yields empty (caught upstream as
    //    an empty-response error rather than a corrupted grid).
    {
        std::string content = answer + "\n<think>\nWait, let me double check row 3...";
        Grid g = reconcile(make_board_0928(), strip_reasoning(content), nullptr);
        check(grid_to_string(g, 5) == expected, "unterminated trailing <think> dropped");
    }
    {
        std::string only_reasoning = "<think>\nI need to examine every cell...\n";
        check(strip_reasoning(only_reasoning).empty(),
              "reasoning-only response strips to empty");
    }

    // 5. Without the fix, the contaminated content would NOT match (guard that
    //    the raw reconcile really is corrupted, proving the fix is necessary).
    {
        std::string content =
            "<think>\nRow one is TEA maybe SELIG hmm ONSSO CUP\n</think>\n" + answer;
        Grid g_raw = reconcile(make_board_0928(), content, nullptr);  // NO strip
        check(grid_to_string(g_raw, 5) != expected,
              "unstripped content is corrupted (fix is required)");
    }

    std::cout << "\n==== " << (g_failures == 0 ? "ALL PASS" : "FAILURES") << ": "
              << g_failures << " failed ====\n";
    return g_failures == 0 ? 0 : 1;
}
