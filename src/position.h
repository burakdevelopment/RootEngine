#pragma once
#include "types.h"
#include "bitboard.h"
#include "move.h"
#include <string>

enum CastlingRights {
    NO_CASTLING = 0,
    WHITE_OO    = 1,
    WHITE_OOO   = 2,
    BLACK_OO    = 4,
    BLACK_OOO   = 8,
    ALL_CASTLING = 15
};

constexpr int MAX_HISTORY = 2048;

class Position {
private:
    struct State {
        Square    ep_square;
        int       castling_rights;
        PieceType captured_piece;
        int       halfmove_clock;
        uint64_t  key;
    };
    State st[MAX_HISTORY];
    int game_ply = 0;

    Bitboard piece_bb[COLOR_NB][PIECE_TYPE_NB];
    Bitboard color_bb[COLOR_NB];
    uint8_t  board[64];

    Color    side_to_move;
    Square   en_passant;
    int      castle_rights;
    int      halfmove_clock;
    uint64_t hash_key;

    void put_piece(Color c, PieceType pt, Square sq);
    void remove_piece(Color c, PieceType pt, Square sq);
    uint64_t compute_key() const;
    Square adjust_ep(Square ep_candidate) const;

public:
    Position();
    void clear();
    void set_fen(const std::string& fen);
    void set_startpos();
    std::string to_fen() const;
    void print_board() const;

    void make_move(Move m);
    void unmake_move(Move m);
    void make_null_move();
    void unmake_null_move();

    bool is_square_attacked(Square sq, Color attacker) const;
    Bitboard attackers_to(Square sq, Bitboard occ) const;
    bool in_check() const;
    bool is_legal_now() const;

    bool is_repetition() const;
    bool is_draw() const;
    bool has_non_pawn_material(Color c) const;

    inline PieceType piece_on(Square sq) const { return static_cast<PieceType>(board[sq]); }
    inline Color color_on(Square sq) const {
        return Bitboards::check_bit(color_bb[WHITE], sq) ? WHITE
             : Bitboards::check_bit(color_bb[BLACK], sq) ? BLACK : COLOR_NB;
    }
    inline Square king_sq(Color c) const { return Bitboards::lsb(piece_bb[c][KING]); }

    inline Bitboard pieces(Color c, PieceType pt) const { return piece_bb[c][pt]; }
    inline Bitboard pieces(Color c) const { return color_bb[c]; }
    inline Bitboard all_pieces() const { return color_bb[WHITE] | color_bb[BLACK]; }
    inline Bitboard empty_squares() const { return ~all_pieces(); }
    inline Color turn() const { return side_to_move; }
    inline Square en_passant_sq() const { return en_passant; }
    inline int castling_rights() const { return castle_rights; }
    inline int get_halfmove() const { return halfmove_clock; }
    inline uint64_t key() const { return hash_key; }
};
