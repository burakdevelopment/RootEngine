
#pip install torch chess numpy

import sys
import numpy as np
import torch
import torch.nn as nn
import chess

HIDDEN = 256
QA = 255
QB = 64
SCALE = 400
WDL_LAMBDA = 0.3   
MAX_FEATURES = 32  


def feature_index(persp_white: bool, piece: chess.Piece, sq: int) -> int:
    rel = 0 if (piece.color == chess.WHITE) == persp_white else 1
    s = sq if persp_white else sq ^ 56
    return rel * 384 + (piece.piece_type - 1) * 64 + s


def load_data(path):
    us_idx, them_idx, targets = [], [], []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            parts = line.strip().split(";")
            if len(parts) != 3:
                continue
            try:
                board = chess.Board(parts[0])
                score_white = int(parts[1])
                result_white = float(parts[2])
            except Exception:
                continue

            stm_white = board.turn == chess.WHITE
            score_stm = score_white if stm_white else -score_white
            result_stm = result_white if stm_white else 1.0 - result_white

            u = np.full(MAX_FEATURES, 768, dtype=np.int64)  
            t = np.full(MAX_FEATURES, 768, dtype=np.int64)
            i = 0
            for sq, piece in board.piece_map().items():
                u[i] = feature_index(stm_white, piece, sq)
                t[i] = feature_index(not stm_white, piece, sq)
                i += 1

            target = (1 - WDL_LAMBDA) * (1 / (1 + np.exp(-score_stm / SCALE))) \
                     + WDL_LAMBDA * result_stm
            us_idx.append(u)
            them_idx.append(t)
            targets.append(target)

    return (torch.tensor(np.array(us_idx)),
            torch.tensor(np.array(them_idx)),
            torch.tensor(np.array(targets), dtype=torch.float32))


class NNUE(nn.Module):
    def __init__(self):
        super().__init__()
        
        self.ft = nn.Embedding(769, HIDDEN, padding_idx=768)
        self.ft_bias = nn.Parameter(torch.zeros(HIDDEN))
        self.out = nn.Linear(2 * HIDDEN, 1)
        nn.init.uniform_(self.ft.weight, -0.05, 0.05)
        with torch.no_grad():
            self.ft.weight[768].zero_()

    def forward(self, us, them):
        acc_us = self.ft(us).sum(dim=1) + self.ft_bias
        acc_them = self.ft(them).sum(dim=1) + self.ft_bias
        h = torch.cat([acc_us.clamp(0, 1), acc_them.clamp(0, 1)], dim=1)
        return self.out(h).squeeze(1)


def export(model, path):
    with torch.no_grad():
        ft_w = (model.ft.weight[:768] * QA).round().clamp(-32767, 32767).short().numpy()
        ft_b = (model.ft_bias * QA).round().clamp(-32767, 32767).short().numpy()
        out_w = (model.out.weight[0] * QB).round().clamp(-32767, 32767).short().numpy()
        out_b = int(round(model.out.bias.item() * QA * QB))

    with open(path, "wb") as f:
        f.write(b"RTNN")
        f.write(np.uint32(1).tobytes())
        f.write(np.uint32(HIDDEN).tobytes())
        f.write(ft_w.astype("<i2").tobytes())        
        f.write(ft_b.astype("<i2").tobytes())        
        f.write(out_w.astype("<i2").tobytes())       
        f.write(np.int32(out_b).tobytes())
    print(f"kaydedildi: {path}")


def main():
    data_path = sys.argv[1] if len(sys.argv) > 1 else "train_data.csv"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "root.nnue"
    epochs = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    batch = int(sys.argv[4]) if len(sys.argv) > 4 else 16384

    print("veri yukleniyor...")
    us, them, y = load_data(data_path)
    n = len(y)
    print(f"{n} pozisyon")

    split = int(n * 0.95)
    perm = torch.randperm(n)
    us, them, y = us[perm], them[perm], y[perm]
    us_v, them_v, y_v = us[split:], them[split:], y[split:]
    us, them, y = us[:split], them[:split], y[:split]

    model = NNUE()
    opt = torch.optim.Adam(model.parameters(), lr=1e-3)
    torch.set_num_threads(max(1, torch.get_num_threads() - 1))

    for epoch in range(epochs):
        model.train()
        perm = torch.randperm(len(y))
        total = 0.0
        nb = 0
        for i in range(0, len(y), batch):
            idx = perm[i:i + batch]
            opt.zero_grad()
            pred = model(us[idx], them[idx])
            loss = ((torch.sigmoid(pred) - y[idx]) ** 2).mean()
            loss.backward()
            opt.step()
            
            with torch.no_grad():
                model.ft.weight.clamp_(-1.98, 1.98)
                model.ft.weight[768].zero_()
                model.out.weight.clamp_(-1.98, 1.98)
            total += loss.item()
            nb += 1

        model.eval()
        with torch.no_grad():
            vloss = ((torch.sigmoid(model(us_v, them_v)) - y_v) ** 2).mean().item()
        print(f"epoch {epoch+1}/{epochs}: egitim {total/nb:.5f}  dogrulama {vloss:.5f}")

    export(model, out_path)


if __name__ == "__main__":
    main()
