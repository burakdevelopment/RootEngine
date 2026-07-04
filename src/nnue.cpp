#include "nnue.h"
#include "bitboard.h"
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>

namespace NNUE {

    static std::vector<int16_t> ft_weights;
    static std::vector<int16_t> ft_bias;
    static std::vector<int16_t> out_weights;
    static int32_t out_bias = 0;
    static bool is_active = false;

    bool active() { return is_active; }
    void unload() { is_active = false; }

    bool load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;

        char magic[4];
        uint32_t version = 0, hidden = 0;
        f.read(magic, 4);
        f.read(reinterpret_cast<char*>(&version), 4);
        f.read(reinterpret_cast<char*>(&hidden), 4);
        if (!f || std::memcmp(magic, "RTNN", 4) != 0 || version != 1 || hidden != HIDDEN)
            return false;

        ft_weights.resize(static_cast<size_t>(INPUTS) * HIDDEN);
        ft_bias.resize(HIDDEN);
        out_weights.resize(2 * HIDDEN);

        f.read(reinterpret_cast<char*>(ft_weights.data()), ft_weights.size() * 2);
        f.read(reinterpret_cast<char*>(ft_bias.data()), ft_bias.size() * 2);
        f.read(reinterpret_cast<char*>(out_weights.data()), out_weights.size() * 2);
        f.read(reinterpret_cast<char*>(&out_bias), 4);
        if (!f) { is_active = false; return false; }

        is_active = true;
        return true;
    }

    static inline int feature_index(Color persp, Color c, PieceType pt, Square sq) {
        int rel = (c == persp) ? 0 : 1;
        int s = (persp == WHITE) ? sq : (sq ^ 56);
        return rel * 384 + pt * 64 + s;
    }

    static inline int crelu(int v) {
        return v < 0 ? 0 : (v > QA ? QA : v);
    }

    int evaluate(const Position& pos) {
        alignas(64) int32_t acc[2][HIDDEN];

        for (int j = 0; j < HIDDEN; ++j) {
            acc[WHITE][j] = ft_bias[j];
            acc[BLACK][j] = ft_bias[j];
        }

        for (int ci = WHITE; ci <= BLACK; ++ci) {
            Color c = static_cast<Color>(ci);
            for (int pt = PAWN; pt <= KING; ++pt) {
                Bitboard bb = pos.pieces(c, static_cast<PieceType>(pt));
                while (bb) {
                    Square sq = Bitboards::pop_lsb(bb);
                    const int16_t* row_w = &ft_weights[static_cast<size_t>(
                        feature_index(WHITE, c, static_cast<PieceType>(pt), sq)) * HIDDEN];
                    const int16_t* row_b = &ft_weights[static_cast<size_t>(
                        feature_index(BLACK, c, static_cast<PieceType>(pt), sq)) * HIDDEN];
                    for (int j = 0; j < HIDDEN; ++j) {
                        acc[WHITE][j] += row_w[j];
                        acc[BLACK][j] += row_b[j];
                    }
                }
            }
        }

        Color us = pos.turn();
        Color them = ~us;
        const int16_t* w_us = &out_weights[0];
        const int16_t* w_them = &out_weights[HIDDEN];

        int64_t sum = out_bias;
        for (int j = 0; j < HIDDEN; ++j) {
            sum += static_cast<int64_t>(crelu(acc[us][j])) * w_us[j];
            sum += static_cast<int64_t>(crelu(acc[them][j])) * w_them[j];
        }

        int score = static_cast<int>(sum * SCALE / (QA * QB));

        if (score > MATE_IN_MAX - 1) score = MATE_IN_MAX - 1;
        if (score < -(MATE_IN_MAX - 1)) score = -(MATE_IN_MAX - 1);
        return score;
    }
}
