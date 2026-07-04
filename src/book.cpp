#include "book.h"
#include "movegen.h"
#include <fstream>
#include <vector>
#include <random>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Book {

    static std::string resolve_path(const std::string& path) {
        std::ifstream test(path, std::ios::binary);
        if (test.is_open()) return path;
#ifdef _WIN32
        char buf[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::string exe_dir(buf, len);
            size_t slash = exe_dir.find_last_of("\\/");
            if (slash != std::string::npos) {
                std::string candidate = exe_dir.substr(0, slash + 1) + path;
                std::ifstream t2(candidate, std::ios::binary);
                if (t2.is_open()) return candidate;
            }
        }
#endif
        return path;
    }

    struct BookMove {
        uint16_t move;
        uint16_t weight;
    };

    static uint64_t read_u64(const unsigned char* p) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
        return v;
    }
    static uint16_t read_u16(const unsigned char* p) {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }

    static bool read_entry(std::ifstream& file, long long index, uint64_t& key, BookMove& bm) {
        unsigned char buf[16];
        file.seekg(index * 16, std::ios::beg);
        file.read(reinterpret_cast<char*>(buf), 16);
        if (!file) { file.clear(); return false; }
        key = read_u64(buf);
        bm.move = read_u16(buf + 8);
        bm.weight = read_u16(buf + 10);
        return true;
    }

    static Move to_legal_move(Position& pos, uint16_t poly) {
        int to_file   = poly & 7;
        int to_rank   = (poly >> 3) & 7;
        int from_file = (poly >> 6) & 7;
        int from_rank = (poly >> 9) & 7;
        int promo     = (poly >> 12) & 7;

        Square from = static_cast<Square>(from_rank * 8 + from_file);
        Square to   = static_cast<Square>(to_rank * 8 + to_file);

        if (pos.piece_on(from) == KING) {
            if (from == SQ_E1 && to == SQ_H1) to = SQ_G1;
            else if (from == SQ_E1 && to == SQ_A1) to = SQ_C1;
            else if (from == SQ_E8 && to == SQ_H8) to = SQ_G8;
            else if (from == SQ_E8 && to == SQ_A8) to = SQ_C8;
        }

        MoveList list;
        MoveGen::generate_all_moves(pos, list);
        for (int i = 0; i < list.size(); ++i) {
            Move m = list.get(i);
            if (m.from_sq() != from || m.to_sq() != to) continue;
            if (m.is_promotion()) {

                if (promo == 0) continue;
                if (m.promo_piece() != static_cast<PieceType>(KNIGHT + promo - 1)) continue;
            } else if (promo != 0) continue;

            pos.make_move(m);
            bool legal = pos.is_legal_now();
            pos.unmake_move(m);
            if (legal) return m;
        }
        return Move();
    }

    Move probe(Position& pos, const std::string& book_path) {
        std::ifstream file(resolve_path(book_path), std::ios::binary);
        if (!file.is_open())
            return Move();

        file.seekg(0, std::ios::end);
        long long file_size = file.tellg();
        file.clear();
        long long num_entries = file_size / 16;
        if (num_entries <= 0) return Move();

        uint64_t hash = pos.key();

        long long low = 0, high = num_entries - 1, found = -1;
        uint64_t key;
        BookMove bm;

        while (low <= high) {
            long long mid = low + (high - low) / 2;
            if (!read_entry(file, mid, key, bm)) return Move();
            if (key < hash) low = mid + 1;
            else { if (key == hash) found = mid; high = mid - 1; }
        }
        if (found < 0) return Move();

        std::vector<BookMove> candidates;
        for (long long i = found; i < num_entries; ++i) {
            if (!read_entry(file, i, key, bm) || key != hash) break;
            if (bm.weight > 0) candidates.push_back(bm);
        }
        if (candidates.empty()) return Move();

        static std::mt19937 rng(std::random_device{}());
        uint16_t best_weight = 0;
        for (const auto& c : candidates)
            if (c.weight > best_weight) best_weight = c.weight;

        std::vector<uint16_t> best_moves;
        for (const auto& c : candidates)
            if (c.weight == best_weight) best_moves.push_back(c.move);

        uint16_t chosen = best_moves[rng() % best_moves.size()];
        return to_legal_move(pos, chosen);
    }
}
