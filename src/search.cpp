#include "search.h"
#include "movegen.h"
#include "evaluation.h"
#include "tt.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <thread>
#include <vector>
#include <memory>

namespace Search {

    std::atomic<bool> stop_flag{false};

    static int num_threads = 1;
    static int lmr_table[64][64];

    void init() {
        for (int d = 1; d < 64; ++d)
            for (int m = 1; m < 64; ++m)
                lmr_table[d][m] = static_cast<int>(0.5 + std::log(d) * std::log(m) / 2.25);
    }

    void set_threads(int n) {
        num_threads = std::clamp(n, 1, 64);
    }
    int get_threads() { return num_threads; }

    struct Shared {
        std::atomic<bool> abort{false};
        std::atomic<uint64_t> nodes{0};
        std::chrono::steady_clock::time_point start_time;
        int64_t soft_limit_ms = -1, hard_limit_ms = -1;
        uint64_t node_limit = 0;
        int max_depth = MAX_PLY - 1;
        bool print = true;

        int64_t elapsed_ms() const {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
        }
    };

    struct ThreadData {
        int id = 0;
        Shared* sh = nullptr;
        Position pos;
        uint64_t nodes = 0;
        int seldepth = 0;
        Move killers[MAX_PLY][2] = {};
        Move counters[COLOR_NB][64][64] = {};
        int  history[COLOR_NB][64][64] = {};
        Move pv_table[MAX_PLY][MAX_PLY] = {};
        int  pv_len[MAX_PLY] = {};
        int  eval_stack[MAX_PLY] = {};
        Move move_stack[MAX_PLY] = {};
        Move best_move;
        int  best_score = 0;
        int  completed_depth = 0;
    };

    static void check_limits(ThreadData* td) {
        Shared& sh = *td->sh;
        if (stop_flag.load(std::memory_order_relaxed)) { sh.abort = true; return; }

        if (td->id == 0 && sh.hard_limit_ms >= 0 && sh.elapsed_ms() >= sh.hard_limit_ms) {
            sh.abort = true; return;
        }
        if (sh.node_limit && td->nodes >= sh.node_limit)
            sh.abort = true;
    }

    static inline int to_tt(int s, int ply) {
        if (s >= MATE_IN_MAX) return s + ply;
        if (s <= -MATE_IN_MAX) return s - ply;
        return s;
    }
    static inline int from_tt(int s, int ply) {
        if (s >= MATE_IN_MAX) return s - ply;
        if (s <= -MATE_IN_MAX) return s + ply;
        return s;
    }

    static int see(const Position& pos, Move m) {
        Square to = m.to_sq();
        Square from = m.from_sq();

        PieceType captured = (m.flags() == EP_CAPTURE) ? PAWN : pos.piece_on(to);
        int gain[32];
        int d = 0;
        gain[0] = (captured == NO_PIECE) ? 0 : Evaluation::SEEValues[captured];

        PieceType last_attacker = m.is_promotion() ? QUEEN : pos.piece_on(from);

        Bitboard occ = pos.all_pieces() ^ Bitboards::square_bb(from);
        if (m.flags() == EP_CAPTURE)
            occ ^= Bitboards::square_bb(static_cast<Square>(to + (pos.turn() == WHITE ? -8 : 8)));

        Bitboard all_bq = pos.pieces(WHITE, BISHOP) | pos.pieces(BLACK, BISHOP)
                        | pos.pieces(WHITE, QUEEN) | pos.pieces(BLACK, QUEEN);
        Bitboard all_rq = pos.pieces(WHITE, ROOK) | pos.pieces(BLACK, ROOK)
                        | pos.pieces(WHITE, QUEEN) | pos.pieces(BLACK, QUEEN);

        Bitboard attackers = pos.attackers_to(to, occ) & occ;
        Color side = ~pos.turn();

        while (true) {
            Bitboard side_att = attackers & pos.pieces(side) & occ;
            if (!side_att) break;

            PieceType pt = PAWN;
            Bitboard b = 0;
            for (int p = PAWN; p <= KING; ++p) {
                b = side_att & pos.pieces(side, static_cast<PieceType>(p));
                if (b) { pt = static_cast<PieceType>(p); break; }
            }

            if (pt == KING && (attackers & pos.pieces(~side) & occ & ~b)) break;

            d++;
            if (d >= 31) break;
            gain[d] = Evaluation::SEEValues[last_attacker] - gain[d - 1];
            last_attacker = pt;

            occ ^= (b & -b);
            attackers |= (Attacks::bishop(to, occ) & all_bq) | (Attacks::rook(to, occ) & all_rq);
            attackers &= occ;
            side = ~side;
        }

        while (d)
            gain[d - 1] = -std::max(-gain[d - 1], gain[d]), d--;
        return gain[0];
    }

    static inline int piece_val(const Position& pos, Move m) {
        PieceType cap = (m.flags() == EP_CAPTURE) ? PAWN : pos.piece_on(m.to_sq());
        return (cap == NO_PIECE) ? 0 : Evaluation::SEEValues[cap];
    }

    static void score_moves(ThreadData* td, MoveList& list, uint16_t tt_move_raw, int ply) {
        const Position& pos = td->pos;
        Move prev = (ply > 0) ? td->move_stack[ply - 1] : Move();
        Move cm = prev.is_none() ? Move()
                : td->counters[pos.turn()][prev.from_sq()][prev.to_sq()];

        for (int i = 0; i < list.size(); ++i) {
            Move m = list.get(i);
            int s;
            if (m.raw() == tt_move_raw && tt_move_raw != 0)
                s = 2000000;
            else if (m.is_capture()) {
                PieceType victim = (m.flags() == EP_CAPTURE) ? PAWN : pos.piece_on(m.to_sq());
                PieceType attacker = pos.piece_on(m.from_sq());
                s = 1000000 + Evaluation::SEEValues[victim] * 10 - Evaluation::SEEValues[attacker] / 10;
                if (m.is_promotion()) s += 500000;
            }
            else if (m.is_promotion())
                s = 900000 + (m.promo_piece() == QUEEN ? 1000 : 0);
            else if (m == td->killers[ply][0])
                s = 800000;
            else if (m == td->killers[ply][1])
                s = 790000;
            else if (!cm.is_none() && m == cm)
                s = 780000;
            else
                s = td->history[pos.turn()][m.from_sq()][m.to_sq()];
            list.set_score(i, s);
        }
    }

    static inline void hist_update(int& h, int bonus) {
        h += bonus - h * std::abs(bonus) / 16384;
    }

    static int qsearch(ThreadData* td, int alpha, int beta, int ply) {
        Position& pos = td->pos;
        Shared& sh = *td->sh;

        if ((td->nodes & 1023) == 0) check_limits(td);
        if (sh.abort.load(std::memory_order_relaxed)) return 0;
        td->nodes++;
        sh.nodes.fetch_add(1, std::memory_order_relaxed);
        if (ply > td->seldepth) td->seldepth = ply;

        if (pos.is_draw()) return 0;
        if (ply >= MAX_PLY - 1) return Evaluation::evaluate(pos);

        bool pv_node = (beta - alpha) > 1;

        uint64_t key = pos.key();
        const TT::Entry* tte = TT::probe(key);
        if (tte && !pv_node) {
            int tt_score = from_tt(tte->score, ply);
            if (tte->flag == TT::EXACT) return tt_score;
            if (tte->flag == TT::ALPHA && tt_score <= alpha) return tt_score;
            if (tte->flag == TT::BETA  && tt_score >= beta)  return tt_score;
        }

        bool in_chk = pos.in_check();
        int stand_pat = -INF;

        if (!in_chk) {
            stand_pat = Evaluation::evaluate(pos);
            if (stand_pat >= beta) return stand_pat;
            if (stand_pat > alpha) alpha = stand_pat;
        }

        MoveList list;
        if (in_chk) MoveGen::generate_all_moves(pos, list);
        else        MoveGen::generate_captures(pos, list);
        score_moves(td, list, tte ? tte->move : 0, ply);

        int best = stand_pat;
        int legal = 0;
        int orig_alpha = alpha;
        Move best_move;

        for (int i = 0; i < list.size(); ++i) {
            Move m = list.pick(i);

            if (!in_chk) {
                if (!m.is_promotion() && stand_pat + piece_val(pos, m) + 200 <= alpha)
                    continue;
                if (!m.is_promotion() && see(pos, m) < 0)
                    continue;
            }

            td->move_stack[ply] = m;
            pos.make_move(m);
            if (!pos.is_legal_now()) { pos.unmake_move(m); continue; }
            legal++;

            int score = -qsearch(td, -beta, -alpha, ply + 1);
            pos.unmake_move(m);
            if (sh.abort.load(std::memory_order_relaxed)) return 0;

            if (score > best) {
                best = score;
                best_move = m;
                if (score > alpha) {
                    alpha = score;
                    if (alpha >= beta) break;
                }
            }
        }

        if (in_chk && legal == 0) return -MATE + ply;

        TT::Flag flag = best >= beta ? TT::BETA
                      : best > orig_alpha ? TT::EXACT : TT::ALPHA;
        TT::store(key, 0, to_tt(best, ply), flag, best_move);
        return best;
    }

    static int negamax(ThreadData* td, int depth, int alpha, int beta, int ply, bool do_null) {
        Position& pos = td->pos;
        Shared& sh = *td->sh;

        td->pv_len[ply] = 0;
        bool pv_node = (beta - alpha) > 1;
        bool root = (ply == 0);

        if ((td->nodes & 1023) == 0) check_limits(td);
        if (sh.abort.load(std::memory_order_relaxed)) return 0;

        if (!root) {
            if (pos.is_draw()) return 0;
            if (ply >= MAX_PLY - 1) return Evaluation::evaluate(pos);

            alpha = std::max(alpha, -MATE + ply);
            beta  = std::min(beta,  MATE - ply - 1);
            if (alpha >= beta) return alpha;
        }

        bool in_chk = pos.in_check();
        if (in_chk) depth++;

        if (depth <= 0)
            return qsearch(td, alpha, beta, ply);

        td->nodes++;
        sh.nodes.fetch_add(1, std::memory_order_relaxed);

        uint64_t key = pos.key();
        const TT::Entry* tte = TT::probe(key);
        uint16_t tt_move_raw = tte ? tte->move : 0;

        if (tte && !pv_node && tte->depth >= depth) {
            int tt_score = from_tt(tte->score, ply);
            if (tte->flag == TT::EXACT) return tt_score;
            if (tte->flag == TT::ALPHA && tt_score <= alpha) return tt_score;
            if (tte->flag == TT::BETA  && tt_score >= beta)  return tt_score;
        }

        if (depth >= 4 && !tt_move_raw)
            depth--;

        int static_eval = in_chk ? VALUE_NONE : Evaluation::evaluate(pos);
        td->eval_stack[ply] = static_eval;

        bool improving = !in_chk && ply >= 2 &&
                         td->eval_stack[ply - 2] != VALUE_NONE &&
                         static_eval > td->eval_stack[ply - 2];

        if (!pv_node && !in_chk && depth <= 8 &&
            static_eval - (improving ? 60 : 85) * depth >= beta &&
            std::abs(beta) < MATE_IN_MAX)
            return static_eval;

        if (!pv_node && !in_chk && do_null && depth >= 3 &&
            static_eval >= beta && pos.has_non_pawn_material(pos.turn())) {
            int R = 3 + depth / 4 + std::min(3, (static_eval - beta) / 200);
            td->move_stack[ply] = Move();
            pos.make_null_move();
            int score = -negamax(td, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
            pos.unmake_null_move();
            if (sh.abort.load(std::memory_order_relaxed)) return 0;
            if (score >= beta)
                return (score >= MATE_IN_MAX) ? beta : score;
        }

        MoveList list;
        MoveGen::generate_all_moves(pos, list);
        score_moves(td, list, tt_move_raw, ply);

        int legal = 0;
        int best_score = -INF;
        Move best_move;
        TT::Flag flag = TT::ALPHA;

        Move tried_quiets[64];
        int num_tried_quiets = 0;

        bool futile = !pv_node && !in_chk && depth <= 3 &&
                      static_eval != VALUE_NONE &&
                      static_eval + 90 * depth + 100 <= alpha &&
                      std::abs(alpha) < MATE_IN_MAX;

        int lmp_limit = (3 + depth * depth) / (improving ? 1 : 2);

        for (int i = 0; i < list.size(); ++i) {
            Move m = list.pick(i);
            bool quiet = m.is_quiet();

            if (!root && legal > 0 && best_score > -MATE_IN_MAX) {
                if (quiet && !pv_node && !in_chk) {
                    if (futile) continue;
                    if (depth <= 5 && legal > lmp_limit) continue;

                    if (depth <= 6 && see(pos, m) < -60 * depth) continue;
                }

                if (m.is_capture() && !pv_node && depth <= 6 && see(pos, m) < -120 * depth)
                    continue;
            }

            td->move_stack[ply] = m;
            pos.make_move(m);
            if (!pos.is_legal_now()) { pos.unmake_move(m); continue; }
            legal++;

            if (quiet && num_tried_quiets < 64)
                tried_quiets[num_tried_quiets++] = m;

            int score;
            if (legal == 1) {
                score = -negamax(td, depth - 1, -beta, -alpha, ply + 1, true);
            } else {

                int r = 0;
                if (quiet && depth >= 3 && legal > 3) {
                    r = lmr_table[std::min(depth, 63)][std::min(legal, 63)];
                    if (pv_node && r > 0) r--;
                    if (!improving) r++;
                    r = std::clamp(r, 0, depth - 2);
                }
                score = -negamax(td, depth - 1 - r, -alpha - 1, -alpha, ply + 1, true);
                if (score > alpha && r > 0)
                    score = -negamax(td, depth - 1, -alpha - 1, -alpha, ply + 1, true);
                if (score > alpha && score < beta)
                    score = -negamax(td, depth - 1, -beta, -alpha, ply + 1, true);
            }
            pos.unmake_move(m);
            if (sh.abort.load(std::memory_order_relaxed)) return 0;

            if (score > best_score) {
                best_score = score;
                best_move = m;

                if (score > alpha) {
                    alpha = score;
                    flag = TT::EXACT;

                    td->pv_table[ply][0] = m;
                    if (ply + 1 < MAX_PLY) {
                        std::memcpy(&td->pv_table[ply][1], &td->pv_table[ply + 1][0],
                                    static_cast<size_t>(td->pv_len[ply + 1]) * sizeof(Move));
                        td->pv_len[ply] = td->pv_len[ply + 1] + 1;
                    } else td->pv_len[ply] = 1;

                    if (alpha >= beta) {
                        flag = TT::BETA;
                        if (quiet) {
                            if (td->killers[ply][0] != m) {
                                td->killers[ply][1] = td->killers[ply][0];
                                td->killers[ply][0] = m;
                            }
                            Move prev = (ply > 0) ? td->move_stack[ply - 1] : Move();
                            if (!prev.is_none())
                                td->counters[pos.turn()][prev.from_sq()][prev.to_sq()] = m;

                            int bonus = std::min(2000, 16 * depth * depth);
                            hist_update(td->history[pos.turn()][m.from_sq()][m.to_sq()], bonus);

                            for (int q = 0; q < num_tried_quiets - 1; ++q) {
                                Move qm = tried_quiets[q];
                                hist_update(td->history[pos.turn()][qm.from_sq()][qm.to_sq()], -bonus);
                            }
                        }
                        break;
                    }
                }
            }
        }

        if (legal == 0)
            return in_chk ? (-MATE + ply) : 0;

        TT::store(key, depth, to_tt(best_score, ply), flag, best_move);
        return best_score;
    }

    static void print_info(ThreadData* td, int depth, int score) {
        Shared& sh = *td->sh;
        int64_t ms = sh.elapsed_ms();
        uint64_t total = sh.nodes.load(std::memory_order_relaxed);
        uint64_t nps = ms > 0 ? total * 1000 / static_cast<uint64_t>(ms) : total;

        std::cout << "info depth " << depth << " seldepth " << td->seldepth;
        if (std::abs(score) >= MATE_IN_MAX) {
            int mate_in = (MATE - std::abs(score) + 1) / 2;
            std::cout << " score mate " << (score > 0 ? mate_in : -mate_in);
        } else
            std::cout << " score cp " << score;
        std::cout << " nodes " << total << " nps " << nps << " time " << ms
                  << " hashfull " << TT::hashfull() << " pv";
        for (int i = 0; i < td->pv_len[0]; ++i)
            std::cout << " " << td->pv_table[0][i].to_string();
        std::cout << std::endl;
    }

    static void id_loop(ThreadData* td) {
        Shared& sh = *td->sh;
        bool is_main = (td->id == 0);

        {
            MoveList list;
            MoveGen::generate_all_moves(td->pos, list);
            for (int i = 0; i < list.size(); ++i) {
                Move m = list.get(i);
                td->pos.make_move(m);
                bool ok = td->pos.is_legal_now();
                td->pos.unmake_move(m);
                if (ok) { td->best_move = m; break; }
            }
        }

        int prev_score = 0;
        int stability = 0;

        for (int d = 1; d <= sh.max_depth && d < MAX_PLY; ++d) {

            if (!is_main && d > 1 && ((d + td->id) % 2 == 0))
                continue;

            td->seldepth = 0;
            int alpha = -INF, beta = INF, delta = 25;
            if (d >= 5) {
                alpha = std::max(-INF, prev_score - delta);
                beta  = std::min(+INF, prev_score + delta);
            }

            int score;
            while (true) {
                score = negamax(td, d, alpha, beta, 0, true);
                if (sh.abort.load(std::memory_order_relaxed)) break;
                if (score <= alpha) {
                    alpha = std::max(-INF, score - delta);
                    delta += delta / 2 + 5;
                } else if (score >= beta) {
                    beta = std::min(+INF, score + delta);
                    delta += delta / 2 + 5;
                } else break;
            }
            if (sh.abort.load(std::memory_order_relaxed)) break;

            prev_score = score;
            if (td->pv_len[0] > 0) {
                Move new_best = td->pv_table[0][0];
                stability = (new_best == td->best_move) ? stability + 1 : 0;
                td->best_move = new_best;
            }
            td->best_score = score;
            td->completed_depth = d;

            if (is_main && sh.print)
                print_info(td, d, score);

            if (is_main && sh.soft_limit_ms >= 0) {

                double factor = 1.30 - 0.06 * std::min(stability, 5);
                int64_t eff = static_cast<int64_t>(sh.soft_limit_ms * factor);
                if (sh.hard_limit_ms >= 0) eff = std::min(eff, sh.hard_limit_ms);
                if (sh.elapsed_ms() >= eff) break;
                if (std::abs(score) >= MATE_IN_MAX && d >= 10) break;
            }
        }

        if (is_main)
            sh.abort = true;
    }

    static void setup_time(Shared& sh, const Position& pos, const Limits& limits) {
        constexpr int64_t OVERHEAD = 30;
        sh.soft_limit_ms = sh.hard_limit_ms = -1;
        sh.node_limit = limits.nodes;
        sh.max_depth = std::min(limits.depth, MAX_PLY - 1);

        if (limits.movetime >= 0) {
            sh.soft_limit_ms = sh.hard_limit_ms = std::max<int64_t>(1, limits.movetime - OVERHEAD);
        } else {
            int64_t t   = (pos.turn() == WHITE) ? limits.wtime : limits.btime;
            int64_t inc = (pos.turn() == WHITE) ? limits.winc  : limits.binc;
            if (t >= 0) {
                int64_t alloc;
                if (limits.movestogo > 0)
                    alloc = t / std::max(1, std::min(limits.movestogo, 40)) * 8 / 10 + inc / 2;
                else
                    alloc = t / 22 + inc / 2;
                sh.soft_limit_ms = std::max<int64_t>(1, alloc);
                sh.hard_limit_ms = std::max<int64_t>(1, std::min({alloc * 4, t / 3, t - OVERHEAD}));
                sh.soft_limit_ms = std::min(sh.soft_limit_ms, sh.hard_limit_ms);
            }
        }
        if (limits.infinite) sh.soft_limit_ms = sh.hard_limit_ms = -1;
    }

    void start(Position pos, Limits limits) {
        Shared sh;
        sh.start_time = std::chrono::steady_clock::now();
        sh.print = true;
        setup_time(sh, pos, limits);

        std::vector<std::unique_ptr<ThreadData>> tds;
        for (int i = 0; i < num_threads; ++i) {
            auto td = std::make_unique<ThreadData>();
            td->id = i;
            td->sh = &sh;
            td->pos = pos;
            tds.push_back(std::move(td));
        }

        std::vector<std::thread> helpers;
        for (int i = 1; i < num_threads; ++i)
            helpers.emplace_back(id_loop, tds[i].get());

        id_loop(tds[0].get());

        for (auto& h : helpers)
            h.join();

        std::cout << "bestmove " << tds[0]->best_move.to_string() << std::endl;
    }

    Result run_sync(Position& pos, const Limits& limits) {
        Shared sh;
        sh.start_time = std::chrono::steady_clock::now();
        sh.print = false;
        setup_time(sh, pos, limits);

        ThreadData td;
        td.id = 0;
        td.sh = &sh;
        td.pos = pos;

        id_loop(&td);

        Result r;
        r.best_move = td.best_move;
        r.score = td.best_score;
        r.nodes = sh.nodes.load();
        r.depth = td.completed_depth;
        return r;
    }
}
