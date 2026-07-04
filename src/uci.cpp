#include "uci.h"
#include "search.h"
#include "movegen.h"
#include "evaluation.h"
#include "book.h"
#include "tt.h"
#include "nnue.h"
#include "datagen.h"
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <fstream>

namespace UCI {

    static Position pos;
    static std::thread search_thread;

    static bool        own_book = true;
    static std::string book_file = "book.bin";
    static std::string eval_file = "root.nnue";
    static int         hash_mb = 64;

    static void stop_and_join() {
        Search::stop_flag = true;
        if (search_thread.joinable())
            search_thread.join();
        Search::stop_flag = false;
    }

    Move parse_move(Position& p, const std::string& move_str) {
        MoveList list;
        MoveGen::generate_all_moves(p, list);
        for (int i = 0; i < list.size(); ++i) {
            Move m = list.get(i);
            if (m.to_string() != move_str) continue;
            p.make_move(m);
            bool legal = p.is_legal_now();
            p.unmake_move(m);
            if (legal) return m;
        }
        return Move();
    }

    static const char* BenchFens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bq1rk1/ppp1bppp/2n2n2/3pp3/8/2NPPN2/PPP1BPPP/R1BQ1RK1 w - - 0 8",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "r2q1rk1/1bp1bppp/p1np1n2/1p2p3/4P3/1BPP1N1P/PP3PP1/RNBQR1K1 w - - 0 11",
        "2rq1rk1/pb2bppp/1p2pn2/n7/2pP4/P1N1PN2/BP1B1PPP/R2Q1RK1 w - - 0 12",
        "8/8/1p3kp1/p1p2p1p/P1P2P1P/1P4K1/8/8 w - - 0 40",
        "8/5pk1/6p1/3Q4/8/6PK/5P2/3q4 w - - 4 55",
        "r1b2rk1/2q1b1pp/p2ppn2/1p6/3QP3/1BN1B3/PPP3PP/R4RK1 w - - 0 15",
        "6k1/5p2/6p1/8/7p/8/6PP/6K1 b - - 0 40",
    };

    void bench(int depth) {
        uint64_t total_nodes = 0;
        auto t0 = std::chrono::steady_clock::now();

        Position bp;
        for (const char* fen : BenchFens) {
            TT::clear();
            bp.set_fen(fen);
            Search::Limits lim;
            lim.depth = depth;
            Search::Result r = Search::run_sync(bp, lim);
            total_nodes += r.nodes;
            std::cout << "bench pos: " << r.nodes << " nodes, best "
                      << r.best_move.to_string() << ", depth " << r.depth << "\n";
        }

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        uint64_t nps = ms > 0 ? total_nodes * 1000 / static_cast<uint64_t>(ms) : total_nodes;
        std::cout << "Bench: " << total_nodes << " nodes " << nps << " nps" << std::endl;
    }

    static void handle_position(std::istringstream& ss) {
        std::string token;
        ss >> token;

        if (token == "startpos") {
            pos.set_startpos();
            ss >> token;
        } else if (token == "fen") {
            std::string fen, part;
            int parts = 0;
            while (ss >> part && part != "moves" && parts < 6) {
                fen += part + " ";
                parts++;
            }
            pos.set_fen(fen);
            token = part;
        }

        if (token == "moves") {
            std::string move_str;
            while (ss >> move_str) {
                Move m = parse_move(pos, move_str);
                if (m.is_none()) break;
                pos.make_move(m);
            }
        }
    }

    static void handle_go(std::istringstream& ss) {
        stop_and_join();

        Search::Limits limits;
        std::string token;
        bool has_any = false;

        while (ss >> token) {
            if      (token == "depth")     { ss >> limits.depth; has_any = true; }
            else if (token == "movetime")  { ss >> limits.movetime; has_any = true; }
            else if (token == "wtime")     { ss >> limits.wtime; has_any = true; }
            else if (token == "btime")     { ss >> limits.btime; has_any = true; }
            else if (token == "winc")      { ss >> limits.winc; }
            else if (token == "binc")      { ss >> limits.binc; }
            else if (token == "movestogo") { ss >> limits.movestogo; }
            else if (token == "nodes")     { ss >> limits.nodes; has_any = true; }
            else if (token == "infinite")  { limits.infinite = true; has_any = true; }
        }
        if (!has_any) limits.movetime = 3000;

        if (own_book && !limits.infinite) {
            Move bm = Book::probe(pos, book_file);
            if (!bm.is_none()) {
                std::cout << "info string book move" << std::endl;
                std::cout << "bestmove " << bm.to_string() << std::endl;
                return;
            }
        }

        search_thread = std::thread(Search::start, pos, limits);
    }

    static void handle_setoption(std::istringstream& ss) {
        std::string token, name, value;
        ss >> token;
        while (ss >> token && token != "value")
            name += (name.empty() ? "" : " ") + token;
        while (ss >> token)
            value += (value.empty() ? "" : " ") + token;

        if (name == "Hash") {
            try { hash_mb = std::stoi(value); } catch (...) { return; }
            if (hash_mb < 1) hash_mb = 1;
            if (hash_mb > 4096) hash_mb = 4096;
            TT::init(static_cast<size_t>(hash_mb));
        } else if (name == "Threads") {
            int t = 1;
            try { t = std::stoi(value); } catch (...) { return; }
            Search::set_threads(t);
        } else if (name == "OwnBook") {
            own_book = (value == "true" || value == "True" || value == "1");
        } else if (name == "BookFile") {
            book_file = value;
        } else if (name == "UseNNUE") {
            bool use = (value == "true" || value == "True" || value == "1");
            if (use) {
                if (NNUE::load(eval_file))
                    std::cout << "info string NNUE yuklendi: " << eval_file << std::endl;
                else
                    std::cout << "info string NNUE yuklenemedi (" << eval_file
                              << "), klasik degerlendirme kullanilacak" << std::endl;
            } else {
                NNUE::unload();
            }
        } else if (name == "EvalFile") {
            eval_file = value;
            if (NNUE::active()) {
                if (NNUE::load(eval_file))
                    std::cout << "info string NNUE yuklendi: " << eval_file << std::endl;
                else
                    std::cout << "info string NNUE yuklenemedi: " << eval_file << std::endl;
            }
        }
    }

    void loop() {
        pos.set_startpos();

        std::string line;
        while (std::getline(std::cin, line)) {

            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();

            std::istringstream ss(line);
            std::string cmd;
            ss >> cmd;

            if (cmd == "quit") {
                stop_and_join();
                break;
            }
            else if (cmd == "uci") {
                std::cout << "id name RootEngine 3.0\n";
                std::cout << "id author sh3llexpl01t\n";
                std::cout << "option name Hash type spin default 64 min 1 max 4096\n";
                std::cout << "option name Threads type spin default 1 min 1 max 64\n";
                std::cout << "option name OwnBook type check default true\n";
                std::cout << "option name BookFile type string default book.bin\n";
                std::cout << "option name UseNNUE type check default false\n";
                std::cout << "option name EvalFile type string default root.nnue\n";
                std::cout << "uciok" << std::endl;
            }
            else if (cmd == "isready") {
                std::cout << "readyok" << std::endl;
            }
            else if (cmd == "setoption") {
                stop_and_join();
                handle_setoption(ss);
            }
            else if (cmd == "ucinewgame") {
                stop_and_join();
                TT::clear();
                pos.set_startpos();
            }
            else if (cmd == "position") {
                stop_and_join();
                handle_position(ss);
            }
            else if (cmd == "go") {
                handle_go(ss);
            }
            else if (cmd == "stop") {
                stop_and_join();
            }
            else if (cmd == "bench") {
                stop_and_join();
                int depth = 12;
                ss >> depth;
                bench(depth);
            }
            else if (cmd == "datagen") {

                stop_and_join();
                uint64_t count = 100000;
                std::string out = "data.csv";
                int threads = 1;
                uint64_t npm = 5000;
                ss >> count >> out >> threads >> npm;
                Datagen::run(count, out, threads, npm);
            }
            else if (cmd == "perft") {
                stop_and_join();
                int depth = 5;
                ss >> depth;
                auto t0 = std::chrono::steady_clock::now();
                uint64_t n = MoveGen::perft(pos, depth);
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                std::cout << "perft(" << depth << ") = " << n
                          << "  (" << ms << " ms)" << std::endl;
            }
            else if (cmd == "divide") {
                stop_and_join();
                int depth = 2;
                ss >> depth;
                MoveList list;
                MoveGen::generate_all_moves(pos, list);
                uint64_t total = 0;
                for (int i = 0; i < list.size(); ++i) {
                    Move m = list.get(i);
                    pos.make_move(m);
                    if (pos.is_legal_now()) {
                        uint64_t n = (depth <= 1) ? 1 : MoveGen::perft(pos, depth - 1);
                        total += n;
                        std::cout << m.to_string() << ": " << n << "\n";
                    }
                    pos.unmake_move(m);
                }
                std::cout << "toplam: " << total << std::endl;
            }
            else if (cmd == "d" || cmd == "print") {
                pos.print_board();
            }
            else if (cmd == "eval") {
                std::cout << "eval (stm): " << Evaluation::evaluate(pos) << " cp"
                          << (NNUE::active() ? " [NNUE]" : " [HCE]") << std::endl;
            }
        }
        stop_and_join();
    }
}
