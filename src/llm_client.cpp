#include "llm_client.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace llm_client {

// ── Base64 ────────────────────────────────────────────────────────────────────
std::string base64_encode(const unsigned char* data, size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        unsigned n = (data[i] << 16) | (data[i+1] << 8) | data[i+2];
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
    }
    if (i < len) {
        unsigned n = data[i] << 16;
        if (i + 1 < len) n |= data[i+1] << 8;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back((i + 1 < len) ? tbl[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

// ── JSON string escaping ────────────────────────────────────────────────────
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

// ── HTTP POST via WinHTTP ─────────────────────────────────────────────────────
#ifdef _WIN32
static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// Shared WinHTTP request core. `verb` is L"GET" or L"POST". For POST, pass the
// JSON body; for GET pass an empty body.
static bool http_request(const std::wstring& verb, const std::string& url,
                         const std::string& body, std::string& response_out,
                         std::string& error_out, int timeout_ms) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[1024]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 1023;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        error_out = "Invalid API URL"; return false;
    }

    HINTERNET hSession = WinHttpOpen(L"WendSolver/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { error_out = "WinHttpOpen failed"; return false; }
    WinHttpSetTimeouts(hSession, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (hConnect) {
        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, verb.c_str(), path,
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (hRequest) {
            const bool is_post = (verb == L"POST");
            const wchar_t* hdrs = is_post ? L"Content-Type: application/json\r\n"
                                          : WINHTTP_NO_ADDITIONAL_HEADERS;
            DWORD hdr_len = is_post ? (DWORD)-1L : 0;
            BOOL sent = WinHttpSendRequest(hRequest, hdrs, hdr_len,
                is_post ? (LPVOID)body.data() : WINHTTP_NO_REQUEST_DATA,
                is_post ? (DWORD)body.size() : 0,
                is_post ? (DWORD)body.size() : 0, 0);
            if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD status = 0, slen = sizeof(status);
                WinHttpQueryHeaders(hRequest,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);

                std::string resp;
                DWORD avail = 0;
                do {
                    avail = 0;
                    if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
                    if (!avail) break;
                    std::vector<char> buf(avail);
                    DWORD read = 0;
                    if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
                    resp.append(buf.data(), read);
                } while (avail > 0);

                if (status >= 200 && status < 300) {
                    response_out = std::move(resp);
                    ok = true;
                } else {
                    error_out = "HTTP status " + std::to_string(status) + ": " + resp.substr(0, 300);
                }
            } else {
                error_out = "Request failed (is the LM Studio server running?)";
            }
            WinHttpCloseHandle(hRequest);
        } else error_out = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
    } else error_out = "WinHttpConnect failed (server not reachable)";
    WinHttpCloseHandle(hSession);
    return ok;
}

bool http_post_json(const std::string& url, const std::string& body,
                    std::string& response_out, std::string& error_out,
                    int timeout_ms) {
    return http_request(L"POST", url, body, response_out, error_out, timeout_ms);
}

bool http_get(const std::string& url, std::string& response_out,
              std::string& error_out, int timeout_ms) {
    return http_request(L"GET", url, {}, response_out, error_out, timeout_ms);
}
#else
bool http_post_json(const std::string&, const std::string&,
                    std::string&, std::string& error_out, int) {
    error_out = "HTTP not supported on this platform"; return false;
}
bool http_get(const std::string&, std::string&, std::string& error_out, int) {
    error_out = "HTTP not supported on this platform"; return false;
}
#endif

// ── Response parsing ──────────────────────────────────────────────────────────
// Minimal, tolerant extractor: find "content"  :  "...."  and JSON-unescape it.
std::string extract_message_content(const std::string& json) {
    // Locate the "content" key.
    size_t key = json.find("\"content\"");
    if (key == std::string::npos) return {};
    size_t colon = json.find(':', key);
    if (colon == std::string::npos) return {};
    // Skip whitespace to the opening quote.
    size_t i = colon + 1;
    while (i < json.size() && std::isspace((unsigned char)json[i])) ++i;
    if (i >= json.size() || json[i] != '"') return {};
    ++i;  // past opening quote
    std::string out;
    for (; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            char e = json[++i];
            switch (e) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '"': out += '"';  break;
                case '\\':out += '\\'; break;
                case '/': out += '/';  break;
                case 'u': {
                    if (i + 4 < json.size()) {
                        int code = std::stoi(json.substr(i + 1, 4), nullptr, 16);
                        if (code < 0x80) out += (char)code;  // ASCII is all we need
                        i += 4;
                    }
                    break;
                }
                default: out += e; break;
            }
        } else if (c == '"') {
            break;  // closing quote
        } else {
            out += c;
        }
    }
    return out;
}

// ── Reasoning stripping ───────────────────────────────────────────────────────
// Remove <think>...</think> style reasoning blocks. Reasoning-capable models
// emit their chain-of-thought inline in `content`; those prose lines would be
// misread as grid rows by reconcile(). We drop every recognized reasoning block
// (case-insensitive tag match) and — critically — also drop an *unterminated*
// trailing block (a response that starts thinking but is cut off / never closes
// the tag), so no reasoning prose ever reaches the grid parser.
std::string strip_reasoning(const std::string& content) {
    static const char* kTags[] = {"think", "thinking", "reason", "reasoning"};

    std::string s = content;
    // Lowercase copy for case-insensitive tag search; edits are mapped back by
    // index into `s` (tags are ASCII so indices line up 1:1).
    auto lower = [](std::string v) {
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        return v;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        std::string ls = lower(s);
        for (const char* tag : kTags) {
            std::string open  = std::string("<")  + tag + ">";
            std::string close = std::string("</") + tag + ">";
            size_t o = ls.find(open);
            if (o == std::string::npos) continue;
            size_t c = ls.find(close, o + open.size());
            if (c != std::string::npos) {
                s.erase(o, (c + close.size()) - o);           // full block
            } else {
                s.erase(o);                                   // unterminated → drop to end
            }
            changed = true;
            break;  // restart scan on the mutated string
        }
    }

    // Trim leading blank lines left behind by the removal.
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return s.substr(start);
}

// ── Reconciliation ────────────────────────────────────────────────────────────
Grid reconcile(const board_vision::BoardResult& a, const std::string& vlm_text,
               std::vector<std::string>* warnings) {
    using board_vision::CellKind;
    Grid grid;
    grid.clear();

    // Parse VLM rows into lists of letters (ignoring dots/non-alpha).
    std::vector<std::vector<char>> vlm_letters_per_row;
    {
        std::istringstream ss(vlm_text);
        std::string line;
        while (std::getline(ss, line)) {
            // Trim and drop code-fence lines.
            size_t s = line.find_first_not_of(" \t\r`");
            if (s == std::string::npos) continue;
            std::vector<char> letters;
            for (char c : line) {
                if (std::isalpha((unsigned char)c))
                    letters.push_back((char)std::toupper((unsigned char)c));
            }
            if (!letters.empty())
                vlm_letters_per_row.push_back(std::move(letters));
        }
    }

    // Fill row by row using our authoritative hole/letter map.
    for (int r = 0; r < a.n && r < GRID_SIZE; ++r) {
        const std::vector<char>* row_letters =
            (r < (int)vlm_letters_per_row.size()) ? &vlm_letters_per_row[r] : nullptr;

        // Authoritative letter-cell count for this row.
        int want = 0;
        for (int c = 0; c < a.n && c < GRID_SIZE; ++c)
            if (a.cells[r][c] == CellKind::Letter) ++want;

        int got = row_letters ? (int)row_letters->size() : 0;
        if (warnings && got != want) {
            warnings->push_back("row " + std::to_string(r + 1) + ": model returned " +
                                std::to_string(got) + " letter(s), expected " +
                                std::to_string(want));
        }

        size_t li = 0;
        for (int c = 0; c < a.n && c < GRID_SIZE; ++c) {
            if (a.cells[r][c] == CellKind::Letter) {
                char ch = '?';
                if (row_letters && li < row_letters->size())
                    ch = (*row_letters)[li];
                ++li;
                grid.cells[r][c].filled = true;
                grid.cells[r][c].letter = ch;
            } else {
                grid.cells[r][c] = Cell{};  // hole/empty -> unfilled
            }
        }
    }
    return grid;
}

// ── Single-cell OCR ───────────────────────────────────────────────────────────
// Send one cropped, upscaled letter-cell image to the VLM and return the single
// A-Z letter it reads (uppercased), or '\0' on any failure.
//
// Why per-cell instead of whole-board: qwen3-vl-8b, given the entire board and
// asked to transcribe it, misreads dense rows (e.g. it repeatedly read row 3 of
// the 2026-09-28 sample as row 4's letters) and — worse — spirals into thousands
// of tokens of self-correcting prose that blows past the 8192-token output cap
// and never emits a final grid. Board geometry is already known exactly from
// board_vision, so we lean on it: crop each Letter cell and ask only "what is
// this one letter?". A single glyph gives the model nothing to overthink, keeps
// every response tiny (max_tokens small), and reads every sample board correctly.
static char ocr_single_cell(const cv::Mat& cell_bgr, const std::string& base_url,
                            const std::string& model, std::string* err_out) {
    // Upscale 4x for glyph clarity (single cells are small).
    cv::Mat up;
    cv::resize(cell_bgr, up, cv::Size(cell_bgr.cols * 4, cell_bgr.rows * 4),
               0, 0, cv::INTER_CUBIC);

    std::vector<uchar> png;
    if (!cv::imencode(".png", up, png)) {
        if (err_out) *err_out = "Failed to encode cell image";
        return '\0';
    }
    std::string b64 = base64_encode(png.data(), png.size());

    static const char* kPrompt =
        "This image shows exactly one uppercase letter (A-Z) from a word puzzle. "
        "Reply with ONLY that single letter and nothing else. No punctuation, no "
        "explanation, no reasoning.";

    // max_tokens is capped low: the answer is one character, and a hard cap also
    // prevents any accidental reasoning spiral from consuming the context window.
    std::ostringstream jb;
    jb << "{\"model\":\"" << json_escape(model) << "\",\"temperature\":0,"
       << "\"max_tokens\":8,"
       << "\"messages\":[{\"role\":\"user\",\"content\":["
       << "{\"type\":\"text\",\"text\":\"" << json_escape(kPrompt) << "\"},"
       << "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64,"
       << b64 << "\"}}]}]}";

    std::string url = base_url + "/v1/chat/completions";
    std::string response, err;
    if (!http_post_json(url, jb.str(), response, err)) {
        if (err_out) *err_out = err.empty() ? "LLM request failed" : err;
        return '\0';
    }

    std::string content = strip_reasoning(extract_message_content(response));
    for (char c : content) {
        if (std::isalpha((unsigned char)c))
            return (char)std::toupper((unsigned char)c);
    }
    return '\0';
}

// ── Top-level ─────────────────────────────────────────────────────────────────
OcrResult read_board(const cv::Mat& board,
                     const board_vision::BoardResult& analysis,
                     const std::string& base_url,
                     const std::string& model) {
    using board_vision::CellKind;
    OcrResult res;

    const int n = analysis.n;
    if (n < 1 || board.empty()) {
        res.error = "Could not read letters from grid cells (no board)";
        return res;
    }

    // Cell geometry — identical to board_vision::classify so the crops line up
    // exactly with the cells that were classified as letters.
    const double ch = (double)board.rows / n;
    const double cw = (double)board.cols / n;

    Grid grid;
    grid.clear();

    std::ostringstream raw;      // "r,c=X" diagnostics
    int read_count = 0;
    int letter_cells = 0;

    for (int r = 0; r < n && r < GRID_SIZE; ++r) {
        for (int c = 0; c < n && c < GRID_SIZE; ++c) {
            if (analysis.cells[r][c] != CellKind::Letter) {
                grid.cells[r][c] = Cell{};   // hole/empty -> unfilled
                continue;
            }
            ++letter_cells;

            int y0 = (int)(r * ch), y1 = (int)((r + 1) * ch);
            int x0 = (int)(c * cw), x1 = (int)((c + 1) * cw);
            // Inset ~8% to keep the dark grid lines out of the crop.
            int iy = (int)((y1 - y0) * 0.08), ix = (int)((x1 - x0) * 0.08);
            y0 += iy; y1 -= iy; x0 += ix; x1 -= ix;
            y0 = std::max(0, y0); x0 = std::max(0, x0);
            y1 = std::min(board.rows, y1); x1 = std::min(board.cols, x1);
            if (y1 <= y0 || x1 <= x0) {
                res.warnings.push_back("cell (" + std::to_string(r + 1) + "," +
                                       std::to_string(c + 1) + "): empty crop");
                grid.cells[r][c].filled = true;
                grid.cells[r][c].letter = '?';
                continue;
            }

            cv::Mat cell = board(cv::Range(y0, y1), cv::Range(x0, x1)).clone();
            std::string cell_err;
            char letter = ocr_single_cell(cell, base_url, model, &cell_err);

            if (letter == '\0') {
                // Transport-level failure aborts the whole read (server down,
                // etc.); a mere unreadable glyph is recorded as a warning.
                if (!cell_err.empty() && cell_err != "empty") {
                    res.error = cell_err;
                    return res;
                }
                res.warnings.push_back("cell (" + std::to_string(r + 1) + "," +
                                       std::to_string(c + 1) +
                                       "): could not read a letter");
                grid.cells[r][c].filled = true;
                grid.cells[r][c].letter = '?';
            } else {
                grid.cells[r][c].filled = true;
                grid.cells[r][c].letter = letter;
                ++read_count;
            }
            raw << (raw.tellp() > 0 ? " " : "") << (r + 1) << ',' << (c + 1)
                << '=' << (letter ? letter : '?');
        }
    }

    if (read_count == 0) {
        res.error = "Could not read letters from grid cells (empty model response)";
        return res;
    }

    res.raw = raw.str();
    res.grid = grid;
    res.ok = true;
    return res;
}

}  // namespace llm_client
