#pragma once
#include "position.h"
#include <string>

namespace NNUE {
    constexpr int INPUTS = 768;
    constexpr int HIDDEN = 256;
    constexpr int QA = 255;
    constexpr int QB = 64;
    constexpr int SCALE = 400;

    bool load(const std::string& path);
    void unload();
    bool active();

    int evaluate(const Position& pos);
}
