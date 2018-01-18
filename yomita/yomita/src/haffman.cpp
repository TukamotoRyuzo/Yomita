/*
読み太（yomita）, a USI shogi (Japanese chess) playing engine derived from
Stockfish 7 & YaneuraOu mid 2016 V3.57
Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish author)
Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (Stockfish author)
Copyright (C) 2015-2016 Motohiro Isozaki(YaneuraOu author)
Copyright (C) 2016-2017 Ryuzo Tukamoto

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#if defined LEARN

#include <sstream>
#include <fstream>

#include "board.h"

const int huffman_table[][2] =
{
    { 0x00, 1 }, // NO_PIECE
    { 0x1f, 6 }, // BISHOP
    { 0x3f, 6 }, // ROOK
    { 0x01, 2 }, // PAWN
    { 0x03, 4 }, // LANCE
    { 0x0b, 4 }, // KNIGHT
    { 0x07, 4 }, // SILVER
    { 0x0f, 5 }, // GOLD
};

struct SfenPacker
{
    SfenPacker(uint8_t* d) : data(d), bit_cursor(0) {};

    int getCursor() const { return bit_cursor; }

    void write1bit(int b)
    {
        if (b)
            data[bit_cursor / 8] |= 1 << (bit_cursor & 7);
        ++bit_cursor;
    }

    int read1bit()
    {
        int b = (data[bit_cursor / 8] >> (bit_cursor & 7)) & 1;
        ++bit_cursor;
        return b;
    }

    void writeNbit(int d, int n)
    {
        for (int i = 0; i < n; ++i)
            write1bit(d & (1 << i));
    }

    int readNbit(int n)
    {
        int result = 0;
        for (int i = 0; i < n; ++i)
            result |= read1bit() ? (1 << i) : 0;
        return result;
    }

    void pack(const Board& b)
    {
        memset(data, 0, 32);

        write1bit(b.turn());

        for (auto c : Turns)
            writeNbit(b.kingSquare(c), 7);

        for (auto sq : Squares)
        {
            Piece pc = b.piece(sq);

            if (typeOf(pc) == KING)
                continue;

            writeBoard(pc);
        }

        for (auto c : Turns)
            for (PieceType pr : HandPiece)
            {
                int n = b.hand(c).count(pr);
                for (int i = 0; i < n; ++i)
                    writeHand(pr | c);
            }

        assert(getCursor() == 256);
    }

    void writeBoard(Piece pc)
    {
        PieceType pr = nativeType(pc);
        auto c = huffman_table[pr];
        writeNbit(c[0], c[1]);

        if (pc == (Piece)NO_PIECE_TYPE)
            return;

        if (pr != GOLD)
            write1bit(isPromoted(pc) ? 1 : 0);

        write1bit(turnOf(pc));
    }

    void writeHand(Piece pc)
    {
        assert(pc != EMPTY);

        PieceType pr = nativeType(pc);
        auto c = huffman_table[pr];
        writeNbit(c[0] >> 1, c[1] - 1);

        if (pr != GOLD)
            write1bit(false);
        
        write1bit(turnOf(pc));
    }

    Piece readBoard()
    {
        PieceType pr = NO_PIECE_TYPE;
        int code = 0, bits = 0;
        while (true)
        {
            code |= read1bit() << bits;
            ++bits;

            assert(bits <= 6);

            for (pr = NO_PIECE_TYPE; pr < KING; ++pr)
                if (huffman_table[pr][0] == code
                    && huffman_table[pr][1] == bits)
                    goto Found;
        }
    Found:;
        if (pr == NO_PIECE_TYPE)
            return Piece(pr);

        bool promote = (pr == GOLD) ? false : read1bit();
        Turn c = (Turn)read1bit();
        return (promote ? promotePieceType(pr) : pr) | c;
    }

    Piece readHand()
    {
        PieceType pr = NO_PIECE_TYPE;
        int code = 0, bits = 0;
        while (true)
        {
            code |= read1bit() << bits;
            ++bits;

            assert(bits <= 6);

            for (pr = BISHOP; pr < KING; ++pr)
                if ((huffman_table[pr][0] >> 1) == code
                    && (huffman_table[pr][1] - 1) == bits)
                    goto Found;
        }
    Found:;
        assert(pr != NO_PIECE_TYPE);

        if (pr != GOLD)
            read1bit();

        Turn c = (Turn)read1bit();

        return pr | c;
    }

private:
    uint8_t *data;
    int bit_cursor;
};

#if defined GENERATED_SFEN_BY_FILESQ
extern Square f2r(const Square sq);
// 縦型Squareのfor文での増分用関数。
Square nextSq(const Square sq) { return Square(sq + 9 > SQ_MAX ? sq - 73 : sq + 9); }
#endif

void Board::setFromPackedSfen(uint8_t data[32])
{
    SfenPacker packer(data);
    auto th = thisThread();
    clear();
    setThread(th);

    turn_ = (Turn)packer.read1bit();
    PieceNo piece_no_count[KING] =
    {
        PIECE_NO_ZERO, PIECE_NO_BISHOP, PIECE_NO_ROOK, PIECE_NO_PAWN, PIECE_NO_LANCE,
        PIECE_NO_KNIGHT, PIECE_NO_SILVER, PIECE_NO_GOLD
    };
#ifdef USE_EVAL
    eval_list_.clear();
#endif
    for (auto c : Turns)
    {
#ifdef GENERATED_SFEN_BY_FILESQ
        board_[f2r((Square)stream.readNbit(7))] = KING | c;
#else
        board_[packer.readNbit(7)] = KING | c;
#endif
    }

#ifdef GENERATED_SFEN_BY_FILESQ
    for (Square sq = SQ_11; sq != SQ_MAX; sq = nextSq(sq))
#else
    for (auto sq : Squares)
#endif
    {
        Piece pc;
        if (typeOf(board_[sq]) != KING)
        {
            assert(board_[sq] == EMPTY);
            pc = packer.readBoard();
        }
        else
        {
            pc = board_[sq];
            board_[sq] = EMPTY; 
        }

        if (pc == EMPTY)
            continue;

        PieceNo piece_no =
            (pc == B_KING) ? PIECE_NO_BKING :
            (pc == W_KING) ? PIECE_NO_WKING :
            piece_no_count[nativeType(pc)]++;
#ifdef USE_BYTEBOARD
        pieceno_to_sq_[piece_no] = sq;
        sq_to_pieceno_[sq] = piece_no;
#endif
        setPiece(pc, sq, piece_no);
        assert(packer.getCursor() <= 256);
    }
#if 0
    std::cout << std::endl;
    for (auto rank : Ranks)
    {
        for (auto file : Files)
        {
            Piece p = board_[sqOf(file, rank)];

            if (turnOf(p) == BLACK)
                std::cout << " ";

            std::cout << p;
        }
        std::cout << std::endl;
    }
#endif
#ifdef USE_BITBOARD
    bb_gold_ = bbType(GOLD, PRO_PAWN, PRO_LANCE, PRO_KNIGHT, PRO_SILVER);
#endif
#ifdef USE_EVAL
    int i = 0;
    Piece lastPc = EMPTY;
#endif

    int p_num[W_KING] = { 0 };

    while (packer.getCursor() != 256)
    {
        assert(packer.getCursor() < 256);
        auto pc = packer.readHand();
        PieceType rpc = nativeType(pc);
        hand_[turnOf(pc)].add(rpc);
#ifdef USE_EVAL
        PieceNo piece_no = piece_no_count[rpc]++;
        assert(isOK(piece_no));
        eval_list_.putPiece(piece_no, turnOf(pc), rpc, p_num[pc]++);
#ifdef USE_BYTEBOARD
        pieceno_to_sq_[piece_no] = SQ_MAX;
#endif
#endif
    }

    setState(st_);
#ifdef USE_EVAL
    Eval::computeAll(*this);
#endif

#ifdef USE_BYTEBOARD
    for (int i = 0; i < 40; i++)
    {
        Square sq = pieceSquare(i);

        if (sq != SQ_MAX)
            stm_piece_[turnOf(piece(sq))] |= 1ULL << i;
    }
#endif
    assert(verify());
}

void Board::sfenPack(uint8_t data[32]) const
{
    SfenPacker sp(data);
    sp.pack(*this);
}

#endif
