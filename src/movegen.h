#pragma once
#include "position.h"
#include "movelist.h"
#include "attacks.h"

namespace MoveGen {

    using Bitboards::pop_lsb;
    using Bitboards::square_bb;

    enum GenMode { GEN_ALL, GEN_CAPTURES };

    template<GenMode Mode>
    inline void generate_pawn_moves(const Position& pos, MoveList& list) {
        Color us = pos.turn();
        Bitboard pawns = pos.pieces(us, PAWN);
        Bitboard empty = pos.empty_squares();
        Bitboard enemies = pos.pieces(~us);
        Square ep_sq = pos.en_passant_sq();

        constexpr Bitboard RANK8 = 0xFF00000000000000ULL;
        constexpr Bitboard RANK1 = 0x00000000000000FFULL;

        if (us == WHITE) {
            Bitboard push1_all = (pawns << 8) & empty;
            Bitboard promo_push = push1_all & RANK8;

            while (promo_push) {
                Square to = pop_lsb(promo_push);
                Square from = static_cast<Square>(to - 8);
                list.add(Move(from, to, PROMO_Q));
                list.add(Move(from, to, PROMO_N));
                list.add(Move(from, to, PROMO_R));
                list.add(Move(from, to, PROMO_B));
            }

            if (Mode == GEN_ALL) {
                Bitboard push1 = push1_all & ~RANK8;
                Bitboard push2 = ((push1_all & 0x0000000000FF0000ULL) << 8) & empty;
                while (push1) {
                    Square to = pop_lsb(push1);
                    list.add(Move(static_cast<Square>(to - 8), to, QUIET_MOVE));
                }
                while (push2) {
                    Square to = pop_lsb(push2);
                    list.add(Move(static_cast<Square>(to - 16), to, DOUBLE_PAWN_PUSH));
                }
            }

            Bitboard att_left  = (pawns << 7) & enemies & ~Bitboards::FILE_H_BB;
            Bitboard att_right = (pawns << 9) & enemies & ~Bitboards::FILE_A_BB;

            while (att_left) {
                Square to = pop_lsb(att_left);
                Square from = static_cast<Square>(to - 7);
                if (rank_of(to) == 7) {
                    list.add(Move(from, to, PROMO_Q_CAP));
                    list.add(Move(from, to, PROMO_N_CAP));
                    list.add(Move(from, to, PROMO_R_CAP));
                    list.add(Move(from, to, PROMO_B_CAP));
                } else
                    list.add(Move(from, to, CAPTURE));
            }
            while (att_right) {
                Square to = pop_lsb(att_right);
                Square from = static_cast<Square>(to - 9);
                if (rank_of(to) == 7) {
                    list.add(Move(from, to, PROMO_Q_CAP));
                    list.add(Move(from, to, PROMO_N_CAP));
                    list.add(Move(from, to, PROMO_R_CAP));
                    list.add(Move(from, to, PROMO_B_CAP));
                } else
                    list.add(Move(from, to, CAPTURE));
            }

            if (ep_sq != SQ_NONE) {
                Bitboard ep_bb = square_bb(ep_sq);
                if ((pawns << 7) & ep_bb & ~Bitboards::FILE_H_BB)
                    list.add(Move(static_cast<Square>(ep_sq - 7), ep_sq, EP_CAPTURE));
                if ((pawns << 9) & ep_bb & ~Bitboards::FILE_A_BB)
                    list.add(Move(static_cast<Square>(ep_sq - 9), ep_sq, EP_CAPTURE));
            }
        } else {
            Bitboard push1_all = (pawns >> 8) & empty;
            Bitboard promo_push = push1_all & RANK1;

            while (promo_push) {
                Square to = pop_lsb(promo_push);
                Square from = static_cast<Square>(to + 8);
                list.add(Move(from, to, PROMO_Q));
                list.add(Move(from, to, PROMO_N));
                list.add(Move(from, to, PROMO_R));
                list.add(Move(from, to, PROMO_B));
            }

            if (Mode == GEN_ALL) {
                Bitboard push1 = push1_all & ~RANK1;
                Bitboard push2 = ((push1_all & 0x0000FF0000000000ULL) >> 8) & empty;
                while (push1) {
                    Square to = pop_lsb(push1);
                    list.add(Move(static_cast<Square>(to + 8), to, QUIET_MOVE));
                }
                while (push2) {
                    Square to = pop_lsb(push2);
                    list.add(Move(static_cast<Square>(to + 16), to, DOUBLE_PAWN_PUSH));
                }
            }

            Bitboard att_left  = (pawns >> 9) & enemies & ~Bitboards::FILE_H_BB;
            Bitboard att_right = (pawns >> 7) & enemies & ~Bitboards::FILE_A_BB;

            while (att_left) {
                Square to = pop_lsb(att_left);
                Square from = static_cast<Square>(to + 9);
                if (rank_of(to) == 0) {
                    list.add(Move(from, to, PROMO_Q_CAP));
                    list.add(Move(from, to, PROMO_N_CAP));
                    list.add(Move(from, to, PROMO_R_CAP));
                    list.add(Move(from, to, PROMO_B_CAP));
                } else
                    list.add(Move(from, to, CAPTURE));
            }
            while (att_right) {
                Square to = pop_lsb(att_right);
                Square from = static_cast<Square>(to + 7);
                if (rank_of(to) == 0) {
                    list.add(Move(from, to, PROMO_Q_CAP));
                    list.add(Move(from, to, PROMO_N_CAP));
                    list.add(Move(from, to, PROMO_R_CAP));
                    list.add(Move(from, to, PROMO_B_CAP));
                } else
                    list.add(Move(from, to, CAPTURE));
            }

            if (ep_sq != SQ_NONE) {
                Bitboard ep_bb = square_bb(ep_sq);
                if ((pawns >> 9) & ep_bb & ~Bitboards::FILE_H_BB)
                    list.add(Move(static_cast<Square>(ep_sq + 9), ep_sq, EP_CAPTURE));
                if ((pawns >> 7) & ep_bb & ~Bitboards::FILE_A_BB)
                    list.add(Move(static_cast<Square>(ep_sq + 7), ep_sq, EP_CAPTURE));
            }
        }
    }

    template<GenMode Mode>
    inline void generate_piece_moves(const Position& pos, MoveList& list) {
        Color us = pos.turn();
        Bitboard enemies = pos.pieces(~us);
        Bitboard occ = pos.all_pieces();
        Bitboard targets = (Mode == GEN_CAPTURES) ? enemies : ~pos.pieces(us);

        for (int pt = KNIGHT; pt <= KING; ++pt) {
            Bitboard bb = pos.pieces(us, static_cast<PieceType>(pt));
            while (bb) {
                Square from = pop_lsb(bb);
                Bitboard attacks;
                switch (pt) {
                    case KNIGHT: attacks = Attacks::knight(from); break;
                    case BISHOP: attacks = Attacks::bishop(from, occ); break;
                    case ROOK:   attacks = Attacks::rook(from, occ); break;
                    case QUEEN:  attacks = Attacks::queen(from, occ); break;
                    default:     attacks = Attacks::king(from); break;
                }
                attacks &= targets;
                while (attacks) {
                    Square to = pop_lsb(attacks);
                    list.add(Move(from, to, Bitboards::check_bit(enemies, to) ? CAPTURE : QUIET_MOVE));
                }
            }
        }
    }

    inline void generate_castling(const Position& pos, MoveList& list) {
        Color us = pos.turn();
        Bitboard occ = pos.all_pieces();

        if (us == WHITE) {
            if ((pos.castling_rights() & WHITE_OO) &&
                !(occ & (square_bb(SQ_F1) | square_bb(SQ_G1))) &&
                !pos.is_square_attacked(SQ_E1, BLACK) &&
                !pos.is_square_attacked(SQ_F1, BLACK) &&
                !pos.is_square_attacked(SQ_G1, BLACK))
                list.add(Move(SQ_E1, SQ_G1, KING_CASTLE));
            if ((pos.castling_rights() & WHITE_OOO) &&
                !(occ & (square_bb(SQ_B1) | square_bb(SQ_C1) | square_bb(SQ_D1))) &&
                !pos.is_square_attacked(SQ_E1, BLACK) &&
                !pos.is_square_attacked(SQ_D1, BLACK) &&
                !pos.is_square_attacked(SQ_C1, BLACK))
                list.add(Move(SQ_E1, SQ_C1, QUEEN_CASTLE));
        } else {
            if ((pos.castling_rights() & BLACK_OO) &&
                !(occ & (square_bb(SQ_F8) | square_bb(SQ_G8))) &&
                !pos.is_square_attacked(SQ_E8, WHITE) &&
                !pos.is_square_attacked(SQ_F8, WHITE) &&
                !pos.is_square_attacked(SQ_G8, WHITE))
                list.add(Move(SQ_E8, SQ_G8, KING_CASTLE));
            if ((pos.castling_rights() & BLACK_OOO) &&
                !(occ & (square_bb(SQ_B8) | square_bb(SQ_C8) | square_bb(SQ_D8))) &&
                !pos.is_square_attacked(SQ_E8, WHITE) &&
                !pos.is_square_attacked(SQ_D8, WHITE) &&
                !pos.is_square_attacked(SQ_C8, WHITE))
                list.add(Move(SQ_E8, SQ_C8, QUEEN_CASTLE));
        }
    }

    inline void generate_all_moves(const Position& pos, MoveList& list) {
        generate_pawn_moves<GEN_ALL>(pos, list);
        generate_piece_moves<GEN_ALL>(pos, list);
        generate_castling(pos, list);
    }

    inline void generate_captures(const Position& pos, MoveList& list) {
        generate_pawn_moves<GEN_CAPTURES>(pos, list);
        generate_piece_moves<GEN_CAPTURES>(pos, list);
    }

    inline uint64_t perft(Position& pos, int depth) {
        if (depth == 0) return 1;
        MoveList list;
        generate_all_moves(pos, list);
        uint64_t nodes = 0;
        for (int i = 0; i < list.size(); ++i) {
            Move m = list.get(i);
            pos.make_move(m);
            if (pos.is_legal_now())
                nodes += (depth == 1) ? 1 : perft(pos, depth - 1);
            pos.unmake_move(m);
        }
        return nodes;
    }
}
