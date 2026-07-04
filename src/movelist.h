#pragma once
#include "move.h"

class MoveList {
private:
    Move moves[256];
    int scores_[256];
    int count;

public:
    MoveList() : count(0) {}

    inline void add(Move m) {
        if (count < 256)
            moves[count++] = m;
    }

    inline int size() const { return count; }
    inline Move get(int index) const { return moves[index]; }

    inline void set_score(int index, int s) { scores_[index] = s; }
    inline int score(int index) const { return scores_[index]; }

    inline Move pick(int index) {
        int best = index;
        for (int i = index + 1; i < count; ++i)
            if (scores_[i] > scores_[best])
                best = i;
        if (best != index) {
            Move tm = moves[index]; moves[index] = moves[best]; moves[best] = tm;
            int ts = scores_[index]; scores_[index] = scores_[best]; scores_[best] = ts;
        }
        return moves[index];
    }
};
