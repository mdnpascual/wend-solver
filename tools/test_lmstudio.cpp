// Exercise the LMStudio lifecycle manager against the real environment.
#include "lmstudio.h"
#include <cstdio>
#include <thread>
#include <chrono>

const char* state_name(LMStudio::State s) {
    switch (s) {
        case LMStudio::State::Uninitialized: return "Uninitialized";
        case LMStudio::State::Starting:      return "Starting";
        case LMStudio::State::Ready:         return "Ready";
        case LMStudio::State::Error:         return "Error";
    }
    return "?";
}

int main() {
    LMStudio lm("qwen3-vl-8b-instruct");
    lm.start();
    for (int i = 0; i < 60; ++i) {
        auto s = lm.state();
        std::printf("[%2ds] state=%-13s  msg=%s\n", i, state_name(s), lm.status_message().c_str());
        if (s == LMStudio::State::Ready || s == LMStudio::State::Error) break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::printf("FINAL: %s (ready=%d)\n", state_name(lm.state()), lm.is_ready());
    return 0;
}
