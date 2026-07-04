
#pip install chess

import subprocess, time, sys, math, random, csv, os
from concurrent.futures import ThreadPoolExecutor
import chess

if len(sys.argv) < 3:
    print("usage: python tools/match.py <engineA> <engineB> [games=30] [movetime_ms=200] [workers=3] [csv]")
    sys.exit(1)

ENGINE_A = os.path.abspath(sys.argv[1])
ENGINE_B = os.path.abspath(sys.argv[2])
GAMES = int(sys.argv[3]) if len(sys.argv) > 3 else 30
MOVETIME = int(sys.argv[4]) if len(sys.argv) > 4 else 200
WORKERS = int(sys.argv[5]) if len(sys.argv) > 5 else 3
CSV_OUT = sys.argv[6] if len(sys.argv) > 6 else None

random.seed(42)

def gen_opening():
    board = chess.Board()
    for _ in range(6):
        board.push(random.choice(list(board.legal_moves)))
    return [m.uci() for m in board.move_stack]

OPENINGS = [gen_opening() for _ in range((GAMES + 1) // 2)]

class Eng:
    def __init__(self, path):
        self.p = subprocess.Popen(path, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, bufsize=1, cwd=os.path.dirname(path) or ".")
        self.send("uci"); self.wait("uciok")
        self.send("setoption name OwnBook value false")
        self.send("isready"); self.wait("readyok")

    def send(self, c): self.p.stdin.write(c + "\n"); self.p.stdin.flush()

    def wait(self, tok, timeout=60):
        end = time.time() + timeout
        while time.time() < end:
            line = self.p.stdout.readline()
            if not line: raise RuntimeError("motor kapandi")
            if line.startswith(tok): return line
        raise RuntimeError("zaman asimi: " + tok)

    def bestmove(self, moves):
        pos = "position startpos"
        if moves: pos += " moves " + " ".join(moves)
        self.send(pos)
        self.send(f"go movetime {MOVETIME}")
        return self.wait("bestmove").split()[1]

    def newgame(self):
        self.send("ucinewgame"); self.send("isready"); self.wait("readyok")

    def quit(self):
        try: self.send("quit"); self.p.wait(timeout=5)
        except Exception: self.p.kill()

def play_game(args):
    game_idx, opening, a_is_white = args
    ea, eb = Eng(ENGINE_A), Eng(ENGINE_B)
    ea.newgame(); eb.newgame()
    board = chess.Board()
    moves = list(opening)
    for mv in moves:
        board.push_uci(mv)

    illegal_by = None
    while not board.is_game_over(claim_draw=True) and len(moves) < 400:
        white_turn = board.turn == chess.WHITE
        eng = ea if (white_turn == a_is_white) else eb
        mv = eng.bestmove(moves)
        m = chess.Move.from_uci(mv) if mv != "0000" else None
        if m is None or m not in board.legal_moves:
            illegal_by = "A" if eng is ea else "B"
            break
        board.push(m)
        moves.append(mv)

    ea.quit(); eb.quit()

    if illegal_by:
        score_a = 0.0 if illegal_by == "A" else 1.0
        res = f"ILLEGAL({illegal_by})"
    else:
        res = board.result(claim_draw=True)
        if res == "1/2-1/2" or len(moves) >= 400:
            score_a, res = 0.5, "1/2-1/2"
        elif res == "1-0":
            score_a = 1.0 if a_is_white else 0.0
        else:
            score_a = 0.0 if a_is_white else 1.0
    return game_idx, score_a, res, len(moves), a_is_white

def main():
    jobs = [(i, OPENINGS[i // 2], i % 2 == 0) for i in range(GAMES)]
    t0 = time.time()
    results = []
    with ThreadPoolExecutor(max_workers=WORKERS) as ex:
        for r in ex.map(play_game, jobs):
            results.append(r)
            print(f"  oyun {r[0]+1}/{GAMES}: A_skor={r[1]} ({r[2]}, {r[3]} hamle)", flush=True)

    score = sum(r[1] for r in results)
    n = len(results)
    w = sum(1 for r in results if r[1] == 1.0)
    d = sum(1 for r in results if r[1] == 0.5)
    l = n - w - d
    pct = score / n
    print(f"\nSonuc (A): +{w} ={d} -{l}  skor {score}/{n} ({100*pct:.1f}%)")
    if 0 < pct < 1:
        elo = -400 * math.log10(1 / pct - 1)
        sd = math.sqrt(sum((r[1] - pct) ** 2 for r in results) / max(n - 1, 1)) / math.sqrt(n)
        lo, hi = max(0.001, pct - 1.96 * sd), min(0.999, pct + 1.96 * sd)
        print(f"Elo farki: {elo:+.0f}  [%95 GA: "
              f"{-400*math.log10(1/lo-1):+.0f} .. {-400*math.log10(1/hi-1):+.0f}]")
    print(f"Sure: {time.time()-t0:.0f}s")

    if CSV_OUT:
        with open(CSV_OUT, "w", newline="") as f:
            cw = csv.writer(f)
            cw.writerow(["game", "score_a", "result", "plies", "a_white"])
            for r in sorted(results):
                cw.writerow(r)
        print(f"CSV: {CSV_OUT}")

if __name__ == "__main__":
    main()
