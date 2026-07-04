#pragma once
#include "position.h"
#include <atomic>
#include <cstdint>

namespace Search {

    struct Limits {
        int      depth = MAX_PLY - 1;
        int64_t  movetime = -1;
        int64_t  wtime = -1, btime = -1;
        int64_t  winc = 0, binc = 0;
        int      movestogo = 0;
        uint64_t nodes = 0;
        bool     infinite = false;
    };

    struct Result {
        Move best_move;
        int  score = 0;
        uint64_t nodes = 0;
        int  depth = 0;
    };

    extern std::atomic<bool> stop_flag;

    void init();
    void set_threads(int n);
    int  get_threads();

    void start(Position pos, Limits limits);

    Result run_sync(Position& pos, const Limits& limits);
}
