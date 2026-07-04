#include "attacks.h"

namespace Attacks {

    Bitboard PawnAttacks[COLOR_NB][64];
    Bitboard KnightAttacks[64];
    Bitboard KingAttacks[64];

    Magic RookMagics[64];
    Magic BishopMagics[64];

    static Bitboard RookTable[102400];
    static Bitboard BishopTable[5248];

    static Bitboard sliding_attack(PieceType pt, Square sq, Bitboard occ) {
        Bitboard attacks = 0;
        const int rook_dirs[4][2]   = { {1,0}, {-1,0}, {0,1}, {0,-1} };
        const int bishop_dirs[4][2] = { {1,1}, {1,-1}, {-1,1}, {-1,-1} };
        const auto& dirs = (pt == ROOK) ? rook_dirs : bishop_dirs;

        for (int d = 0; d < 4; ++d) {
            int r = rank_of(sq) + dirs[d][0];
            int f = file_of(sq) + dirs[d][1];
            while (r >= 0 && r <= 7 && f >= 0 && f <= 7) {
                Square s = static_cast<Square>(r * 8 + f);
                attacks |= Bitboards::square_bb(s);
                if (occ & Bitboards::square_bb(s)) break;
                r += dirs[d][0];
                f += dirs[d][1];
            }
        }
        return attacks;
    }

    static uint64_t rng_state = 0x1234567887654321ULL;
    static uint64_t rand64() {
        rng_state ^= rng_state >> 12;
        rng_state ^= rng_state << 25;
        rng_state ^= rng_state >> 27;
        return rng_state * 2685821657736338717ULL;
    }
    static uint64_t sparse_rand() { return rand64() & rand64() & rand64(); }

    static void init_magics(PieceType pt, Magic magics[], Bitboard table[]) {
        Bitboard occupancy[4096], reference[4096];
        int      epoch[4096] = {};
        int      cnt = 0;
        size_t   offset = 0;

        for (int s = 0; s < 64; ++s) {
            Square sq = static_cast<Square>(s);

            Bitboard edges = ((Bitboards::rank_bb(0) | Bitboards::rank_bb(7)) & ~Bitboards::rank_bb(rank_of(sq)))
                           | ((Bitboards::file_bb(0) | Bitboards::file_bb(7)) & ~Bitboards::file_bb(file_of(sq)));

            Magic& m = magics[s];
            m.mask   = sliding_attack(pt, sq, 0) & ~edges;
            m.shift  = 64 - Bitboards::popcount(m.mask);
            m.attacks = &table[offset];

            int size = 0;
            Bitboard b = 0;
            do {
                occupancy[size] = b;
                reference[size] = sliding_attack(pt, sq, b);
                size++;
                b = (b - m.mask) & m.mask;
            } while (b);

            offset += size;

            for (int i = 0; i < size; ) {
                for (m.magic = 0; Bitboards::popcount((m.magic * m.mask) >> 56) < 6; )
                    m.magic = sparse_rand();

                for (++cnt, i = 0; i < size; ++i) {
                    unsigned idx = static_cast<unsigned>(((occupancy[i] & m.mask) * m.magic) >> m.shift);
                    if (epoch[idx] < cnt) {
                        epoch[idx] = cnt;
                        m.attacks[idx] = reference[i];
                    } else if (m.attacks[idx] != reference[i])
                        break;
                }
            }
        }
    }

    void init() {
        for (int s = 0; s < 64; ++s) {
            Square sq = static_cast<Square>(s);
            Bitboard b = Bitboards::square_bb(sq);

            PawnAttacks[WHITE][s] = ((b << 7) & ~Bitboards::FILE_H_BB) | ((b << 9) & ~Bitboards::FILE_A_BB);
            PawnAttacks[BLACK][s] = ((b >> 7) & ~Bitboards::FILE_A_BB) | ((b >> 9) & ~Bitboards::FILE_H_BB);

            Bitboard n = 0;
            n |= (b << 17) & ~Bitboards::FILE_A_BB;
            n |= (b << 15) & ~Bitboards::FILE_H_BB;
            n |= (b << 10) & ~(Bitboards::FILE_A_BB | Bitboards::file_bb(1));
            n |= (b << 6)  & ~(Bitboards::FILE_H_BB | Bitboards::file_bb(6));
            n |= (b >> 17) & ~Bitboards::FILE_H_BB;
            n |= (b >> 15) & ~Bitboards::FILE_A_BB;
            n |= (b >> 10) & ~(Bitboards::FILE_H_BB | Bitboards::file_bb(6));
            n |= (b >> 6)  & ~(Bitboards::FILE_A_BB | Bitboards::file_bb(1));
            KnightAttacks[s] = n;

            Bitboard k = 0;
            k |= (b << 8) | (b >> 8);
            k |= ((b << 1) | (b << 9) | (b >> 7)) & ~Bitboards::FILE_A_BB;
            k |= ((b >> 1) | (b >> 9) | (b << 7)) & ~Bitboards::FILE_H_BB;
            KingAttacks[s] = k;
        }

        init_magics(ROOK, RookMagics, RookTable);
        init_magics(BISHOP, BishopMagics, BishopTable);
    }
}
