cmake_minimum_required(VERSION 3.20)
project(wend_solver VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies via FetchContent ─────────────────────────────────────────────
include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

# Dear ImGui (docking branch)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6-docking
    GIT_SHALLOW    TRUE
)

# glad (OpenGL 3.3 core, generated loader bundled here)
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v0.1.36
    GIT_SHALLOW    TRUE
)
set(GLAD_PROFILE      "core"  CACHE STRING "" FORCE)
set(GLAD_API          "gl=3.3" CACHE STRING "" FORCE)
set(GLAD_GENERATOR    "c"     CACHE STRING "" FORCE)
set(GLAD_EXTENSIONS   ""      CACHE STRING "" FORCE)
set(GLAD_SPEC         "gl"    CACHE STRING "" FORCE)
set(GLAD_ALL_EXTENSIONS OFF   CACHE BOOL   "" FORCE)
set(GLAD_NO_LOADER    OFF     CACHE BOOL   "" FORCE)

FetchContent_MakeAvailable(glfw imgui glad)

# ── OpenCV (prebuilt SDK) ──────────────────────────────────────────────────────
# Used by the clipboard-import feature for board detection & cell segmentation.
# Point OpenCV_DIR at a prebuilt SDK's cmake config dir. Default to the known
# local install; override with -DOpenCV_DIR=... if yours lives elsewhere.
if(NOT DEFINED OpenCV_DIR)
    set(OpenCV_DIR "C:/opencv/opencv/build" CACHE PATH "OpenCV cmake config dir")
endif()
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
message(STATUS "OpenCV ${OpenCV_VERSION} found: ${OpenCV_INCLUDE_DIRS}")

# ── ImGui as a static library ─────────────────────────────────────────────────
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)

# ── Wend Solver executable ────────────────────────────────────────────────────
add_executable(wend_solver
    src/main.cpp
    src/grid.cpp
    src/dictionary.cpp
    src/solver.cpp
    src/ui.cpp
    src/clipboard_import.cpp
    src/clipboard_image.cpp
    src/board_vision.cpp
    src/llm_client.cpp
    src/lmstudio.cpp
)

target_include_directories(wend_solver PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(wend_solver PRIVATE
    imgui_lib
    glfw
    glad
    ${OpenCV_LIBS}
)

# Platform-specific OpenGL linkage
if(WIN32)
    target_link_libraries(wend_solver PRIVATE opengl32 winhttp)
elseif(APPLE)
    find_library(OPENGL_LIB OpenGL)
    target_link_libraries(wend_solver PRIVATE ${OPENGL_LIB})
else()
    target_link_libraries(wend_solver PRIVATE GL)
endif()

# Thread support
find_package(Threads REQUIRED)
target_link_libraries(wend_solver PRIVATE Threads::Threads)

# Copy assets next to the executable
add_custom_command(TARGET wend_solver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:wend_solver>/assets
    COMMENT "Copying assets..."
)

# ── Word list (from the dwyl/english-words git submodule) ──────────────────────
# The solver loads `assets/words.txt` next to the executable at runtime. Rather
# than committing the 4 MB list, it comes from the pinned submodule and is copied
# into place at build time as words.txt.
set(_words_src "${CMAKE_SOURCE_DIR}/third_party/english-words/words_alpha.txt")
if(NOT EXISTS "${_words_src}")
    message(FATAL_ERROR
        "Word list not found at ${_words_src}\n"
        "The dwyl/english-words submodule is not initialized. Run:\n"
        "    git submodule update --init --recursive")
endif()
add_custom_command(TARGET wend_solver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_words_src}"
        $<TARGET_FILE_DIR:wend_solver>/assets/words.txt
    COMMENT "Copying word list (words_alpha.txt -> assets/words.txt)..."
)

# Copy the OpenCV world DLL next to the executable (Windows runtime dependency).
if(WIN32)
    # OpenCV_LIB_PATH points at the vc16/lib dir; DLLs live in the sibling bin dir.
    # Build the expected DLL name from the version (e.g. opencv_world4100.dll).
    string(REPLACE "." "" OPENCV_DLL_VER "${OpenCV_VERSION}")
    set(_ocv_bin "${OpenCV_DIR}/x64/vc16/bin")
    add_custom_command(TARGET wend_solver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_ocv_bin}/opencv_world${OPENCV_DLL_VER}$<$<CONFIG:Debug>:d>.dll"
            $<TARGET_FILE_DIR:wend_solver>
        COMMENT "Copying OpenCV runtime DLL..."
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(wend_solver PRIVATE /W3)
else()
    target_compile_options(wend_solver PRIVATE -Wall -Wextra -Wpedantic)
endif()
