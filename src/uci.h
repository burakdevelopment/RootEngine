#pragma once
#include "position.h"
#include <string>

namespace UCI {

    Move parse_move(Position& pos, const std::string& move_str);

    void bench(int depth = 12);

    void loop();
}
