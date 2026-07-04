#pragma once
#include "move.h"
#include <cstdint>
#include <cstddef>

namespace TT {
    enum Flag : uint8_t { NONE = 0, ALPHA, BETA, EXACT };

    struct Entry {
        uint64_t key;
        int16_t  score;
        uint16_t move;
        uint8_t  depth;
        uint8_t  flag;
    };

    void init(size_t mb);
    void clear();
    void store(uint64_t key, int depth, int score, Flag flag, Move best_move);
    const Entry* probe(uint64_t key);
    int hashfull();
}
