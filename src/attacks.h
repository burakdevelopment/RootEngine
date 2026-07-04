#pragma once
#include "types.h"
#include "bitboard.h"

namespace Attacks {

    extern Bitboard PawnAttacks[COLOR_NB][64];
    extern Bitboard KnightAttacks[64];
    extern Bitboard KingAttacks[64];

    extern Magic RookMagics[64];
    extern Magic BishopMagics[64];

    void init();

    inline Bitboard pawn(Color c, Square sq)  { return PawnAttacks[c][sq]; }
    inline Bitboard knight(Square sq)         { return KnightAttacks[sq]; }
    inline Bitboard king(Square sq)           { return KingAttacks[sq]; }

    inline Bitboard bishop(Square sq, Bitboard occ) {
        const Magic& m = BishopMagics[sq];
        return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
    }

    inline Bitboard rook(Square sq, Bitboard occ) {
        const Magic& m = RookMagics[sq];
        return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
    }

    inline Bitboard queen(Square sq, Bitboard occ) {
        return bishop(sq, occ) | rook(sq, occ);
    }
}
