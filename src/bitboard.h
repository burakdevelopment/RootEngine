#pragma once
#include "types.h"

namespace Bitboards {

    constexpr Bitboard square_bb(Square s) { return 1ULL << s; }

    inline void set_bit(Bitboard& bb, Square s) { bb |= square_bb(s); }
    inline void pop_bit(Bitboard& bb, Square s) { bb &= ~square_bb(s); }
    constexpr bool check_bit(Bitboard bb, Square s) { return (bb & square_bb(s)) != 0; }

    inline int popcount(Bitboard bb) { return __builtin_popcountll(bb); }

    inline Square lsb(Bitboard bb) { return static_cast<Square>(__builtin_ctzll(bb)); }

    inline Square pop_lsb(Bitboard& bb) {
        Square s = lsb(bb);
        bb &= bb - 1;
        return s;
    }

    constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
    constexpr Bitboard FILE_H_BB = 0x8080808080808080ULL;
    constexpr Bitboard RANK_1_BB = 0x00000000000000FFULL;

    constexpr Bitboard file_bb(int f) { return FILE_A_BB << f; }
    constexpr Bitboard rank_bb(int r) { return RANK_1_BB << (8 * r); }
}
