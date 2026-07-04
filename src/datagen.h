#pragma once
#include <string>
#include <cstdint>

namespace Datagen {
    void run(uint64_t target_positions, const std::string& out_path,
             int threads, uint64_t nodes_per_move);
}
