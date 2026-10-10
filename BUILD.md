# Wend Solver — Build & Run Guide

## Prerequisites

- **Visual Studio 2022** (Community or higher) with the **Desktop development with C++** workload
- **Git** (used by CMake FetchContent to download dependencies)
- **Python 3** (used by the glad dependency generator — bundled with VS2022 or install separately)
- **Internet access** on first build (FetchContent downloads ImGui, GLFW, and glad automatically)
- **OpenCV** prebuilt SDK — required by the clipboard-import feature (see below)
- **LM Studio** — required at runtime for the clipboard-import feature (see below)

CMake is bundled with Visual Studio 2022 and does not need a separate install. The commands below use its full path.

---

## OpenCV (clipboard-import dependency)

The clipboard-import feature uses OpenCV for board detection and cell segmentation.
The build links against a **prebuilt OpenCV SDK**.

| Detail | Value |
|---|---|
| **Expected path** | `C:/opencv/opencv/build` (contains `OpenCVConfig.cmake`) |
| **Tested version** | OpenCV 4.10.0, `vc16` (VS2019 ABI — compatible with VS2022) |
| **Runtime DLL** | `opencv_world4100.dll` — copied next to the executable at build time |

If your OpenCV SDK lives elsewhere, override the path at configure time:

```powershell
& $cmake -S . -B build -DOpenCV_DIR="D:/path/to/opencv/build"
```

> Download prebuilt Windows binaries from [opencv.org/releases](https://opencv.org/releases/)
> and extract to `C:\opencv` (or point `-DOpenCV_DIR` at your location).

---

## LM Studio (clipboard-import runtime dependency)

Reading letters from a pasted screenshot is done by a **local vision-language model**
served by [LM Studio](https://lmstudio.ai/). The app manages the server and model
lifecycle automatically on startup.

| Detail | Value |
|---|---|
| **Required model** | `qwen3-vl-8b-instruct` (download inside LM Studio) |
| **CLI** | `lms` (bundled with LM Studio, resolved from `%USERPROFILE%\.lmstudio\bin\lms.exe`) |
| **API endpoint** | `http://localhost:1234` (LM Studio default) |

On launch the app will, in the background: start the LM Studio server if it is not
running, then load `qwen3-vl-8b-instruct` if it is not already resident. The grid
editor works without LM Studio — only the Ctrl+V clipboard import needs it.

---

## Word List

The solver requires a plain-text English word list to function.

| Detail | Value |
|---|---|
| **Expected path (runtime)** | `assets/words.txt`, next to the executable (copied at build time) |
| **Format** | One word per line, letters only (a–z), no punctuation or numbers |
| **Source** | [dwyl/english-words](https://github.com/dwyl/english-words) — `words_alpha.txt`, vendored as a **git submodule** at `third_party/english-words` |

The word list is **not** committed to this repo. It comes from the pinned
`dwyl/english-words` submodule, so you must initialize submodules after cloning:

```powershell
# when cloning fresh:
git clone --recurse-submodules <repo-url>

# or, if you already cloned without it:
git submodule update --init --recursive
```

> At build time CMake copies `third_party/english-words/words_alpha.txt` to
> `assets/words.txt` next to the executable (e.g. `build\Release\assets\words.txt`).
> If the submodule is missing, CMake fails at configure time with instructions.
> The standalone diagnostics in `tools/` accept the word-list path as their first
> argument, e.g. `.\mini_test.exe third_party\english-words\words_alpha.txt`.

---

## Build

Open a **PowerShell** terminal in the repo root and run:

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# Configure (first time or after CMakeLists.txt changes)
& $cmake -S . -B build

# Build
& $cmake --build build --config Release --parallel 8
```

> The first build downloads ImGui, GLFW, and glad via FetchContent — this requires internet access and takes a few extra minutes.  
> Subsequent builds are incremental and fast.

The executable is output to:
```
build\Release\wend_solver.exe
```

---

## Run

```powershell
.\build\Release\wend_solver.exe
```

Or navigate to the folder and double-click `wend_solver.exe`.

> The executable expects `assets\words.txt` to be in an `assets\` folder **next to the exe**.  
> The build step copies it there automatically. If the dictionary fails to load, a warning is printed to the console and the solver will refuse to start until the file is present.

---

## Clipboard Import (Ctrl+V)

Paste a screenshot of a Wend puzzle to auto-fill the grid instead of typing letters.

1. Copy a screenshot of the puzzle board to the clipboard.
2. Press **Ctrl+V** anywhere in the app (outside a text field).
3. The grid is disabled while the image is read; the status banner shows progress.
4. On success the grid auto-fills. **Review it, then press Solve** — it does not solve automatically.

Requirements: OpenCV (build time) and LM Studio with `qwen3-vl-8b-instruct` (runtime),
both described in the Prerequisites section above. The vision-model status is shown at
the top of the grid panel; the first paste may wait for the model to finish loading.

---

## Rebuild from Scratch

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

Remove-Item -Recurse -Force build
& $cmake -S . -B build
& $cmake --build build --config Release --parallel 8
```
