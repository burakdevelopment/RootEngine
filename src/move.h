#pragma once
#include "types.h"
#include <string>

enum MoveFlags {
    QUIET_MOVE = 0,
    DOUBLE_PAWN_PUSH = 1,
    KING_CASTLE = 2,
    QUEEN_CASTLE = 3,
    CAPTURE = 4,
    EP_CAPTURE = 5,
    PROMO_N = 8,
    PROMO_B = 9,
    PROMO_R = 10,
    PROMO_Q = 11,
    PROMO_N_CAP = 12,
    PROMO_B_CAP = 13,
    PROMO_R_CAP = 14,
    PROMO_Q_CAP = 15
};

class Move {
private:
    uint16_t data;

public:
    Move() : data(0) {}

    Move(Square from, Square to, int flags = QUIET_MOVE) {
        data = static_cast<uint16_t>(from | (to << 6) | (flags << 12));
    }

    inline Square from_sq() const { return static_cast<Square>(data & 0x3F); }
    inline Square to_sq() const   { return static_cast<Square>((data >> 6) & 0x3F); }
    inline int flags() const      { return (data >> 12) & 0x0F; }
    inline uint16_t raw() const   { return data; }
    inline bool is_none() const   { return data == 0; }

    inline bool is_capture() const {
        int f = flags();
        return f == CAPTURE || f == EP_CAPTURE || f >= PROMO_N_CAP;
    }
    inline bool is_promotion() const { return flags() >= PROMO_N; }
    inline bool is_quiet() const { return !is_capture() && !is_promotion(); }

    inline PieceType promo_piece() const {
        return static_cast<PieceType>(KNIGHT + (flags() & 0x03));
    }

    inline bool operator==(const Move& other) const { return data == other.data; }
    inline bool operator!=(const Move& other) const { return data != other.data; }

    std::string to_string() const {
        if (data == 0) return "0000";
        std::string s = "";
        s += char('a' + file_of(from_sq()));
        s += char('1' + rank_of(from_sq()));
        s += char('a' + file_of(to_sq()));
        s += char('1' + rank_of(to_sq()));
        if (is_promotion())
            s += "nbrq"[flags() & 0x03];
        return s;
    }
};
