# Spec: Clipboard Screenshot Import

## Goal
Let the user paste a screenshot of a Wend puzzle directly into the app (Ctrl+V) and have the grid auto-populated, instead of typing letters manually.

## Status: IMPLEMENTED
The letter/hole import is built and verified end-to-end on both 5×5 and 6×6 sample boards. The as-built design below **supersedes the original template-matching plan** — see "Design change" for why.

---

## As-Built Architecture

```
Clipboard (Win32 CF_DIB / CF_DIBV5)
    │  read_clipboard_image()  [src/clipboard_image.cpp]
    ▼
cv::Mat (decoded image, BGR)
    │
    ▼
Board geometry  [board_vision::analyze — src/board_vision.cpp]
  1. Board detection: segment the warm tan border (R−B > 20 & R > 90),
     take the largest contour's bounding box.
  2. Grid size N: cluster bright letter-glyph centroids (threshold > 130,
     dilate, contour centroids) along X and Y; N = max of the cluster
     counts and the spacing-derived estimates. Auto-handles 5×5..7×7.
  3. Cell classification: per cell, HOLE if the cell-background estimate > 55,
     else LETTER if center bright-pixel count ≥ 15, else EMPTY. The background
     estimate is the *minimum* of the four corner-patch medians — see
     "Perimeter-letter classification fix" below.
    │  (N + authoritative hole/letter map)
    ▼
Letter OCR  [llm_client::read_board — src/llm_client.cpp]
  - Local vision-language model via LM Studio (qwen3-vl-8b-instruct),
    OpenAI-compatible /v1/chat/completions over WinHTTP.
  - PER-CELL reads: for each cell board_vision classified as a letter, the cell
    is cropped (8% inset to exclude grid lines), upscaled 4x → PNG → base64, and
    sent with a "reply with only the single letter" prompt (max_tokens=8). See
    "Per-cell OCR" below for why whole-board transcription was abandoned.
  - board_vision's letter/hole map is authoritative; the model only ever reads
    single glyphs into cells already known to contain a letter.
    │
    ▼
Grid population  [ClipboardImporter — src/clipboard_import.cpp]
  - read_board writes letters straight into the Grid at the known cell
    positions (no row-alignment step can misfire). Result posted to the UI
    thread; UI fills Grid and refreshes.
```

## Design change (why not template matching / Tesseract)
- **Tesseract** (spec Option B) was prototyped: with grid-line-aware preprocessing it reached ~90% but had a persistent blind spot on clean **R** glyphs (and occasionally Q/B) in this UI font. Not viable for 100%.
- **Template matching** (spec Option A) was skipped — the VLM route reached 100% on both sample grids and was simpler to make robust.
- **Local VLM (chosen):** `qwen3-vl-8b-instruct` reads glyphs correctly when
  asked one cell at a time (see "Per-cell OCR" — whole-board transcription was
  tried first and abandoned). Cost: a runtime dependency on a local LM Studio
  server (managed automatically at startup).

## Layout-guided prompt + reconcile guardrail (accuracy fix)
The first prompt asked the VLM for the full N×N grid and let it decide where the
holes were (`letters + '.' for holes`). This surfaced a real failure: on boards
with **wide (double/triple) holes**, the VLM would miscount cells in a row —
either dropping real letters or "reading through" a hole and hallucinating a
glyph. Because `reconcile` fills the VLM's letters left-to-right into our
authoritative letter cells, a row that came back short shifted every later cell
and left trailing `?` placeholders.

Concrete case (`Screenshot 2026-09-26`, 2nd-to-last row = `S H . C I T E`):
board_vision correctly found 6 letter cells + 1 hole, but the VLM returned only
`S H . T E` — it dropped the **C** and **I** either side of the hole. reconcile
then produced `S H . T E ? ?`. board_vision was 100% correct; the VLM was the
failing stage.

**Fix — two parts:**
1. **Feed the VLM our layout.** The prompt now includes board_vision's
   authoritative per-cell layout (`?` = read a letter here, `#` = hole, `_` =
   empty) *and* an explicit per-row letter count with the columns each letter
   lives in. The model no longer has to count holes itself (its known weakness);
   it just fills the letters into the known slots and is told never to invent a
   glyph for a `#` cell. This alone fixed every sample board.
2. **Guardrail in `reconcile`.** For each row we compare the VLM's letter count
   against our authoritative count. On any mismatch we record a human-readable
   warning in `OcrResult.warnings`; the importer turns a non-empty warning list
   into a low-confidence status ("Grid loaded, but some cells are uncertain —
   please review") instead of silently shipping misaligned/`?` cells.

**Verification:** the integration test `tools/test_clipboard_import.cpp` runs the
full pipeline on every image in `sample_inputs/` and compares the reconciled grid
against hand-verified expected grids stored in `tools/expected_grids.txt`. All
six samples pass with zero warnings. See the repo `README.md` for how to build
and run it.

## Perimeter-letter classification fix (7×7 boards)
A new sample (`Screenshot 2026-09-27`, a 7×7 board) exposed a cell-classification
bug. Board detection and N-detection were correct, but eight *perimeter letter*
cells (`I H Q O Y A C`) were misclassified as holes, so the pipeline saw only 32
letters / 17 holes instead of the true 40 / 9.

**Root cause.** `classify()` estimated a cell's background by pooling the pixels
of all four corner patches into one median, then treating `median > 55` as a
hole. On this board the letters sit tight against the warm **tan border**, which
bled into two of the four corners of each perimeter cell (patch median ~121).
Pooling those bright pixels with the two genuine dark-background corners (~45)
produced an intermediate median (~63) that cleared the 55 hole threshold —
turning real letters into holes. Earlier 5×5/6×6 samples had enough inset that
the border never reached the corner patches, so the bug stayed hidden.

**Fix.** The background estimate is now the **minimum of the four per-corner
medians** rather than the pooled median. Rationale: a genuine hole is uniformly
light in *every* corner, so even its darkest corner stays light (> 55); a letter
cell always has at least one corner showing the true dark cell background (~45),
so the minimum correctly reads low. This is robust to the border contaminating
one or more corners.

**Verification.** No regression on the five original samples (identical letter/
hole counts, all tests still pass); the new 7×7 sample now classifies exactly
(4 corner holes + central plus = 9 holes, 40 letters) and reads correctly
end-to-end through the VLM.

## Per-cell OCR (whole-board transcription abandoned)
A new 5×5 sample (`Screenshot 2026-09-28`, grid `T.E.A / SELIG / .TEE. / ONSSO /
C.U.P`) broke the whole-board OCR approach even though board_vision was 100%
correct (N=5, 19 letters, 6 single-width holes, exact layout).

**Root cause — two compounding VLM failures on one image:**
1. **Misread.** Asked to transcribe the whole board at once, `qwen3-vl-8b-instruct`
   deterministically (temp 0) read row 3 as row 4's letters (`# O N S O` instead
   of `# T E E #`) and shifted the rest — a genuine perception error on a dense
   board, not a formatting issue.
2. **Reasoning spiral.** The model noticed its own output disagreed with the
   authoritative layout we fed it and launched into 20,000+ characters of
   self-correcting prose — blowing past LM Studio's 8192-token output cap so it
   never emitted a final grid. The reasoning was emitted as **plain prose inline
   in `content`** (no `<think>` tags, empty `reasoning_content`), so the old
   `reconcile` parsed the prose lines as grid rows and produced garbage.

Neither `enable_thinking=false` (not honored by this model) nor a hard
`max_tokens` cap fixed it — with a cap the model simply emitted its *misread*
grid and stopped. The perception error itself had to be removed.

**Fix — per-cell OCR.** Since board_vision already knows every letter cell's
exact rectangle, `read_board` now crops each letter cell (8% inset to drop grid
lines), upscales 4×, and asks the model for **one letter** (`max_tokens=8`). A
lone glyph gives the model nothing to overthink: no misalignment is possible
(each answer maps to a known cell), no reasoning spiral occurs, and each request
stays far below the context limit. The reconcile row-alignment step is gone from
the pipeline (the function is retained only for the unit test).

**Defensive guard — `strip_reasoning`.** Every model reply still passes through
`strip_reasoning`, which removes `<think>/<thinking>/<reason>/<reasoning>` blocks
(case-insensitive, including an unterminated trailing block) before the letter is
extracted — cheap insurance against a reasoning model that wraps its output.

**Verification.** The integration test now covers all **seven** samples and
passes with the real VLM (7 passed, 0 failed); the 2026-09-28 board reads exactly
`T.E.A / SELIG / .TEE. / ONSSO / C.U.P`. A pure-logic unit test
(`tools/test_strip_reasoning.cpp`, no LM Studio) covers `strip_reasoning` +
`reconcile` and proves unstripped reasoning content would corrupt the grid.

## Files (as built)
```
include/board_vision.h      src/board_vision.cpp      — board detection, N, cell classification,
                                                        word-length hint detection (detect_word_lengths)
include/llm_client.h        src/llm_client.cpp         — WinHTTP client, base64, JSON, reconcile
include/lmstudio.h          src/lmstudio.cpp           — server/model lifecycle manager
include/clipboard_import.h  src/clipboard_import.cpp   — orchestration + async worker
                            src/clipboard_image.cpp    — Win32 CF_DIB → cv::Mat
assets/box-template.png                                — reference hint square (35×35), bundled next to exe
```

## LM Studio lifecycle (startup)
Managed by `LMStudio` (background thread), state `Uninitialized → Starting → Ready/Error`:
1. Probe server (`GET /v1/models`); if down, launch `lms server start` (hidden) and poll.
2. Check the model is **resident** via `lms ps` (NOT `/v1/models`, which lists all *downloaded* models — loading an already-resident model spawns a wasteful duplicate instance like `qwen3-vl-8b-instruct:2`).
3. Load with `lms load qwen3-vl-8b-instruct --gpu max -y` only if not resident.
- `lms.exe` resolved from `%USERPROFILE%\.lmstudio\bin\lms.exe`, else PATH.

## Trigger + UI gating
- Ctrl+V in `UI::handle_clipboard_import()`, gated on `!io.WantTextInput` so pasting into text fields still works.
- If the model is not ready yet, the image is **queued**; the worker waits for readiness, then processes.
- While Queued/Processing the grid is dimmed and non-interactive; Solve/Clear All are disabled.
- On success the grid auto-fills and prior solve state is cleared. **It does NOT auto-solve** — status prompts the user to review then press Solve.

## CMake
- `find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)`, `OpenCV_DIR` defaults to `C:/opencv/opencv/build` (override with `-DOpenCV_DIR`).
- Links `${OpenCV_LIBS}` and `winhttp`; post-build copies `opencv_world<ver>.dll` next to the exe.

## Error cases (implemented)
| Condition | Message |
|---|---|
| No image on clipboard | "Clipboard does not contain an image" |
| Image too small | "Image too small to detect a grid" |
| Grid not found | "Could not detect a puzzle grid in the image" |
| Model unavailable | "Vision model unavailable: …" |
| OCR empty/failed | "Could not read letters from grid cells" |

---

## Word-length hint detection — IMPLEMENTED

Wend screenshots show the **word-length breakdown** as rows of small gray rounded
squares below the board (above the Undo button). Each **row = one word**, and the
**number of squares in that row = that word's length**. This is detected during the
same Ctrl+V import and used to pre-fill the solver's inputs:
- **Word-length hint** field: the comma-separated lengths (e.g. `6,7,8`).
- **Min word length**: the shortest row.
- **Max word length**: the longest row.

The chosen approach was the spec's template-matching plan. It was first validated in
a Python prototype against all five sample screenshots, then ported to C++. The
fixed-size-square assumption held: the squares render at a **constant ~36px pitch**
across both 5×5 and 6×6 boards, so single-scale matching is sufficient (no pyramid).

### As-built pipeline — `board_vision::detect_word_lengths()`
Pure OpenCV geometry, no VLM. Signature:
`detect_word_lengths(full_screenshot, BoardResult, square_template, bool* confident)`.

1. **Region crop.** Uses the board bounding box (now stored on `BoardResult::box`):
   crops below the board, spanning the board width, extending ~55% of the board
   height downward. The reference square is bundled as `assets/box-template.png`
   (35×35) and loaded at runtime like `words.txt`.
2. **Match.** `cv::matchTemplate` with `TM_CCOEFF_NORMED`, threshold `0.6`.
3. **NMS de-dup.** Greedily keep the highest-scoring hit, discard any other whose
   center is within `0.6 × template width` — collapses each square's match cluster
   to one center. (This was the main correctness risk, per the original notes.)
4. **Row grouping.** Cluster de-duplicated centers by Y (`cluster_1d_groups`, gap
   `0.5 × template height`); each Y-cluster is one word, its member count is that
   word's length. Rows sorted top→bottom.
5. **Button rejection.** Legit hint rows sit at the constant square pitch. A row
   separated from the previous by a gap `> 1.6 × template height` is the Undo/Hint
   button block — it and everything after it are dropped. This cleanly removes the
   phantom trailing row (e.g. the spurious `6` seen on the 5×5 sample).
6. **Confidence.** `*confident` is set true only when the detected lengths sum to
   `BoardResult::filled_count()`.

### Wiring
- `ClipboardImporter::run_pipeline` calls `detect_word_lengths` after OCR (non-fatal:
  detection failure leaves the fields blank rather than aborting the import).
- Results flow to the UI via a new `take_result(Grid&, std::vector<int>&, bool&)`
  overload. `UI::handle_clipboard_import` fills `word_lens_buf_` (CSV) and sets
  `min_len_buf_` / `max_len_buf_` from the row lengths.
- **UX when uncertain:** the fields are pre-filled anyway, but if the lengths don't
  sum to the filled-cell count the status bar says *"Word lengths detected but
  uncertain … please verify"* instead of the normal review prompt.

### Verification
`tools/test_word_lengths.cpp` runs `analyze` + `detect_word_lengths` on every sample
and asserts the detected breakdown matches a hand-verified table **and** sums to the
filled-cell count with `confident == true`. All six samples pass:

```
[PASS] Screenshot 2026-09-22: [6,7,8]        sum=21 filled=21
[PASS] Screenshot 2026-09-23: [4,5,6,8]      sum=23 filled=23
[PASS] Screenshot 2026-09-24: [5,6,8,9]      sum=28 filled=28
[PASS] Screenshot 2026-09-25: [4,5,7,8,9]    sum=33 filled=33
[PASS] Screenshot 2026-09-26: [5,7,8,9,10]   sum=39 filled=39
[PASS] Screenshot 2026-09-27: [6,7,8,9,10]   sum=40 filled=40
6 passed, 0 failed
```

Unlike the OCR integration test, this one needs **no LM Studio** — it is pure
geometry. See `README.md` for the build/run command.
