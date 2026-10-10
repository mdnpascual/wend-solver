#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "dictionary.h"
#include "ui.h"
#include "lmstudio.h"
#include "clipboard_import.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>

// Resolve path to assets/words.txt relative to the executable
static std::string find_words_file() {
    // Try next to exe first (post-build copy)
    std::filesystem::path exe_dir;
    // Common fallback: relative to CWD
    for (auto& candidate : {
        std::string("assets/words.txt"),
        std::string("../assets/words.txt"),
        std::string("../../assets/words.txt")
    }) {
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return "assets/words.txt"; // best guess, will fail with clear error
}

static void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main() {
    // ── Load dictionary ───────────────────────────────────────────────────────
    Dictionary dict;
    std::string words_path = find_words_file();
    int word_count = dict.load(words_path);
    if (word_count < 0) {
        std::fprintf(stderr,
            "Warning: Could not load dictionary from '%s'.\n"
            "Place a words.txt (one word per line) in the assets/ folder next to the executable.\n",
            words_path.c_str());
    } else {
        std::printf("Dictionary loaded: %d words from '%s'\n", word_count, words_path.c_str());
    }

    // ── GLFW init ─────────────────────────────────────────────────────────────
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    const char* glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(2560, 1440, "Wend Solver", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // ── glad ──────────────────────────────────────────────────────────────────
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to initialize glad\n");
        return 1;
    }

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Slightly tweaked style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 0.0f;
    style.ChildRounding    = 4.0f;
    style.FrameRounding    = 4.0f;
    style.GrabRounding     = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.14f, 1.0f);

    // Scale the entire UI to 200%.
    style.ScaleAllSizes(2.0f);
    io.FontGlobalScale = 2.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // ── LM Studio + clipboard import ──────────────────────────────────────────
    // Kick off the vision-model readiness sequence in the background so it is
    // (ideally) ready by the time the user pastes a screenshot.
    LMStudio lm("qwen3-vl-8b-instruct");
    lm.start();
    ClipboardImporter importer(lm);

    // ── UI ────────────────────────────────────────────────────────────────────
    UI ui(dict, lm, importer);

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ui.render();

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
