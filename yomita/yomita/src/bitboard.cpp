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

#include "bitboard.h"
#include "bitop.h"
#include <fstream>

#ifdef HAVE_BMI2
__m256i YMM_TO[SQ_MAX];
#endif

// turn側から見て敵の陣地だけを取り出すマスク定数
const Bitboard BB_ENEMY_MASK[TURN_MAX] = { Bitboard(0x7ffffffULL, 0x1ffULL), Bitboard(0x7fc0000000000000ULL, 0x7ffffff000000000ULL) };

// turn側から見て敵の陣地 + 1段を取り出すマスク定数
const Bitboard BB_PROMOTE_MASK[TURN_MAX] = { Bitboard(0xfffffffffULL, 0x3ffffULL), Bitboard(0x7fffe00000000000ULL, 0x7ffffffff8000000ULL) };

// Squareに駒がいるとして、RANK_2からRANK_8までを64bit変数として見た時調べなければならないビット
const uint64_t PEXT_MASK_FILE[SQ_MAX] =
{
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
    0x40201008040201ULL << 0, 0x40201008040201ULL << 1, 0x40201008040201ULL << 2, 0x40201008040201ULL << 3, 0x40201008040201ULL << 4,
    0x40201008040201ULL << 5, 0x40201008040201ULL << 6, 0x40201008040201ULL << 7, 0x40201008040201ULL << 8,
};

const uint64_t PEXT_MASK_DIAG[SQ_MAX] =
{
    0x2008020080200802ULL, 0x10040100401004ULL, 0x8020080200aULL, 0x401004414ULL, 0x2088828ULL, 0x10111050ULL, 0x20202020a0ULL, 0x404040404040ULL, 0x80808080808080ULL,
    0x1004010040100400ULL, 0x2008020080200800ULL, 0x10040100401400ULL, 0x80200882800ULL, 0x411105000ULL, 0x202220a000ULL, 0x404040414000ULL, 0x80808080808000ULL, 0x101010101010000ULL,
    0x802008020080002ULL, 0x1004010040100004ULL, 0x200802008028000aULL, 0x10040110500014ULL, 0x82220a00028ULL, 0x404441400050ULL, 0x808080828000a0ULL, 0x101010101000040ULL, 0x202020202000080ULL,
    0x401004010000404ULL, 0x802008020000808ULL, 0x1004010050001410ULL, 0x20080220a0002822ULL, 0x10444140005044ULL, 0x8088828000a088ULL, 0x101010500014010ULL, 0x202020200008020ULL, 0x404040400010040ULL,
    0x200802000080808ULL, 0x401004000101010ULL, 0x80200a000282020ULL, 0x1004414000504440ULL, 0x2088828000a08882ULL, 0x111050001411004ULL, 0x2020a0002802008ULL, 0x404040001004010ULL, 0x808080002008020ULL,
    0x100400010101010ULL, 0x200800020202020ULL, 0x401400050404040ULL, 0x8828000a0888080ULL, 0x1105000141110400ULL, 0x220a000282200802ULL, 0x414000500401004ULL, 0x808000200802008ULL, 0x1010000401004010ULL,
    0x80002020202020ULL, 0x100004040404040ULL, 0x28000a080808080ULL, 0x500014111010000ULL, 0xa00028222080000ULL, 0x1400050440100400ULL, 0x28000a0080200802ULL, 0x1000040100401004ULL, 0x2000080200802008ULL,
    0x404040404040ULL, 0x808080808080ULL, 0x1410101010000ULL, 0x2822202000000ULL, 0x5044410000000ULL, 0xa088020080000ULL, 0x14010040100400ULL, 0x8020080200802ULL, 0x10040100401004ULL,
    0x80808080808080ULL, 0x101010101010000ULL, 0x282020202000000ULL, 0x504440400000000ULL, 0xa08882000000000ULL, 0x1411004010000000ULL, 0x2802008020080000ULL, 0x1004010040100400ULL, 0x2008020080200802ULL,
};

Bitboard BB_PAWN_ATTACKS  [TURN_MAX][SQ_MAX];
Bitboard BB_KNIGHT_ATTACKS[TURN_MAX][SQ_MAX];
Bitboard BB_SILVER_ATTACKS[TURN_MAX][SQ_MAX];
Bitboard BB_GOLD_ATTACKS  [TURN_MAX][SQ_MAX];
Bitboard BB_KING_ATTACKS            [SQ_MAX];
Bitboard ROOK_STEP_ATTACK[SQ_MAX];
Bitboard BISHOP_STEP_ATTACK[SQ_MAX];

Bitboard BISHOP_ATTACK[20224];
int      BISHOP_ATTACK_INDEX[SQ_MAX];
Bitboard BB_FILE_ATTACKS [SQ_MAX][128];
Bitboard BB_RANK_ATTACKS [SQ_MAX][128];

Bitboard BB_LANCE_CHECKS [TURN_MAX][SQ_MAX];
Bitboard BB_KNIGHT_CHECKS[TURN_MAX][SQ_MAX];
Bitboard BB_SILVER_CHECKS[TURN_MAX][SQ_MAX];
Bitboard BB_GOLD_CHECKS  [TURN_MAX][SQ_MAX];

// 何も邪魔な駒がないときの利き
Bitboard BB_LANCE_ATTACKS[TURN_MAX][SQ_MAX]; 
Bitboard BB_ROOK_ATTACKS           [SQ_MAX];

Bitboard BB_EXCEPT_PAWN_FILE_MASK[TURN_MAX][512];

Bitboard BB_OBSTACLE[SQ_MAX][SQ_MAX];
Bitboard BB_LINE[SQ_MAX][SQ_MAX];

RelationType SQUARE_RELATIONSHIP[SQ_MAX][SQ_MAX];

namespace
{
    // 角の利きテーブルの初期化。Aperyのコードを参考にしています。
    Bitboard bishopBlockMaskCalc(const Square sq)
    {
        Bitboard result = allZeroMask();

        for (auto s : Squares)
            if (abs(rankOf(sq) - rankOf(s)) == abs(fileOf(sq) - fileOf(s)))
                result ^= s;

        return result & ~(mask(FILE_1) | mask(FILE_9) | mask(RANK_1) | mask(RANK_9) | mask(sq));
    }

    Bitboard attackCalc(const Square square, const Bitboard& occupied)
    {
        Bitboard result = allZeroMask();

        for (Square delta : { DELTA_NE, DELTA_SE, DELTA_SW, DELTA_NW })
        {
            for (Square sq = square + delta;
                isOK(sq) && abs(fileOf(sq - delta) - fileOf(sq)) == 1; // sq + deltaが盤面内か。二つ目の条件は盤面を一周していないかの判定
                sq += delta)
            {
                result ^= sq;

                // ぶつかったら終了
                if (occupied & sq)
                    break;
            }
        }

        return result;
    }

    // 引数のindexをbits桁の2進数としてみなす。
    // 与えられたmask(1の数がbitsだけある)に対して、1のbitのいくつかを(indexの値に従って)0にする。
    Bitboard indexToOccupied(const int index, const int bits, Bitboard mask)
    {
        auto result = allZeroMask();

        for (int i = 0; i < bits; ++i)
        {
            const Square sq = mask.firstOne();

            if (index & (1 << i))
                result ^= sq;
        }

        return result;
    };

    uint64_t occupiedToIndex(const Bitboard& block, const Bitboard& mask)
    {
        return pext(block.merge(), mask.merge());
    }

    void initBishop()
    {
        // 各マスのbishopが利きを調べる必要があるマスの数
        const int BISHOP_BLOCK_BITS[81] =
        {
            7,  6,  6,  6,  6,  6,  6,  6,  7,
            6,  6,  6,  6,  6,  6,  6,  6,  6,
            6,  6,  8,  8,  8,  8,  8,  6,  6,
            6,  6,  8, 10, 10, 10,  8,  6,  6,
            6,  6,  8, 10, 12, 10,  8,  6,  6,
            6,  6,  8, 10, 10, 10,  8,  6,  6,
            6,  6,  8,  8,  8,  8,  8,  6,  6,
            6,  6,  6,  6,  6,  6,  6,  6,  6,
            7,  6,  6,  6,  6,  6,  6,  6,  7
        };

        int index = 0;

        for (auto sq : Squares)
        {
            // 調べる必要がある場所
            const Bitboard block = bishopBlockMaskCalc(sq);

            assert(block.merge() == PEXT_MASK_DIAG[sq]);

            BISHOP_ATTACK_INDEX[sq] = index;

            // 調べる必要があるマスの数
            const int num1s = BISHOP_BLOCK_BITS[sq];

            // パターンは2のnum1s乗通りある。
            for (int i = 0; i < (1 << num1s); ++i)
            {
                const Bitboard occupied = indexToOccupied(i, num1s, block);
                BISHOP_ATTACK[index + occupiedToIndex(occupied & block, block)] = attackCalc(sq, occupied);
            }

            index += 1 << BISHOP_BLOCK_BITS[sq];
        }
    }
}

// デバッグ用。ビットボードのレイアウトを見たいときに使う
std::ostream& operator << (std::ostream& os, const Bitboard& b)
{
    for (int index = 0; index <= 1; index++)
    {
        os << "b_[" << index << "]:\n";

        if (index == 1)
            for (int i = 0; i < 18; i++)
            {
                os << "- ";

                if ((i + 1) % 9 == 0)
                    os << std::endl;
            }

        uint64_t mask = 1;

        for (int i = 0; i < 63; i++)
        {
            os << (b.b(index) & mask ? "1 " : "0 ");

            if ((i + 1) % 9 == 0)
                os << std::endl;

            mask <<= 1;
        }

        if (index == 0)
            for (int i = 0; i < 18; i++)
            {
                os << "- ";

                if ((i + 1) % 9 == 0)
                    os << std::endl;
            }
        os << std::endl;
    }

    os << "Bitboard(" << "0x" << std::hex << b.b(0) << "ULL, " << "0x" << std::hex << b.b(1) << "ULL),\n" << std::endl;
    return os;
}

// pbbで渡されたBitboardに、rank, fileが示す位置のビットを立てる。
// rank,fileの範囲チェックも行うので、rankやfileが9以上0未満のときは何も行わずにreturnする。
void setAttacks(Rank rank, File file, Bitboard* pbb)
{
    if (isOK(rank) && isOK(file))
        *pbb |= sqOf(file, rank);
}

// 位置関係を初期化する。
void initRelation()
{
    // これらのビットボードはこの関数でしか使わない。
    Bitboard bb_right[81];    // sq(盤上の位置)の→のsq
    Bitboard bb_leftdown[81]; // 左下
    Bitboard bb_down[81];	  // ↓
    Bitboard bb_rightdown[81];// 右下
    Bitboard bb_left[81];	  // ←
    Bitboard bb_rightup[81];  // 右上
    Bitboard bb_up[81];		  // ↑
    Bitboard bb_leftup[81];   // 左上

    for (auto rank : Ranks)
    {
        for (auto file : Files)
        {
            Square pos = sqOf(file, rank);

            // まずは0クリア
            bb_right[pos].clear();
            bb_leftdown[pos].clear();
            bb_down[pos].clear();
            bb_rightdown[pos].clear();
            bb_left[pos].clear();
            bb_rightup[pos].clear();
            bb_up[pos].clear();
            bb_leftup[pos].clear();

            for (int i = 1; i < 9; i++)
            {
                setAttacks(rank, file + i, bb_right + pos); // posより右のビットをすべて立てる
                setAttacks(rank - i, file - i, bb_leftup + pos);
                setAttacks(rank - i, file, bb_up + pos);
                setAttacks(rank - i, file + i, bb_rightup + pos);
                setAttacks(rank, file - i, bb_left + pos);
                setAttacks(rank + i, file + i, bb_rightdown + pos);
                setAttacks(rank + i, file, bb_down + pos);
                setAttacks(rank + i, file - i, bb_leftdown + pos);
            }
        }
    }
    // bb_方向の設定終わり

    for (auto from : Squares)
    {
        for (auto to : Squares) // fromに対するtoの位置関係をとりあえず0に初期化
            SQUARE_RELATIONSHIP[from][to] = DIRECT_MISC;

        // from, toは横の関係にある。
        for(auto to : bb_right[from] | bb_left[from])
            SQUARE_RELATIONSHIP[from][to] = DIRECT_RANK;

        // fromの左下、右上のビットを立て、[from][to]に
        // direc_diag1(右上から左下、または左下から右上)の関係だ、という情報を格納
        for (auto to : bb_leftdown[from] | bb_rightup[from])
            SQUARE_RELATIONSHIP[from][to] = DIRECT_DIAG1;

        // fromとtoは縦の位置関係
        for (auto to : bb_up[from] | bb_down[from])
            SQUARE_RELATIONSHIP[from][to] = DIRECT_FILE;

        // fromとtoは左上から右下の位置関係
        for (auto to : bb_leftup[from] | bb_rightdown[from])
            SQUARE_RELATIONSHIP[from][to] = DIRECT_DIAG2;
    }
    // 位置関係の設定終わり

    // 障害物の設定(BB_OBSTACLE)
    // 障害物とは、toからfromの間にあるマスが1になっていることである。
    for (auto from : Squares)
    {
        for (auto to : Squares)
        {
            BB_OBSTACLE[from][to].clear();

            // toがfromより右もしくは下にある
            if (to > from)
            {
                switch (relation(from, to))
                {
                // fromから見た位置関係である
                case DIRECT_RANK: // 右
                    BB_OBSTACLE[from][to] = bb_right[from] ^ bb_right[to - 1];
                    break;
                case DIRECT_FILE: // 下
                    BB_OBSTACLE[from][to] = bb_down[from] ^ bb_down[to - 9];
                    break;
                case DIRECT_DIAG1:// 左下
                    BB_OBSTACLE[from][to] = bb_leftdown[from] ^ bb_leftdown[to - 8];
                    break;
                case DIRECT_DIAG2:// 右下
                    BB_OBSTACLE[from][to] = bb_rightdown[from] ^ bb_rightdown[to - 10];
                    break;
                case DIRECT_MISC: // どの関係にもない
                    break;
                default:
                    assert(false);
                }
            }
            else // toがfromより右もしくは上にある
            {
                switch (relation(from, to))
                {
                case DIRECT_RANK: // 左
                    BB_OBSTACLE[from][to] = bb_left[from] ^ bb_left[to + 1];
                    break;
                case DIRECT_FILE: // 上
                    BB_OBSTACLE[from][to] = bb_up[from] ^ bb_up[to + 9];
                    break;
                case DIRECT_DIAG1:// 右上
                    BB_OBSTACLE[from][to] = bb_rightup[from] ^ bb_rightup[to + 8];
                    break;
                case DIRECT_DIAG2:// 左上
                    BB_OBSTACLE[from][to] = bb_leftup[from] ^ bb_leftup[to + 10];
                    break;
                case DIRECT_MISC: // どの関係にもない
                    break;
                default:
                    assert(false);
                }
            }
        }
    }
    // 障害物の設定終わり

    for (auto sq1 : Squares)
    {
        for (auto sq2 : Squares)
        {
            BB_LINE[sq1][sq2].clear();
            switch (relation(sq1, sq2))
            {
            case DIRECT_RANK: // 左
                BB_LINE[sq1][sq2] = bb_right[sq1] | bb_left[sq1];
                break;
            case DIRECT_FILE: // 上
                BB_LINE[sq1][sq2] = bb_up[sq1] | bb_down[sq1];
                break;
            case DIRECT_DIAG1:// 右上
                BB_LINE[sq1][sq2] = bb_rightup[sq1] | bb_leftdown[sq1];
                break;
            case DIRECT_DIAG2:// 左上
                BB_LINE[sq1][sq2] = bb_leftup[sq1] | bb_rightdown[sq1];
                break;
            case DIRECT_MISC: // どの関係にもない
                break;
            default:assert(false);
            }
        }
    }
}

// すべての小駒の種類について、利いている場所が1になっているbitboardを用意。
// 盤上のすべての位置(1一に居る場合から9九に居る場合まで)分だけ用意する。
// 飛車角香の利きは、障害物の存在するパターン数だけ用意。
void initAttacks()
{
    // 飛び駒以外の駒の利きテーブルをセット
    Bitboard bb;

    for (auto rank : Ranks)
    {
        for (auto file : Files)
        {
            // 先手の歩
            bb.clear();
            setAttacks(rank - 1, file, &bb);
            BB_PAWN_ATTACKS[BLACK][rank * 9 + file] = bb;

            // 後手の歩
            bb.clear();
            setAttacks(rank + 1, file, &bb);
            BB_PAWN_ATTACKS[WHITE][rank * 9 + file] = bb;

            // 先手_桂馬
            bb.clear();
            setAttacks(rank - 2, file + 1, &bb);
            setAttacks(rank - 2, file - 1, &bb);
            BB_KNIGHT_ATTACKS[BLACK][rank * 9 + file] = bb;

            // 後手_桂馬
            bb.clear();
            setAttacks(rank + 2, file + 1, &bb);
            setAttacks(rank + 2, file - 1, &bb);
            BB_KNIGHT_ATTACKS[WHITE][rank * 9 + file] = bb;

            // 先手_銀
            bb.clear();
            setAttacks(rank + 1, file - 1, &bb);
            setAttacks(rank - 1, file - 1, &bb);
            setAttacks(rank - 1, file, &bb);
            setAttacks(rank - 1, file + 1, &bb);
            setAttacks(rank + 1, file + 1, &bb);
            BB_SILVER_ATTACKS[BLACK][rank * 9 + file] = bb;

            // 後手_銀
            bb.clear();
            setAttacks(rank - 1, file + 1, &bb);
            setAttacks(rank + 1, file + 1, &bb);
            setAttacks(rank + 1, file, &bb);
            setAttacks(rank + 1, file - 1, &bb);
            setAttacks(rank - 1, file - 1, &bb);
            BB_SILVER_ATTACKS[WHITE][rank * 9 + file] = bb;

            // 先手_金
            bb.clear();
            setAttacks(rank, file - 1, &bb);
            setAttacks(rank - 1, file - 1, &bb);
            setAttacks(rank - 1, file, &bb);
            setAttacks(rank - 1, file + 1, &bb);
            setAttacks(rank, file + 1, &bb);
            setAttacks(rank + 1, file, &bb);
            BB_GOLD_ATTACKS[BLACK][rank * 9 + file] = bb;

            // 後手_金
            bb.clear();
            setAttacks(rank, file + 1, &bb);
            setAttacks(rank + 1, file + 1, &bb);
            setAttacks(rank + 1, file, &bb);
            setAttacks(rank + 1, file - 1, &bb);
            setAttacks(rank, file - 1, &bb);
            setAttacks(rank - 1, file, &bb);
            BB_GOLD_ATTACKS[WHITE][rank * 9 + file] = bb;

            // 玉
            bb.clear();
            setAttacks(rank - 1, file, &bb);
            setAttacks(rank + 1, file, &bb);
            setAttacks(rank + 1, file - 1, &bb);
            setAttacks(rank + 1, file + 1, &bb);
            setAttacks(rank - 1, file - 1, &bb);
            setAttacks(rank - 1, file + 1, &bb);
            setAttacks(rank, file + 1, &bb);
            setAttacks(rank, file - 1, &bb);
            BB_KING_ATTACKS[rank * 9 + file] = bb;


            // BB_xxx_ATTACKSとは
            // file = 筋 , file_attacks =  縦方向の利きの意味。
            // BB_FILE_ATTACKS[sq][w]
            // を与えると、sqから上下に対する利きが一発で求まる。また、
            // w = そのsqの属する列の、駒がある位置が1であるbit pattern(7bit)
            // -
            // 0 ←ここがpatの0ビット目(2段目)
            // 0
            // 0
            // 0			←pat
            // 0
            // 0
            // 0 ←ここがpatの6ビット目(8段目)
            // -

            // patはbit pattern (7bit) = 0～127の任意の数
            // patの0bit目が2段目、6bit目が8段目を表していることに注意。
            for (int pat = 0; pat < 128; pat++)
            {
                bb.clear();

                // irank + iの範囲は、1..8。すなわち2段から8段までの7bit
                // irankが2より小さいときはこの処理は行われない
                for (int i = 1; rank - i >= 0; i++)
                {
                    // 上方向へのスキャン
                    // rank = 1がpatの0ビット目
                    setAttacks(rank - i, file, &bb);

                    if ((pat << 1) & (1 << (rank - i)))
                        break;

                    // rankが減るとpatも減る→同じ関係性
                }

                // 下方向
                for (int i = 1; rank + i <= 8; i++)
                {
                    setAttacks(rank + i, file, &bb);

                    if ((pat << 1) & (1 << (rank + i)))
                        break;
                }

                BB_FILE_ATTACKS[rank * 9 + file][pat] = bb;

                // BB_FILE_ATTACKS終わり

                bb.clear();

                // 左方向
                for (int i = 1; file - i >= 0; i++)
                {
                    setAttacks(rank, file - i, &bb);

                    if ((pat << 1) & (1 << (file - i))) 
                        break;
                }

                // 右方向
                for (int i = 1; file + i <= 8; i++)
                {
                    setAttacks(rank, file + i, &bb);

                    if ((pat << 1) & (1 << (file + i))) 
                        break;
                }

                BB_RANK_ATTACKS[rank * 9 + file][pat] = bb;

                // BB_RANK_ATTACKS終わり
            }
        }
    }

    // 飛び駒の利き
    for (Square sq : Squares)
    {
        BB_LANCE_ATTACKS[BLACK][sq] = lanceAttack(BLACK, sq, allZeroMask());
        BB_LANCE_ATTACKS[WHITE][sq] = lanceAttack(WHITE, sq, allZeroMask());
        BB_ROOK_ATTACKS[sq]         = rookAttack(sq, allZeroMask());
        ROOK_STEP_ATTACK[sq]        = goldAttack(BLACK, sq) & goldAttack(WHITE, sq);
        BISHOP_STEP_ATTACK[sq]      = silverAttack(BLACK, sq) & silverAttack(WHITE, sq);
    }

    // 歩のいる9ビットに対する歩のいない筋が1に成っているbitboard。
    // 先手ならRANK_1、後手ならRANK_9を0にしておく。
    for (Turn t : Turns)
    {
        for (int pat = 0; pat < 512; pat++)
        {
            bb.clear();

            for (File file : Files)
                if (!(pat & (1 << file)))
                    bb |= mask(file);

            bb &= t == BLACK ? ~mask(RANK_1) : ~mask(RANK_9);

            BB_EXCEPT_PAWN_FILE_MASK[t][pat] = bb;
        }
    }
}

// 金、銀、桂、香について
// 各王の位置について王手することができる位置のビットが１になっているbitboardを用意する。
// 銀、桂、香は成も考慮しなければならない。香を動かして王手になるのは、成った時だけ。
void initChecks()
{
    Bitboard bb_check, bb;

    for (Turn turn : Turns)
    {
        for (Square king : Squares)
        {
            // 今、board[king]に王がいると仮定する

            // 金
            BB_GOLD_CHECKS[turn][king].clear();

            // 逆の手番側の金の利きを利用する。bb_checkが1になっているところに金を置けば、王手になる。
            // さらに、王手できる位置に移動できる位置を1にする。
            for (auto sq : BB_GOLD_ATTACKS[~turn][king])
                BB_GOLD_CHECKS[turn][king] |= BB_GOLD_ATTACKS[~turn][sq];

            // ここで、BB_GOLD_CHECKS[turn][king]は、王手することができる金の位置を表すbitboardである。
            // ただ、まだ王手になる金の位置もまじっているし、玉の位置も1になっている
            // 敵の金の利き + その金の位置を1にする。
            BB_GOLD_CHECKS[turn][king] &= ~(mask(king) | BB_GOLD_ATTACKS[~turn][king]);

            // 銀
            BB_SILVER_CHECKS[turn][king].clear();

            // turn側から見て敵の陣地 + 1段を取り出すマスク定数
            const Bitboard BB_PROMOTE_MASK[TURN_MAX] = { frontMask(BLACK, RANK_5), frontMask(WHITE, RANK_5) };

            for (auto sq : BB_SILVER_ATTACKS[~turn][king])
                BB_SILVER_CHECKS[turn][king] |= BB_SILVER_ATTACKS[~turn][sq];

            // ここで、BB_SILVER_CHECKS[turn][king]は、王手することができるturn側の銀の位置を示すが、王手になる銀の位置も混じっている。
            // また、成ることによって王手になる場所は考慮していない。

            // 敵の金の利きを利用する
            // 先手の場合なら、3段目に移動できる銀の場所 + 3→4段目に移動できる銀の場所
            // 3段目に移動できる銀の場所
            for (auto sq : BB_GOLD_ATTACKS[~turn][king] & BB_ENEMY_MASK[turn])
                BB_SILVER_CHECKS[turn][king] |= BB_SILVER_ATTACKS[~turn][sq];

            // 3→4段目に移動できる銀の場所
            for (auto sq : BB_GOLD_ATTACKS[~turn][king] & (BB_PROMOTE_MASK[turn] & ~BB_ENEMY_MASK[turn]))
                BB_SILVER_CHECKS[turn][king] |= BB_SILVER_ATTACKS[~turn][sq] & BB_ENEMY_MASK[turn];

            // 最後に、王手になる銀の位置と王の位置を0にする
            BB_SILVER_CHECKS[turn][king] &= ~(mask(king) | BB_SILVER_ATTACKS[~turn][king]);

            // 桂
            BB_KNIGHT_CHECKS[turn][king].clear();

            for (auto sq : BB_KNIGHT_ATTACKS[~turn][king])
                BB_KNIGHT_CHECKS[turn][king] |= BB_KNIGHT_ATTACKS[~turn][sq];

            for (auto sq : BB_GOLD_ATTACKS[~turn][king] & BB_ENEMY_MASK[turn])
                BB_KNIGHT_CHECKS[turn][king] |= BB_KNIGHT_ATTACKS[~turn][sq];

            // 香
            if (turn == BLACK)
            {
                // 7段目より上
                if (rankOf(king) <= RANK_7)
                {
                    // 玉の位置の一つ下を除いて、玉の位置より下が1になっているbitboard
                    // この位置に香車があると王手になってしまうが、駒を取りながら王手する手を生成するためにこのようにしておく
                    BB_LANCE_CHECKS[turn][king] = lanceAttackToEdge(WHITE, king + 9) & BB_FILE_ATTACKS[king][0];

                    // 敵陣
                    if (rankOf(king) <= RANK_3)
                    {
                        // 左端にないとき
                        if (king != SQ_91 && king != SQ_92 && king != SQ_93)
                        {
                            // 玉の位置から←に一つ移動した位置より下がすべて1になっているビットボード
                            BB_LANCE_CHECKS[turn][king] |= lanceAttackToEdge(WHITE, king - 1) & BB_FILE_ATTACKS[king - 1][0];
                        }
                        // 右端にないとき
                        if (king != SQ_11 && king != SQ_12 && king != SQ_13)
                        {
                            // 玉の位置から→に一つ移動した位置より下がすべて1になっているビットボード
                            BB_LANCE_CHECKS[turn][king] |= lanceAttackToEdge(WHITE, king + 1) & BB_FILE_ATTACKS[king + 1][0];
                        }
                    }
                }
                else
                {
                    // 先手陣の2段目以内に相手がいるなら、王手することはできない。										
                    BB_LANCE_CHECKS[turn][king].clear();
                }
            }
            else
            {
                // 3段目より下
                if (rankOf(king) >= RANK_3)
                {
                    // 玉の位置の一つ上を除いて、玉の位置より上が1になっているbitboard
                    BB_LANCE_CHECKS[turn][king] = lanceAttackToEdge(BLACK, king - 9) & BB_FILE_ATTACKS[king][0];

                    // 敵陣
                    if (rankOf(king) >= RANK_7)
                    {
                        // 左端にないとき
                        if (king != SQ_97 && king != SQ_98 && king != SQ_99)
                        {
                            // 玉の位置から←に一つ移動した位置より下がすべて1になっているビットボード
                            BB_LANCE_CHECKS[turn][king] |= lanceAttackToEdge(BLACK, king - 1) & BB_FILE_ATTACKS[king - 1][0];
                        }
                        // 左端にないとき
                        if (king != SQ_17 && king != SQ_18 && king != SQ_19)
                        {
                            // 玉の位置から→に一つ移動した位置より下がすべて1になっているビットボード
                            BB_LANCE_CHECKS[turn][king] |= lanceAttackToEdge(BLACK, king + 1) & BB_FILE_ATTACKS[king + 1][0];
                        }
                    }

                }
                else
                {
                    // 先手陣に相手がいるなら、王手することはできない。
                    BB_LANCE_CHECKS[turn][king].clear();
                }
            }
        }
    }
}

// 各種bitboardのセッティング。
// これは、bitboardを使うすべての関数の前準備となるものであるから、
// main()で一番最初に呼ばれなければならない。
void initTables()
{
    initBishop();
    initRelation();
    initAttacks();
    initChecks();

#ifdef HAVE_BMI2
    for (auto sq : Squares)
        YMM_TO[sq] = _mm256_set_epi64x(sq, sq, sq, sq);
#endif
}