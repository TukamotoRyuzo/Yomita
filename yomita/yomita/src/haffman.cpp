/*
読み太（yomita）, a USI shogi (Japanese chess) playing engine derived from
Stockfish 7 & YaneuraOu mid 2016 V3.57
Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad (Stockfish author)
Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad (Stockfish author)
Copyright (C) 2015-2016 Motohiro Isozaki(YaneuraOu author)
Copyright (C) 2016 Ryuzo Tukamoto

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

#if defined LEARN || defined GENSFEN

#include "board.h"
#include <sstream>
#include <fstream>

struct BitStream
{
    // -----------------------------------
    //        局面の圧縮・解凍
    // -----------------------------------

    // ビットストリームを扱うクラス
    // 局面の符号化を行なうときに、これがあると便利

    // データを格納するメモリを事前にセットする。
    // そのメモリは0クリアされているものとする。
    void  set_data(uint8_t* data_) { data = data_; reset(); }

    // set_data()で渡されたポインタの取得。
    uint8_t* get_data() const { return data; }

    // カーソルの取得。
    int get_cursor() const { return bit_cursor; }

    // カーソルのリセット
    void reset() { bit_cursor = 0; }

    // ストリームに1bit書き出す。
    // bは非0なら1を書き出す。0なら0を書き出す。
    FORCE_INLINE void write_one_bit(int b)
    {
        if (b)
            data[bit_cursor / 8] |= 1 << (bit_cursor & 7);

        ++bit_cursor;
    }

    // ストリームから1ビット取り出す。
    FORCE_INLINE int read_one_bit()
    {
        int b = (data[bit_cursor / 8] >> (bit_cursor & 7)) & 1;
        ++bit_cursor;

        return b;
    }

    // nビットのデータを書き出す
    // データはdの下位から順に書き出されるものとする。
    void write_n_bit(int d, int n)
    {
        for (int i = 0; i < n; ++i)
            write_one_bit(d & (1 << i));
    }

    // nビットのデータを読み込む
    // write_n_bit()の逆変換。
    int read_n_bit(int n)
    {
        int result = 0;
        for (int i = 0; i < n; ++i)
            result |= read_one_bit() ? (1 << i) : 0;

        return result;
    }

private:
    // 次に読み書きすべきbit位置。
    int bit_cursor;

    // データの実体
    uint8_t* data;
};


//  ハフマン符号化
//   ※　 なのはminiの符号化から、変換が楽になるように単純化。
//
//   盤上の1升(NO_PIECE以外) = 2～6bit ( + 成りフラグ1bit+ 先後1bit )
//   手駒の1枚               = 1～5bit ( + 成りフラグ1bit+ 先後1bit )
//
//    空     xxxxx0 + 0    (none)
//    歩     xxxx01 + 2    xxxx0 + 2
//    香     xx0011 + 2    xx001 + 2
//    桂     xx1011 + 2    xx101 + 2
//    銀     xx0111 + 2    xx011 + 2
//    金     x01111 + 1    x0111 + 1 // 金は成りフラグはない。
//    角     011111 + 2    01111 + 2
//    飛     111111 + 2    11111 + 2
//
// すべての駒が盤上にあるとして、
//     空 81 - 40駒 = 41升 = 41bit
//     歩      4bit*18駒   = 72bit
//     香      6bit* 4駒   = 24bit
//     桂      6bit* 4駒   = 24bit
//     銀      6bit* 4駒   = 24bit            
//     金      6bit* 4駒   = 24bit
//     角      8bit* 2駒   = 16bit
//     飛      8bit* 2駒   = 16bit
//                          -------
//                          241bit + 1bit(手番) + 7bit×2(王の位置先後) = 256bit
//
// 盤上の駒が手駒に移動すると盤上の駒が空になるので盤上のその升は1bitで表現でき、
// 手駒は、盤上の駒より1bit少なく表現できるので結局、全体のbit数に変化はない。
// ゆえに、この表現において、どんな局面でもこのbit数で表現できる。
// 手駒に成りフラグは不要だが、これも含めておくと盤上の駒のbit数-1になるので
// 全体のbit数が固定化できるのでこれも含めておくことにする。

struct HuffmanedPiece
{
    int code; // どうコード化されるか
    int bits; // 何bit専有するのか
};

HuffmanedPiece huffman_table[] =
{
    { 0x00,1 }, // NO_PIECE
    { 0x1f,6 }, // BISHOP
    { 0x3f,6 }, // ROOK
    { 0x01,2 }, // PAWN
    { 0x03,4 }, // LANCE
    { 0x0b,4 }, // KNIGHT
    { 0x07,4 }, // SILVER
    { 0x0f,5 }, // GOLD
};

// sfenを圧縮/解凍するためのクラス
// sfenはハフマン符号化をすることで256bit(32bytes)にpackできる。
// このことはなのはminiにより証明された。上のハフマン符号化である。
//
// 内部フォーマット = 手番1bit+王の位置7bit*2 + 盤上の駒(ハフマン符号化) + 手駒(ハフマン符号化)
//
struct SfenPacker
{
    // sfenをpackしてdata[32]に格納する。
    void pack(const Board& b)
    {
        //    cout << b;

        memset(data, 0, 32 /* 256bit */);
        stream.set_data(data);

        // 手番
        stream.write_one_bit((int)(b.turn()));

        // 先手玉、後手玉の位置、それぞれ7bit
        for (auto c : Turns)
            stream.write_n_bit(b.kingSquare(c), 7);

        // 盤面の駒は王以外はそのまま書き出して良し！
        for (auto sq : Squares)
        {
            // 盤上の玉以外の駒をハフマン符号化して書き出し
            Piece pc = b.piece(sq);

            if (typeOf(pc) == KING)
                continue;

            write_board_piece_to_stream(pc);
        }

        // 手駒をハフマン符号化して書き出し
        for (auto c : Turns)
            for (PieceType pr : HandPiece)
            {
                int n = b.hand(c).count(pr);

                // この駒、n枚持ってるよ
                for (int i = 0; i < n; ++i)
                    write_hand_piece_to_stream(toPiece(pr, c));
            }

        // 綺麗に書けた..気がする。

        // 全部で256bitのはず。(普通の盤面であれば)
        assert(stream.get_cursor() == 256);
    }

    // data[32]をsfen化して返す。
    std::string unpack()
    {
        stream.set_data(data);

        // 盤上の81升
        Piece board[81];
        memset(board, 0, sizeof(Piece) * 81);

        // 手番
        Turn turn = (Turn)stream.read_one_bit();

        // まず玉の位置
        for (auto c : Turns)
            board[stream.read_n_bit(7)] = toPiece(KING, c);

        // 盤上の駒
        for (auto sq : Squares)
        {
            // すでに玉がいるようだ
            if (typeOf(board[sq]) == KING)
                continue;

            board[sq] = read_board_piece_from_stream();

            //cout << sq << ' ' << board[sq] << ' ' << stream.get_cursor() << endl;

            assert(stream.get_cursor() <= 256);
        }

        // 手駒
        Hand hand[2];

        while (stream.get_cursor() != 256)
        {
            // 256になるまで手駒が格納されているはず
            auto pc = read_hand_piece_from_stream();
            hand[turnOf(pc)].plus(nativeType(pc));
        }

        // boardとhandが確定した。これで局面を構築できる…かも。
        // Board::sfen()は、board,hand,side_to_move,game_plyしか参照しないので
        // 無理やり代入してしまえば、sfen()で文字列化できるはず。

        return Board::sfenFromRawdata(board, hand, turn, 0);
    }

    // pack()でpackされたsfen(256bit = 32bytes)
    // もしくはunpack()でdecodeするsfen
    uint8_t *data; // uint8_t[32];

              //private:
              // Board::set_from_packed_sfen(uint8_t data[32])でこれらの関数を使いたいので筋は悪いがpublicにしておく。

    BitStream stream;

    // 盤面の駒をstreamに出力する。
    void write_board_piece_to_stream(Piece pc)
    {
        // 駒種
        PieceType pr = nativeType(pc);
        auto c = huffman_table[pr];
        stream.write_n_bit(c.code, c.bits);

        if (pc == NO_PIECE_TYPE)
            return;

        // 成りフラグ
        // (金はこのフラグはない)
        if (pr != GOLD)
            stream.write_one_bit(isPromoted(pc) ? 1 : 0);

        // 先後フラグ
        stream.write_one_bit(turnOf(pc));
    }

    // 手駒をstreamに出力する
    void write_hand_piece_to_stream(Piece pc)
    {
        assert(pc != EMPTY);

        // 駒種
        PieceType pr = nativeType(pc);
        auto c = huffman_table[pr];
        stream.write_n_bit(c.code >> 1, c.bits - 1);

        // 金以外は手駒であっても不成を出力して、盤上の駒のbit数-1を保つ
        if (pr != GOLD)
            stream.write_one_bit(false);

        // 先後フラグ
        stream.write_one_bit(turnOf(pc));
    }

    // 盤面の駒を1枚streamから読み込む
    Piece read_board_piece_from_stream()
    {
        PieceType pr = NO_PIECE_TYPE;
        int code = 0, bits = 0;
        while (true)
        {
            code |= stream.read_one_bit() << bits;
            ++bits;

            assert(bits <= 6);

            // ハフマン符号のどれと一致しているのかを調べる。
            for (pr = NO_PIECE_TYPE; pr < KING; ++pr)
                if (huffman_table[pr].code == code
                    && huffman_table[pr].bits == bits)
                    goto Found;
        }
    Found:;
        if (pr == NO_PIECE_TYPE)
            return Piece(pr);

        // 成りフラグ
        // (金はこのフラグはない)
        bool promote = (pr == GOLD) ? false : stream.read_one_bit();

        // 先後フラグ
        Turn c = (Turn)stream.read_one_bit();

        return toPiece(promote ? promotePieceType(pr) : pr, c);
    }

    // 手駒を1枚streamから読み込む
    Piece read_hand_piece_from_stream()
    {
        PieceType pr = NO_PIECE_TYPE;
        int code = 0, bits = 0;
        while (true)
        {
            code |= stream.read_one_bit() << bits;
            ++bits;

            assert(bits <= 6);

            for (pr = BISHOP; pr < KING; ++pr)
                if ((huffman_table[pr].code >> 1) == code
                    && (huffman_table[pr].bits - 1) == bits)
                    goto Found;
        }
    Found:;
        assert(pr != NO_PIECE_TYPE);

        // 金以外であれば成りフラグを1bit捨てる
        if (pr != GOLD)
            stream.read_one_bit();

        // 先後フラグ
        Turn c = (Turn)stream.read_one_bit();

        return toPiece(pr, c);
    }
};

// 高速化のために直接unpackする関数を追加。かなりしんどい。
// packer::unpack()とBoard::set()とを合体させて書く。
template <bool Test>
void Board::setFromPackedSfen(uint8_t data[32])
{
    SfenPacker packer;
    auto& stream = packer.stream;
    stream.set_data(data);

    clear();

    // 手番
    turn_ = (Turn)stream.read_one_bit();

    // PieceListを更新する上で、どの駒がどこにあるかを設定しなければならないが、
    // それぞれの駒をどこまで使ったかのカウンター
    PieceNo piece_no_count[KING] =
    {
        PIECE_NO_ZERO, PIECE_NO_BISHOP, PIECE_NO_ROOK, PIECE_NO_PAWN, PIECE_NO_LANCE,
        PIECE_NO_KNIGHT, PIECE_NO_SILVER, PIECE_NO_GOLD
    };
#ifdef USE_EVAL
    eval_list_.clear();
#endif
    // まず玉の位置
    for (auto c : Turns)
    {
#ifdef GENERATED_SFEN_BY_FILESQ
        board_[fileSq((Square)stream.read_n_bit(7))] = toPiece(KING, c);
#else
        board_[stream.read_n_bit(7)] = toPiece(KING, c);
#endif
    }

    // 盤上の駒
#ifdef GENERATED_SFEN_BY_FILESQ
    for (Square sq = SQ_11; sq != SQ_MAX; sq = nextSq(sq))
#else
    for (auto sq : Squares)
#endif
    {
        sq = (Square)toEvalSq(sq);

        // すでに玉がいるようだ
        Piece pc;
        if (typeOf(board_[sq]) != KING)
        {
            assert(board_[sq] == EMPTY);
            pc = packer.read_board_piece_from_stream();
        }
        else
        {
            pc = board_[sq];
            board_[sq] = EMPTY; // いっかい取り除いておかないとput_piece()でASSERTに引っかかる。
        }

        // 駒がない場合もあるのでその場合はスキップする。
        if (pc == EMPTY)
            continue;

        PieceNo piece_no =
            (pc == B_KING) ? PIECE_NO_BKING : // 先手玉
            (pc == W_KING) ? PIECE_NO_WKING : // 後手玉
            piece_no_count[nativeType(pc)]++; // それ以外

        setPiece(pc, sq, piece_no);

        //std::cout << sq << ' ' << board_[sq] << ' ' << stream.get_cursor() << std::endl;

        assert(stream.get_cursor() <= 256);
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
    bb_gold_ = bbType(GOLD, PRO_PAWN, PRO_LANCE, PRO_KNIGHT, PRO_SILVER);

#ifdef USE_EVAL
    int i = 0;
    Piece lastPc = EMPTY;
#endif

    int p_num[W_KING] = { 0 };

    while (stream.get_cursor() != 256)
    {
        // 256になるまで手駒が格納されているはず
        auto pc = packer.read_hand_piece_from_stream();
        PieceType rpc = nativeType(pc);
        hand_[turnOf(pc)].plus(rpc);

#ifdef USE_EVAL
        // FV38などではこの個数分だけpieceListに突っ込まないといけない。
        PieceNo piece_no = piece_no_count[rpc]++;
        assert(isOK(piece_no));
        eval_list_.putPiece(piece_no, turnOf(pc), rpc, p_num[pc]++);
#endif
    }

    setState(st_);

#ifdef USE_EVAL
    // 時間がかかるので、テスト時には評価関数を呼び出さない。
    if (!Test)
        Eval::computeEval(*this);
#endif
    assert(verify());
}

template void Board::setFromPackedSfen<true>(uint8_t data[32]);
template void Board::setFromPackedSfen<false>(uint8_t data[32]);

// 盤面と手駒、手番を与えて、そのsfenを返す。
std::string Board::sfenFromRawdata(Piece board[81], Hand hands[2], Turn turn, int gamePly_)
{
    // 内部的な構造体にコピーして、sfen()を呼べば、変換過程がそこにしか依存していないならば
    // これで正常に変換されるのでは…。
    Board b;

    memcpy(b.board_, board, sizeof(Piece) * 81);
    memcpy(b.hand_, hands, sizeof(Hand) * 2);
    b.turn_ = turn;
    b.ply_ = gamePly_;

    return b.sfen();

    // ↑の実装、美しいが、いかんせん遅い。
    // 棋譜を大量に読み込ませて学習させるときにここがボトルネックになるので直接unpackする関数を書く。
}

void Board::sfenPack(uint8_t data[32]) const
{
    SfenPacker sp;
    sp.data = data;
    sp.pack(*this);
}

std::string Board::sfenUnPack(uint8_t data[32])
{
    SfenPacker sp;
    sp.data = data;
    return sp.unpack();
}

#endif