# Wend Solver

A C++ desktop app that solves **Wend** word puzzles. A grid (up to 15×15) has
letters placed on cells with some cells empty (holes); the solver finds a set of
independent word paths that together visit every filled cell exactly once, where
each path spells a valid English word.

Built with Dear ImGui + GLFW/OpenGL. Includes a clipboard-import feature: paste a
screenshot of a puzzle (**Ctrl+V**) and the grid auto-fills via OpenCV board
detection plus a local vision-language model for letter reading.

![Wend Solver reading a puzzle from a screenshot and displaying solutions](docs/wend_demo_screenshot.jpg)

## Features
- 15×15 interactive grid editor (click, type, arrow keys, Del/Esc).
- Min/max word-length and comma-separated word-length hint inputs
  (auto-filled from the screenshot on Ctrl+V import — see below).
- Multithreaded brute-force solver; solutions stream to the UI as found.
- Solution browsing with per-word grid coloring (Up/Down to cycle).
- **Clipboard screenshot import** (Ctrl+V) — OpenCV geometry + local VLM OCR.

## Build & Run
See **[BUILD.md](BUILD.md)** for full prerequisites (Visual Studio 2022, OpenCV,
LM Studio) and commands. This repo uses a git submodule for the word list, so
clone recursively:

```powershell
git clone --recurse-submodules https://github.com/mdnpascual/wend-solver.git
# already cloned? run: git submodule update --init --recursive
```

In short, from the repo root:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B build
& $cmake --build build --config Release --parallel 8
.\build\Release\wend_solver.exe
```

## Project layout
```
src/            application + pipeline sources
include/        headers
assets/         box-template.png (hint-square reference); words.txt is copied here at build time
sample_inputs/  example puzzle screenshots (used by the import test)
tools/          standalone tests & diagnostics
third_party/    english-words submodule (dwyl/english-words word list)
.kiro/specs/    feature specs (see clipboard-import.md)
```

---

## Clipboard-import OCR test

`tools/test_clipboard_import.cpp` is an integration test for the screenshot-import
pipeline. It runs the real pipeline on every image in `sample_inputs/`:

```
screenshot → board_vision::analyze → llm_client::read_board → reconciled Grid
```

and compares each reconciled grid against a **hand-verified expected grid**. The
expected grids live in a data file — `tools/expected_grids.txt` — so you can
re-run the test (or add new sample images) without touching or recompiling any
code.

### Prerequisites
The test exercises the actual vision model, so it needs the same runtime setup as
the app's Ctrl+V feature:

- **LM Studio** running with the `qwen3-vl-8b-instruct` model loaded
  (default endpoint `http://localhost:1234`). See [BUILD.md](BUILD.md).
- **OpenCV** prebuilt SDK (default `C:/opencv/opencv/build`, version 4.10.0 →
  `opencv_world4100.dll`).

### Build the test
From the repo root, in a Developer PowerShell (or after running `vcvars64.bat`):

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cl /nologo /std:c++17 /EHsc /I include /I C:\opencv\opencv\build\include ^
  tools\test_clipboard_import.cpp src\board_vision.cpp src\llm_client.cpp src\grid.cpp ^
  /Fe:build\test_clipboard_import.exe /Fo:build\ ^
  /link /LIBPATH:C:\opencv\opencv\build\x64\vc16\lib opencv_world4100.lib winhttp.lib"

# The test binary needs the OpenCV runtime DLL alongside it:
Copy-Item C:\opencv\opencv\build\x64\vc16\bin\opencv_world4100.dll build\ -Force
```

> Adjust the OpenCV version suffix (`4100`) and paths if your SDK differs.

### Run the test
```powershell
.\build\test_clipboard_import.exe
```

Expected output ends with:

```
==== SUMMARY: 6 passed, 0 failed ====
```

Each board prints its reconciled grid, any low-confidence `[warn]` lines, and
`PASS`/`FAIL`. The process exit code is `0` when all boards pass, non-zero
otherwise (handy for scripting).

### Command-line arguments
All optional; defaults shown:

```
test_clipboard_import.exe [sample_inputs_dir] [expected_file] [base_url] [model]
                           sample_inputs        tools/expected_grids.txt
                                                http://localhost:1234  qwen3-vl-8b-instruct
```

Run from the repo root so the default relative paths resolve.

### Expected-grids file format (`tools/expected_grids.txt`)
Plain text, edited by hand — no rebuild needed:

- Blank lines and lines starting with `#` are ignored (comments).
- A line starting with `@` names an image file, relative to the sample-inputs
  directory, e.g. `@Screenshot 2026-09-26 082703.png`.
- The lines that follow (until the next `@` or end of file) are that board's
  rows — one row per line, using an **uppercase letter** for a letter cell and
  `.` for a hole/empty cell. Each row's length and the number of rows must equal
  the board's N.

Example:
```
@Screenshot 2026-09-26 082703.png
EMI.CPI
POC.ITC
GCR.TUC
RSOOPRI
AP...EN
SH.CITE
CI...AM
```

**To add a new sample:** drop the screenshot into `sample_inputs/`, append a new
`@`-block with its hand-verified grid to `tools/expected_grids.txt`, and re-run
the test. No recompile required.

---

## Word-length hint test

`tools/test_word_lengths.cpp` covers the **word-length hint detection** stage —
the rows of gray squares below the board that encode each word's length. On
Ctrl+V import these are detected (`board_vision::detect_word_lengths`) and used to
pre-fill the solver's **Word lengths hint** field and the **Min/Max word length**
inputs.

Unlike the OCR test, this one is **pure OpenCV geometry — no LM Studio required**.
It runs template matching against the bundled `assets/box-template.png`, then for
every image in `sample_inputs/` asserts the detected breakdown matches a
hand-verified table **and** sums to the board's filled-cell count.

### Build the test
From the repo root, in a Developer PowerShell (or after running `vcvars64.bat`):

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cl /nologo /std:c++17 /EHsc /I include /I C:\opencv\opencv\build\include ^
  tools\test_word_lengths.cpp src\board_vision.cpp src\grid.cpp ^
  /Fe:build\test_word_lengths.exe /Fo:build\ ^
  /link /LIBPATH:C:\opencv\opencv\build\x64\vc16\lib opencv_world4100.lib"

# The test binary needs the OpenCV runtime DLL alongside it:
Copy-Item C:\opencv\opencv\build\x64\vc16\bin\opencv_world4100.dll build\ -Force
```

> Adjust the OpenCV version suffix (`4100`) and paths if your SDK differs.

### Run the test
```powershell
.\build\test_word_lengths.exe
```

Expected output:

```
[PASS] Screenshot 2026-09-22 070256.png: lengths=[6,7,8] sum=21 filled=21
[PASS] Screenshot 2026-09-23 173824.png: lengths=[4,5,6,8] sum=23 filled=23
[PASS] Screenshot 2026-09-24 101711.png: lengths=[5,6,8,9] sum=28 filled=28
[PASS] Screenshot 2026-09-25 112800.png: lengths=[4,5,7,8,9] sum=33 filled=33
[PASS] Screenshot 2026-09-26 082703.png: lengths=[5,7,8,9,10] sum=39 filled=39
[PASS] Screenshot 2026-09-27 010811.png: lengths=[6,7,8,9,10] sum=40 filled=40

6 passed, 0 failed
```

The exit code is `0` when all boards pass, non-zero otherwise.

### Command-line arguments
Both optional; defaults shown:

```
test_word_lengths.exe [sample_inputs_dir] [template_png]
                       sample_inputs        assets/box-template.png
```

Run from the repo root so the default relative paths resolve. The expected
breakdowns are compiled into the test (`kExpected` in `tools/test_word_lengths.cpp`);
add a new entry there when you add a sample image.

---

## strip_reasoning / reconcile unit test

`tools/test_strip_reasoning.cpp` is a **pure-logic** test (no LM Studio, no image
work) guarding the letter-parsing layer of `llm_client`. It covers
`strip_reasoning` — which removes `<think>/<thinking>/<reason>/<reasoning>` blocks
a reasoning-capable model may emit inline — and `reconcile`. One case proves that
unstripped reasoning content corrupts the grid, i.e. the guard is necessary.

### Build & run
From the repo root, in a Developer PowerShell:

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" && cl /nologo /std:c++17 /EHsc /I include /I C:\opencv\opencv\build\include ^
  tools\test_strip_reasoning.cpp src\llm_client.cpp src\board_vision.cpp src\grid.cpp ^
  /Fe:build\test_strip_reasoning.exe /Fo:build\ ^
  /link /LIBPATH:C:\opencv\opencv\build\x64\vc16\lib opencv_world4100.lib winhttp.lib"

.\build\test_strip_reasoning.exe
```

Ends with `==== ALL PASS: 0 failed ====`; exit code `0` on success.
