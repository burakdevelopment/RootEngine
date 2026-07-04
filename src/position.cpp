#include "position.h"
#include "attacks.h"
#include "polyglot_random.h"
#include <iostream>
#include <sstream>
#include <cctype>
#include <algorithm>

static inline uint64_t piece_key(Color c, PieceType pt, Square sq) {
    return Zobrist::Random64[64 * (2 * pt + (c == WHITE ? 1 : 0)) + sq];
}

static inline uint64_t castle_key(int rights) {
    uint64_t k = 0;
    if (rights & WHITE_OO)  k ^= Zobrist::Random64[768];
    if (rights & WHITE_OOO) k ^= Zobrist::Random64[769];
    if (rights & BLACK_OO)  k ^= Zobrist::Random64[770];
    if (rights & BLACK_OOO) k ^= Zobrist::Random64[771];
    return k;
}

static inline uint64_t ep_key(Square ep) {
    return Zobrist::Random64[772 + file_of(ep)];
}

static inline uint64_t turn_key() { return Zobrist::Random64[780]; }

static const int CastleRightsMask[64] = {
    (int)(~WHITE_OOO & 15), 15, 15, 15, (int)(~(WHITE_OO | WHITE_OOO) & 15), 15, 15, (int)(~WHITE_OO & 15),
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    (int)(~BLACK_OOO & 15), 15, 15, 15, (int)(~(BLACK_OO | BLACK_OOO) & 15), 15, 15, (int)(~BLACK_OO & 15),
};

Position::Position() {
    clear();
}

void Position::clear() {
    for (int c = WHITE; c <= BLACK; ++c) {
        color_bb[c] = 0ULL;
        for (int pt = PAWN; pt <= KING; ++pt)
            piece_bb[c][pt] = 0ULL;
    }
    for (int s = 0; s < 64; ++s)
        board[s] = NO_PIECE;
    side_to_move = WHITE;
    en_passant = SQ_NONE;
    castle_rights = NO_CASTLING;
    halfmove_clock = 0;
    game_ply = 0;
    hash_key = 0;
}

void Position::put_piece(Color c, PieceType pt, Square sq) {
    Bitboard b = Bitboards::square_bb(sq);
    piece_bb[c][pt] |= b;
    color_bb[c] |= b;
    board[sq] = static_cast<uint8_t>(pt);
    hash_key ^= piece_key(c, pt, sq);
}

void Position::remove_piece(Color c, PieceType pt, Square sq) {
    Bitboard b = Bitboards::square_bb(sq);
    piece_bb[c][pt] &= ~b;
    color_bb[c] &= ~b;
    board[sq] = NO_PIECE;
    hash_key ^= piece_key(c, pt, sq);
}

Square Position::adjust_ep(Square ep_candidate) const {
    if (ep_candidate == SQ_NONE) return SQ_NONE;

    Square pawn_sq = static_cast<Square>(side_to_move == BLACK ? ep_candidate + 8 : ep_candidate - 8);
    Bitboard b = Bitboards::square_bb(pawn_sq);
    Bitboard adjacent = ((b << 1) & ~Bitboards::FILE_A_BB) | ((b >> 1) & ~Bitboards::FILE_H_BB);
    return (pieces(side_to_move, PAWN) & adjacent) ? ep_candidate : SQ_NONE;
}

uint64_t Position::compute_key() const {
    uint64_t k = 0;
    for (int c = WHITE; c <= BLACK; ++c)
        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard bb = piece_bb[c][pt];
            while (bb)
                k ^= piece_key(static_cast<Color>(c), static_cast<PieceType>(pt), Bitboards::pop_lsb(bb));
        }
    k ^= castle_key(castle_rights);
    if (en_passant != SQ_NONE) k ^= ep_key(en_passant);
    if (side_to_move == WHITE) k ^= turn_key();
    return k;
}

void Position::set_startpos() {
    set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

void Position::set_fen(const std::string& fen) {
    clear();
    std::istringstream ss(fen);
    std::string board_str, color_str, castling_str, ep_str;
    int halfmove = 0, fullmove = 1;

    ss >> board_str >> color_str >> castling_str >> ep_str;
    if (ss >> halfmove) halfmove_clock = halfmove;
    if (ss >> fullmove) {  }

    int rank = 7, file = 0;
    for (char c : board_str) {
        if (c == '/') { rank--; file = 0; }
        else if (isdigit(static_cast<unsigned char>(c))) file += (c - '0');
        else {
            Color col = isupper(static_cast<unsigned char>(c)) ? WHITE : BLACK;
            PieceType pt;
            switch (tolower(c)) {
                case 'p': pt = PAWN; break;
                case 'n': pt = KNIGHT; break;
                case 'b': pt = BISHOP; break;
                case 'r': pt = ROOK; break;
                case 'q': pt = QUEEN; break;
                default:  pt = KING; break;
            }
            put_piece(col, pt, static_cast<Square>(rank * 8 + file));
            file++;
        }
    }

    side_to_move = (color_str == "w") ? WHITE : BLACK;

    if (castling_str != "-") {
        if (castling_str.find('K') != std::string::npos) castle_rights |= WHITE_OO;
        if (castling_str.find('Q') != std::string::npos) castle_rights |= WHITE_OOO;
        if (castling_str.find('k') != std::string::npos) castle_rights |= BLACK_OO;
        if (castling_str.find('q') != std::string::npos) castle_rights |= BLACK_OOO;
    }

    if (ep_str.size() >= 2 && ep_str != "-") {
        int f = ep_str[0] - 'a';
        int r = ep_str[1] - '1';
        en_passant = adjust_ep(static_cast<Square>(r * 8 + f));
    }

    game_ply = 0;
    hash_key = compute_key();
}

std::string Position::to_fen() const {
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file <= 7; ++file) {
            Square sq = static_cast<Square>(rank * 8 + file);
            if (board[sq] == NO_PIECE) { empty++; continue; }
            if (empty) { fen += char('0' + empty); empty = 0; }
            char pc = "PNBRQK"[board[sq]];
            if (color_on(sq) == BLACK) pc = static_cast<char>(tolower(pc));
            fen += pc;
        }
        if (empty) fen += char('0' + empty);
        if (rank > 0) fen += '/';
    }
    fen += (side_to_move == WHITE) ? " w " : " b ";
    if (castle_rights == NO_CASTLING) fen += "-";
    else {
        if (castle_rights & WHITE_OO)  fen += 'K';
        if (castle_rights & WHITE_OOO) fen += 'Q';
        if (castle_rights & BLACK_OO)  fen += 'k';
        if (castle_rights & BLACK_OOO) fen += 'q';
    }
    fen += ' ';
    if (en_passant == SQ_NONE) fen += '-';
    else {
        fen += char('a' + file_of(en_passant));
        fen += char('1' + rank_of(en_passant));
    }
    fen += ' ';
    fen += std::to_string(halfmove_clock);
    fen += " ";
    fen += std::to_string(1 + game_ply / 2);
    return fen;
}

void Position::make_move(Move m) {
    Square from = m.from_sq();
    Square to = m.to_sq();
    int flag = m.flags();
    Color us = side_to_move;
    Color them = ~us;

    PieceType moving = piece_on(from);
    PieceType captured = (flag == EP_CAPTURE) ? NO_PIECE : piece_on(to);

    if (game_ply >= MAX_HISTORY - 1) game_ply = MAX_HISTORY - 2;

    st[game_ply] = { en_passant, castle_rights, captured, halfmove_clock, hash_key };

    if (moving == PAWN || m.is_capture()) halfmove_clock = 0;
    else halfmove_clock++;

    if (en_passant != SQ_NONE) {
        hash_key ^= ep_key(en_passant);
        en_passant = SQ_NONE;
    }

    remove_piece(us, moving, from);

    if (flag == EP_CAPTURE) {
        Square cap_sq = static_cast<Square>(to + (us == WHITE ? -8 : 8));
        remove_piece(them, PAWN, cap_sq);
    } else if (captured != NO_PIECE) {
        remove_piece(them, captured, to);
    }

    put_piece(us, m.is_promotion() ? m.promo_piece() : moving, to);

    if (flag == KING_CASTLE) {
        Square rf = (us == WHITE) ? SQ_H1 : SQ_H8;
        Square rt = (us == WHITE) ? SQ_F1 : SQ_F8;
        remove_piece(us, ROOK, rf);
        put_piece(us, ROOK, rt);
    } else if (flag == QUEEN_CASTLE) {
        Square rf = (us == WHITE) ? SQ_A1 : SQ_A8;
        Square rt = (us == WHITE) ? SQ_D1 : SQ_D8;
        remove_piece(us, ROOK, rf);
        put_piece(us, ROOK, rt);
    }

    int new_rights = castle_rights & CastleRightsMask[from] & CastleRightsMask[to];
    if (new_rights != castle_rights) {
        hash_key ^= castle_key(castle_rights) ^ castle_key(new_rights);
        castle_rights = new_rights;
    }

    side_to_move = them;
    hash_key ^= turn_key();

    if (flag == DOUBLE_PAWN_PUSH) {
        Square ep_c = static_cast<Square>(from + (us == WHITE ? 8 : -8));
        en_passant = adjust_ep(ep_c);
        if (en_passant != SQ_NONE) hash_key ^= ep_key(en_passant);
    }

    game_ply++;
}

void Position::unmake_move(Move m) {
    game_ply--;
    side_to_move = ~side_to_move;
    Color us = side_to_move;
    Color them = ~us;

    Square from = m.from_sq();
    Square to = m.to_sq();
    int flag = m.flags();

    PieceType placed = piece_on(to);
    remove_piece(us, placed, to);
    put_piece(us, m.is_promotion() ? PAWN : placed, from);

    const State& s = st[game_ply];

    if (flag == EP_CAPTURE) {
        Square cap_sq = static_cast<Square>(to + (us == WHITE ? -8 : 8));
        put_piece(them, PAWN, cap_sq);
    } else if (s.captured_piece != NO_PIECE) {
        put_piece(them, static_cast<PieceType>(s.captured_piece), to);
    }

    if (flag == KING_CASTLE) {
        Square rf = (us == WHITE) ? SQ_H1 : SQ_H8;
        Square rt = (us == WHITE) ? SQ_F1 : SQ_F8;
        remove_piece(us, ROOK, rt);
        put_piece(us, ROOK, rf);
    } else if (flag == QUEEN_CASTLE) {
        Square rf = (us == WHITE) ? SQ_A1 : SQ_A8;
        Square rt = (us == WHITE) ? SQ_D1 : SQ_D8;
        remove_piece(us, ROOK, rt);
        put_piece(us, ROOK, rf);
    }

    en_passant = s.ep_square;
    castle_rights = s.castling_rights;
    halfmove_clock = s.halfmove_clock;
    hash_key = s.key;
}

void Position::make_null_move() {
    if (game_ply >= MAX_HISTORY - 1) game_ply = MAX_HISTORY - 2;
    st[game_ply] = { en_passant, castle_rights, NO_PIECE, halfmove_clock, hash_key };

    if (en_passant != SQ_NONE) {
        hash_key ^= ep_key(en_passant);
        en_passant = SQ_NONE;
    }
    side_to_move = ~side_to_move;
    hash_key ^= turn_key();
    halfmove_clock++;
    game_ply++;
}

void Position::unmake_null_move() {
    game_ply--;
    side_to_move = ~side_to_move;
    const State& s = st[game_ply];
    en_passant = s.ep_square;
    castle_rights = s.castling_rights;
    halfmove_clock = s.halfmove_clock;
    hash_key = s.key;
}

bool Position::is_square_attacked(Square sq, Color attacker) const {
    if (Attacks::pawn(~attacker, sq) & pieces(attacker, PAWN)) return true;
    if (Attacks::knight(sq) & pieces(attacker, KNIGHT)) return true;
    if (Attacks::king(sq) & pieces(attacker, KING)) return true;

    Bitboard occ = all_pieces();
    if (Attacks::bishop(sq, occ) & (pieces(attacker, BISHOP) | pieces(attacker, QUEEN))) return true;
    if (Attacks::rook(sq, occ) & (pieces(attacker, ROOK) | pieces(attacker, QUEEN))) return true;
    return false;
}

Bitboard Position::attackers_to(Square sq, Bitboard occ) const {
    return (Attacks::pawn(BLACK, sq) & pieces(WHITE, PAWN))
         | (Attacks::pawn(WHITE, sq) & pieces(BLACK, PAWN))
         | (Attacks::knight(sq) & (pieces(WHITE, KNIGHT) | pieces(BLACK, KNIGHT)))
         | (Attacks::king(sq) & (pieces(WHITE, KING) | pieces(BLACK, KING)))
         | (Attacks::bishop(sq, occ) & (pieces(WHITE, BISHOP) | pieces(BLACK, BISHOP) | pieces(WHITE, QUEEN) | pieces(BLACK, QUEEN)))
         | (Attacks::rook(sq, occ) & (pieces(WHITE, ROOK) | pieces(BLACK, ROOK) | pieces(WHITE, QUEEN) | pieces(BLACK, QUEEN)));
}

bool Position::in_check() const {

    if (!piece_bb[side_to_move][KING]) return false;
    return is_square_attacked(king_sq(side_to_move), ~side_to_move);
}

bool Position::is_legal_now() const {
    Color mover = ~side_to_move;
    if (!piece_bb[mover][KING]) return false;

    if (!piece_bb[side_to_move][KING]) return false;
    return !is_square_attacked(king_sq(mover), side_to_move);
}

bool Position::is_repetition() const {
    int end = std::max(0, game_ply - halfmove_clock);
    for (int i = game_ply - 4; i >= end; i -= 2)
        if (st[i].key == hash_key)
            return true;
    return false;
}

bool Position::has_non_pawn_material(Color c) const {
    return (pieces(c, KNIGHT) | pieces(c, BISHOP) | pieces(c, ROOK) | pieces(c, QUEEN)) != 0;
}

bool Position::is_draw() const {
    if (halfmove_clock >= 100) return true;
    if (is_repetition()) return true;

    Bitboard majors_pawns = pieces(WHITE, PAWN) | pieces(BLACK, PAWN)
                          | pieces(WHITE, ROOK) | pieces(BLACK, ROOK)
                          | pieces(WHITE, QUEEN) | pieces(BLACK, QUEEN);
    if (!majors_pawns) {
        Bitboard minors = pieces(WHITE, KNIGHT) | pieces(BLACK, KNIGHT)
                        | pieces(WHITE, BISHOP) | pieces(BLACK, BISHOP);
        if (Bitboards::popcount(minors) <= 1) return true;
    }
    return false;
}

void Position::print_board() const {
    std::cout << "\n";
    for (int rank = 7; rank >= 0; --rank) {
        std::cout << rank + 1 << "  ";
        for (int file = 0; file <= 7; ++file) {
            Square sq = static_cast<Square>(rank * 8 + file);
            char pc = '.';
            if (board[sq] != NO_PIECE) {
                pc = "PNBRQK"[board[sq]];
                if (color_on(sq) == BLACK) pc = static_cast<char>(tolower(pc));
            }
            std::cout << pc << " ";
        }
        std::cout << "\n";
    }
    std::cout << "\n   a b c d e f g h\n\n";
    std::cout << "Hamle Sirasi: " << (side_to_move == WHITE ? "Beyaz" : "Siyah") << "\n";
    std::cout << "Hash        : " << std::hex << hash_key << std::dec << "\n\n";
}
