#include "datagen.h"
#include "search.h"
#include "movegen.h"
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <random>
#include <atomic>
#include <chrono>

namespace Datagen {

    static std::mutex out_mutex;
    static std::atomic<uint64_t> total_written{0};

    struct Sample {
        std::string fen;
        int score_white;
    };

    static bool random_opening(Position& pos, std::mt19937_64& rng) {
        pos.set_startpos();
        int plies = 8 + static_cast<int>(rng() % 2);

        for (int i = 0; i < plies; ++i) {
            MoveList list;
            MoveGen::generate_all_moves(pos, list);

            Move legal[256];
            int n = 0;
            for (int j = 0; j < list.size(); ++j) {
                Move m = list.get(j);
                pos.make_move(m);
                if (pos.is_legal_now()) legal[n++] = m;
                pos.unmake_move(m);
            }
            if (n == 0) return false;
            pos.make_move(legal[rng() % n]);
        }

        Search::Limits ql;
        ql.depth = 4;
        Search::Result qr = Search::run_sync(pos, ql);
        return std::abs(qr.score) <= 300;
    }

    static void worker(uint64_t target, const std::string& out_path,
                       uint64_t nodes_per_move, uint64_t seed) {
        std::mt19937_64 rng(seed);
        Position pos;

        while (total_written.load() < target) {
            if (!random_opening(pos, rng)) continue;

            std::vector<Sample> samples;
            double result_white = 0.5;
            int high_score_streak = 0;
            int ply = 0;
            bool adjudicated = false;

            while (true) {
                if (ply > 400) break;

                MoveList list;
                MoveGen::generate_all_moves(pos, list);
                bool any_legal = false;
                for (int j = 0; j < list.size() && !any_legal; ++j) {
                    Move m = list.get(j);
                    pos.make_move(m);
                    if (pos.is_legal_now()) any_legal = true;
                    pos.unmake_move(m);
                }
                if (!any_legal) {
                    if (pos.in_check())
                        result_white = (pos.turn() == WHITE) ? 0.0 : 1.0;
                    else
                        result_white = 0.5;
                    break;
                }
                if (pos.is_draw()) { result_white = 0.5; break; }

                Search::Limits lim;
                lim.nodes = nodes_per_move;
                Search::Result r = Search::run_sync(pos, lim);
                if (r.best_move.is_none()) { result_white = 0.5; break; }

                int score_white = (pos.turn() == WHITE) ? r.score : -r.score;

                if (std::abs(r.score) >= 1500) {
                    if (++high_score_streak >= 6) {
                        result_white = (score_white > 0) ? 1.0 : 0.0;
                        adjudicated = true;
                    }
                } else high_score_streak = 0;

                if (!pos.in_check() && !r.best_move.is_capture() &&
                    !r.best_move.is_promotion() && std::abs(r.score) < 1200)
                    samples.push_back({ pos.to_fen(), score_white });

                if (adjudicated) break;
                pos.make_move(r.best_move);
                ply++;
            }

            if (samples.size() < 8) continue;

            std::lock_guard<std::mutex> lock(out_mutex);
            std::ofstream out(out_path, std::ios::app);
            for (const auto& s : samples)
                out << s.fen << ";" << s.score_white << ";" << result_white << "\n";
            total_written += samples.size();
        }
    }

    void run(uint64_t target_positions, const std::string& out_path,
             int threads, uint64_t nodes_per_move) {
        if (threads < 1) threads = 1;
        total_written = 0;

        auto t0 = std::chrono::steady_clock::now();
        std::random_device rd;

        std::vector<std::thread> pool;
        for (int i = 0; i < threads; ++i)
            pool.emplace_back(worker, target_positions, out_path,
                              nodes_per_move, (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ i);

        while (total_written.load() < target_positions) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            uint64_t w = total_written.load();
            std::cout << "info string datagen " << w << "/" << target_positions
                      << " pozisyon (" << (ms > 0 ? w * 1000 / ms : 0) << " poz/s)" << std::endl;
            if (w >= target_positions) break;
        }

        for (auto& t : pool) t.join();
        std::cout << "info string datagen tamamlandi: " << total_written.load()
                  << " pozisyon -> " << out_path << std::endl;
    }
}
