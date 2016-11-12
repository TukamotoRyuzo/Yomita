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

#include "eval_krb.h"

#ifdef EVAL_KRB

#include <fstream>
#include "usi.h"
#include "board.h"

#define kpp (*et.kpp_)
#define rpp (*et.rpp_)
#define bpp (*et.bpp_)

namespace Eval
{
    EvalTable et;
    const size_t size = sizeof(kpp) + sizeof(rpp) + sizeof(bpp);
    const size_t mbsize = size >> 20;

    // 評価関数ファイルを読み込む
    void loadSub()
    {
        std::ifstream ifsKPP(path((std::string)USI::Options["EvalDir"], KPP_BIN), std::ios::binary);
        std::ifstream ifsRPP(path((std::string)USI::Options["EvalDir"], RPP_BIN), std::ios::binary);
        std::ifstream ifsBPP(path((std::string)USI::Options["EvalDir"], BPP_BIN), std::ios::binary);

        if (!ifsKPP || !ifsRPP || !ifsBPP)
            goto Error;

        ifsKPP.read(reinterpret_cast<char*>(kpp), sizeof(kpp));
        ifsRPP.read(reinterpret_cast<char*>(rpp), sizeof(rpp));
        ifsBPP.read(reinterpret_cast<char*>(bpp), sizeof(bpp));

#ifdef LEARN
        evalLearnInit();
#endif

#if 0
        for (auto sq : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; p1++)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; p2++)
                    kpp[sq][p1][p2] = 1;

        for (auto sq = SQ_ZERO; sq < SQ_MAX_PRO_HAND; sq++)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_gold_end; p1++)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_gold_end; p2++)
                {
                    rpp[sq][p1][p2] = 1;
                    bpp[sq][p1][p2] = 1;
                }
#endif
        return;

    Error:;
        std::cout << "\ninfo string open evaluation file failed.\n";
    }

    // KRBのスケール
    const int FV_SCALE = 32;

    // 駒割り以外の全計算
    Score computeEval(const Board& b)
    {
        // 0は180度回転していない、1は180度回転しているの意味
        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk1 = inverse(b.kingSquare(WHITE));

        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        EvalSum sum;
        sum.clear();

        // 玉の位置と飛車角の位置
        Square sq[6] =
        {
            sq_bk0, sq_wk1,
            b.rookSquare(0), b.rookSquare(1),
            b.bishopSquare(0), b.bishopSquare(1)
        };

        const bool is_white[4] =
        {
            sq[2] >= SQ_MAX_PRO_HAND,
            sq[3] >= SQ_MAX_PRO_HAND,
            sq[4] >= SQ_MAX_PRO_HAND,
            sq[5] >= SQ_MAX_PRO_HAND
        };

        const BonaPiece* list[6] = { list_fb, list_fw, };

        // squareは既にinverseされているのでinverseは必要ない。
        for (int i = 0; i < 4; i++)
        {
            if (is_white[i])
            {
                sq[i + 2] -= SQ_MAX_PRO_HAND;
                list[i + 2] = list_fw;
            }
            else
                list[i + 2] = list_fb;
        }

        for (int i = 0; i < PIECE_NO_BISHOP; i++)
            for (int j = 0; j < i; j++)
            {
                sum.sumKPP[BLACK] += kpp[sq_bk0][list_fb[i]][list_fb[j]];
                sum.sumKPP[WHITE] -= kpp[sq_wk1][list_fw[i]][list_fw[j]];
                sum.sumRPP[0] += rpp[sq[2]][list[2][i]][list[2][j]];
                sum.sumRPP[1] += rpp[sq[3]][list[3][i]][list[3][j]];
                sum.sumBPP[0] += bpp[sq[4]][list[4][i]][list[4][j]];
                sum.sumBPP[1] += bpp[sq[5]][list[5][i]][list[5][j]];
            }

        // kppを全計算
        for (int i = PIECE_NO_BISHOP; i < PIECE_NO_KING; i++)
            for (int j = 0; j < i; j++)
            {
                sum.sumKPP[BLACK] += kpp[sq_bk0][list_fb[i]][list_fb[j]];
                sum.sumKPP[WHITE] -= kpp[sq_wk1][list_fw[i]][list_fw[j]];
            }

        if (is_white[0])
            sum.sumRPP[0] = -sum.sumRPP[0];

        if (is_white[1])
            sum.sumRPP[1] = -sum.sumRPP[1];

        if (is_white[2])
            sum.sumBPP[0] = -sum.sumBPP[0];

        if (is_white[3])
            sum.sumBPP[1] = -sum.sumBPP[1];

        b.state()->sum = sum;

        return Score(sum.calcScore() / FV_SCALE);
    }

    // ある駒が動いたときの玉の差分計算をする関数
    // DirtyNum = 1 : 1枚目のDirtyについて処理
    // DirtyNum = 2 : 2枚目のDirtyについて処理
    // DirtyNum = 3 : 1, 2枚目のDirtyについて処理
    template <Turn T, int DirtyNum>
    void calcKingDiff(EvalSum& sum, Square sq, const BonaPiece* list, const DirtyPiece& dp)
    {
        static_assert(DirtyNum >= 1 && DirtyNum <= 3, "");

        const int k0 = T == BLACK ? dp.pre_piece[0].fb : dp.pre_piece[0].fw;
        const int k1 = T == BLACK ? dp.pre_piece[1].fb : dp.pre_piece[1].fw;
        const int k2 = T == BLACK ? dp.now_piece[0].fb : dp.now_piece[0].fw;
        const int k3 = T == BLACK ? dp.now_piece[1].fb : dp.now_piece[1].fw;

        auto dirty = dp.piece_no[1];
        int ret = 0;

        if (DirtyNum == 1)
        {
            auto dirty = dp.piece_no[0];

            for (int i = 0; i < dirty; ++i)
            {
                ret -= kpp[sq][k0][list[i]];
                ret += kpp[sq][k2][list[i]];
            }
            for (int i = dirty + 1; i < PIECE_NO_KING; ++i)
            {
                ret -= kpp[sq][k0][list[i]];
                ret += kpp[sq][k2][list[i]];
            }
        }
        else if (DirtyNum == 2)
        {
            auto dirty = dp.piece_no[1];

            for (int i = 0; i < dirty; ++i)
            {
                ret -= kpp[sq][k1][list[i]];
                ret += kpp[sq][k3][list[i]];
            }
            for (int i = dirty + 1; i < PIECE_NO_KING; ++i)
            {
                ret -= kpp[sq][k1][list[i]];
                ret += kpp[sq][k3][list[i]];
            }
        }
        else if (DirtyNum == 3)
        {
            auto dirty1 = dp.piece_no[0], dirty2 = dp.piece_no[1];

            if (dirty1 > dirty2)
                std::swap(dirty1, dirty2);

            for (int i = 0; i < dirty1; ++i)
            {
                ret -= kpp[sq][k0][list[i]];
                ret -= kpp[sq][k1][list[i]];
                ret += kpp[sq][k2][list[i]];
                ret += kpp[sq][k3][list[i]];
            }
            for (int i = dirty1 + 1; i < dirty2; ++i)
            {
                ret -= kpp[sq][k0][list[i]];
                ret -= kpp[sq][k1][list[i]];
                ret += kpp[sq][k2][list[i]];
                ret += kpp[sq][k3][list[i]];
            }
            for (int i = dirty2 + 1; i < PIECE_NO_KING; ++i)
            {
                ret -= kpp[sq][k0][list[i]];
                ret -= kpp[sq][k1][list[i]];
                ret += kpp[sq][k2][list[i]];
                ret += kpp[sq][k3][list[i]];
            }
        }

        sum.sumKPP[T] += T == BLACK ? ret : -ret;
    }

    // ある駒が動いたときの差分計算をする関数。ある駒は角以下の駒とする。
    // DirtyNum = 1 : 1枚目のDirtyについて処理
    // DirtyNum = 2 : 2枚目のDirtyについて処理
    // DirtyNum = 3 : 1, 2枚目のDirtyについて処理
    // SelectBitは0bit目からbkpp, wkpp, 0rpp, 1rpp, 0bpp, 1bppとする
    template <int DirtyNum, int SelectBit>
    void calcDiff(EvalSum& sum, Square* sq, const BonaPiece** list, const bool *is_white, const DirtyPiece& dp)
    {
        static_assert(DirtyNum >= 1 && DirtyNum <= 3, "");
#define ADD_BKPP(K0, K2) {\
assert(isOK2(K0) && isOK2(K2) && isOK2(list[0][i]));\
sum.sumKPP[BLACK] -= kpp[sq[0]][K0][list[0][i]];\
sum.sumKPP[BLACK] += kpp[sq[0]][K2][list[0][i]];}

#define ADD_WKPP(K0, K2) {\
assert(isOK2(K0) && isOK2(K2) && isOK2(list[1][i]));\
sum.sumKPP[WHITE] += kpp[sq[1]][K0][list[1][i]];\
sum.sumKPP[WHITE] -= kpp[sq[1]][K2][list[1][i]];}

#define ADD_0RPP(K0, K2) {\
assert(isOK3(K0) && isOK3(K2) && isOK2(list[2][i]));\
sum.sumRPP[BLACK] -= rpp[sq[2]][K0][list[2][i]];\
sum.sumRPP[BLACK] += rpp[sq[2]][K2][list[2][i]];}

#define ADD_1RPP(K0, K2) {\
assert(isOK3(K0) && isOK3(K2) && isOK2(list[3][i]));\
sum.sumRPP[WHITE] -= rpp[sq[3]][K0][list[3][i]];\
sum.sumRPP[WHITE] += rpp[sq[3]][K2][list[3][i]];}

#define ADD_0BPP(K0, K2) {\
assert(isOK3(K0) && isOK3(K2) && isOK2(list[4][i]));\
sum.sumBPP[BLACK] -= bpp[sq[4]][K0][list[4][i]];\
sum.sumBPP[BLACK] += bpp[sq[4]][K2][list[4][i]];}

#define ADD_1BPP(K0, K2) {\
assert(isOK3(K0) && isOK3(K2) && isOK2(list[5][i]));\
sum.sumBPP[WHITE] -= bpp[sq[5]][K0][list[5][i]];\
sum.sumBPP[WHITE] += bpp[sq[5]][K2][list[5][i]];}

#define BIT_IF_KPP(K0, K2, M0, M2)\
if (SelectBit & 1)ADD_BKPP(K0, K2);\
if (SelectBit & 2)ADD_WKPP(M0, M2);

#define BIT_IF_ALL(K0, K2, M0, M2, R00, R02, R10, R12, B00, B02, B10, B12)\
BIT_IF_KPP(K0, K2, M0, M2)\
if (SelectBit & 4)ADD_0RPP(R00, R02);\
if (SelectBit & 8)ADD_1RPP(R10, R12);\
if (SelectBit & 16)ADD_0BPP(B00, B02);\
if (SelectBit & 32)ADD_1BPP(B10, B12);

        int i = 0;

        // 符号反転しておくことで全部+すればよいことになって楽。
        if (SelectBit & 0x3c)
        {
            if (SelectBit & 4)
                if (is_white[0])
                    sum.sumRPP[0] = -sum.sumRPP[0];

            if (SelectBit & 8)
                if (is_white[1])
                    sum.sumRPP[1] = -sum.sumRPP[1];

            if (SelectBit & 16)
                if (is_white[2])
                    sum.sumBPP[0] = -sum.sumBPP[0];

            if (SelectBit & 32)
                if (is_white[3])
                    sum.sumBPP[1] = -sum.sumBPP[1];
        }

        if (DirtyNum == 1)
        {
            const BonaPiece k0 = dp.pre_piece[0].fb;
            const BonaPiece k2 = dp.now_piece[0].fb;
            const BonaPiece m0 = dp.pre_piece[0].fw;
            const BonaPiece m2 = dp.now_piece[0].fw;

            const BonaPiece r00 = is_white[0] ? m0 : k0;
            const BonaPiece r02 = is_white[0] ? m2 : k2;

            const BonaPiece r10 = is_white[1] ? m0 : k0;
            const BonaPiece r12 = is_white[1] ? m2 : k2;

            const BonaPiece b00 = is_white[2] ? m0 : k0;
            const BonaPiece b02 = is_white[2] ? m2 : k2;

            const BonaPiece b10 = is_white[3] ? m0 : k0;
            const BonaPiece b12 = is_white[3] ? m2 : k2;

            auto dirty = dp.piece_no[0];
            
            for (i = 0; i < dirty; ++i)
            {
                BIT_IF_ALL(k0, k2, m0, m2, r00, r02, r10, r12, b00, b02, b10, b12);
            }

            if (SelectBit & 0x3c)
            {
                for (++i; i < PIECE_NO_BISHOP; ++i)
                {
                    BIT_IF_ALL(k0, k2, m0, m2, r00, r02, r10, r12, b00, b02, b10, b12);
                }

                i--;
            }

            for (++i; i < PIECE_NO_KING; ++i)
            {
                BIT_IF_KPP(k0, k2, m0, m2);
            }
        }
        else if (DirtyNum == 2)
        {
            const BonaPiece k1 = dp.pre_piece[1].fb;
            const BonaPiece k3 = dp.now_piece[1].fb;
            const BonaPiece m1 = dp.pre_piece[1].fw;
            const BonaPiece m3 = dp.now_piece[1].fw;

            const BonaPiece r01 = is_white[0] ? m1 : k1;
            const BonaPiece r03 = is_white[0] ? m3 : k3;

            const BonaPiece r11 = is_white[1] ? m1 : k1;
            const BonaPiece r13 = is_white[1] ? m3 : k3;
        
            const BonaPiece b01 = is_white[2] ? m1 : k1;			
            const BonaPiece b03 = is_white[2] ? m3 : k3;
            
            const BonaPiece b11 = is_white[3] ? m1 : k1;
            const BonaPiece b13 = is_white[3] ? m3 : k3;

            auto dirty = dp.piece_no[1];

            for (i = 0; i < dirty; ++i)
            {
                BIT_IF_ALL(k1, k3, m1, m3, r01, r03, r11, r13, b01, b03, b11, b13);
            }

            if (SelectBit & 0x3c)
            {
                for (++i; i < PIECE_NO_BISHOP; ++i)
                {
                    BIT_IF_ALL(k1, k3, m1, m3, r01, r03, r11, r13, b01, b03, b11, b13);
                }

                i--;
            }

            for (++i; i < PIECE_NO_KING; ++i)
            {
                BIT_IF_KPP(k1, k3, m1, m3);
            }
        }
        else if (DirtyNum == 3)
        {
            // この辺の処理がボトルネックになりませんように。
            const BonaPiece k0 = dp.pre_piece[0].fb;
            const BonaPiece k1 = dp.pre_piece[1].fb;
            const BonaPiece k2 = dp.now_piece[0].fb;
            const BonaPiece k3 = dp.now_piece[1].fb;
            const BonaPiece m0 = dp.pre_piece[0].fw;
            const BonaPiece m1 = dp.pre_piece[1].fw;
            const BonaPiece m2 = dp.now_piece[0].fw;
            const BonaPiece m3 = dp.now_piece[1].fw;

            const BonaPiece r00 = is_white[0] ? m0 : k0;
            const BonaPiece r01 = is_white[0] ? m1 : k1;
            const BonaPiece r02 = is_white[0] ? m2 : k2;
            const BonaPiece r03 = is_white[0] ? m3 : k3;

            const BonaPiece r10 = is_white[1] ? m0 : k0;
            const BonaPiece r11 = is_white[1] ? m1 : k1;
            const BonaPiece r12 = is_white[1] ? m2 : k2;
            const BonaPiece r13 = is_white[1] ? m3 : k3;

            const BonaPiece b00 = is_white[2] ? m0 : k0;
            const BonaPiece b01 = is_white[2] ? m1 : k1;
            const BonaPiece b02 = is_white[2] ? m2 : k2;
            const BonaPiece b03 = is_white[2] ? m3 : k3;

            const BonaPiece b10 = is_white[3] ? m0 : k0;
            const BonaPiece b11 = is_white[3] ? m1 : k1;
            const BonaPiece b12 = is_white[3] ? m2 : k2;
            const BonaPiece b13 = is_white[3] ? m3 : k3;

            auto dirty1 = dp.piece_no[0], dirty2 = dp.piece_no[1];

            if (dirty1 > dirty2)
                std::swap(dirty1, dirty2);

            for (i = 0; i < dirty1; ++i)
            {
                BIT_IF_ALL(k0, k2, m0, m2, r00, r02, r10, r12, b00, b02, b10, b12);
                BIT_IF_ALL(k1, k3, m1, m3, r01, r03, r11, r13, b01, b03, b11, b13);
            }
            for (++i; i < dirty2; ++i)
            {
                BIT_IF_ALL(k0, k2, m0, m2, r00, r02, r10, r12, b00, b02, b10, b12);
                BIT_IF_ALL(k1, k3, m1, m3, r01, r03, r11, r13, b01, b03, b11, b13);
            }

            if (SelectBit & 0x3c)
            {
                for (++i; i < PIECE_NO_BISHOP; ++i)
                {
                    BIT_IF_ALL(k0, k2, m0, m2, r00, r02, r10, r12, b00, b02, b10, b12);
                    BIT_IF_ALL(k1, k3, m1, m3, r01, r03, r11, r13, b01, b03, b11, b13);
                }
                i--;
            }

            for (++i; i < PIECE_NO_KING; ++i)
            {
                BIT_IF_KPP(k0, k2, m0, m2);
                BIT_IF_KPP(k1, k3, m1, m3);
            }

            // 移動した駒が二つある場合は移動前の2駒と移動後の2駒について計算をする。
            if (SelectBit & 1)
            {
                sum.sumKPP[BLACK] -= kpp[sq[0]][k0][k1];
                sum.sumKPP[BLACK] += kpp[sq[0]][k2][k3];
            }

            if (SelectBit & 2)
            {
                sum.sumKPP[WHITE] += kpp[sq[1]][m0][m1];
                sum.sumKPP[WHITE] -= kpp[sq[1]][m2][m3];
            }

            if (SelectBit & 4)
            {
                sum.sumRPP[BLACK] -= rpp[sq[2]][r00][r01];
                sum.sumRPP[BLACK] += rpp[sq[2]][r02][r03];
            }

            if (SelectBit & 8)
            {
                sum.sumRPP[WHITE] -= rpp[sq[3]][r10][r11];
                sum.sumRPP[WHITE] += rpp[sq[3]][r12][r13];
            }

            if (SelectBit & 16)
            {
                sum.sumBPP[BLACK] -= bpp[sq[4]][b00][b01];
                sum.sumBPP[BLACK] += bpp[sq[4]][b02][b03];
            }

            if (SelectBit & 32)
            {
                sum.sumBPP[WHITE] -= bpp[sq[5]][b10][b11];
                sum.sumBPP[WHITE] += bpp[sq[5]][b12][b13];
            }
        }

        // 符号が逆転しているものは元に戻しておく
        if (SelectBit & 0x3c)
        {
            if (SelectBit & 4)
                if (is_white[0])
                    sum.sumRPP[0] = -sum.sumRPP[0];

            if (SelectBit & 8)
                if (is_white[1])
                    sum.sumRPP[1] = -sum.sumRPP[1];

            if (SelectBit & 16)
                if (is_white[2])
                    sum.sumBPP[0] = -sum.sumBPP[0];

            if (SelectBit & 32)
                if (is_white[3])
                    sum.sumBPP[1] = -sum.sumBPP[1];
        }
    }

    // 玉を全計算
    template <Turn T>
    void calcKing(EvalSum& sum, Square sq, const BonaPiece* list)
    {
        sum.sumKPP[T] = 0;

        if (T == BLACK)
        {
            for (int i = 0; i < PIECE_NO_KING; i++)
                for (int j = 0; j < i; j++)
                    sum.sumKPP[T] += kpp[sq][list[i]][list[j]];
        }
        else
        {
            for (int i = 0; i < PIECE_NO_KING; i++)
                for (int j = 0; j < i; j++)
                    sum.sumKPP[T] -= kpp[sq][list[i]][list[j]];
        }
    }

    // 飛車角の全計算
    template <PieceType PT, int N>
    void calcAll(EvalSum& sum, Square sq, const BonaPiece* list, bool is_white)
    {
        static_assert(PT == ROOK || PT == BISHOP, "");

        auto &tpp = PT == ROOK ? sum.sumRPP[N] : sum.sumBPP[N];
        const auto eval = PT == ROOK ? rpp : bpp;

        tpp = 0;

        if (is_white)
        {
            for (int i = 0; i < PIECE_NO_BISHOP; i++)
                for (int j = 0; j < i; j++)
                    tpp -= eval[sq][list[i]][list[j]];
        }
        else
        {
            for (int i = 0; i < PIECE_NO_BISHOP; i++)
                for (int j = 0; j < i; j++)
                    tpp += eval[sq][list[i]][list[j]];
        }
    }

    // 差分計算部
    Score calcEvalDiff(const Board& b)
    {
        auto now = b.state();
        EvalSum sum;

        // すでに計算済み
        if (!now->sum.isNotEvaluated())
        {
            sum = now->sum;
            goto CALC_DIFF_END;
        }

        auto prev = now->previous;

        // 前回局面が計算済みでもないなら、全計算
        if (prev->sum.isNotEvaluated())
        {
            computeEval(b);
            sum = now->sum;
            goto CALC_DIFF_END;
        }

        // 前回の計算結果から差分計算する
        {
            sum = prev->sum;

            auto list_fb = b.evalList()->pieceListFb();
            auto list_fw = b.evalList()->pieceListFw();

            // 玉の位置と飛車角の位置
            Square sq[6] = 
            {
                b.kingSquare(BLACK), inverse(b.kingSquare(WHITE)),
                b.rookSquare(0), b.rookSquare(1),
                b.bishopSquare(0), b.bishopSquare(1)
            };	
            
            const bool is_white[4] = 
            { 
                sq[2] >= SQ_MAX_PRO_HAND,
                sq[3] >= SQ_MAX_PRO_HAND,
                sq[4] >= SQ_MAX_PRO_HAND,
                sq[5] >= SQ_MAX_PRO_HAND 
            };

            const BonaPiece* list[6] = { list_fb, list_fw, };

            // squareは既にinverseされているのでinverseは必要ない。
            for (int i = 0; i < 4; i++)
            {
                if (is_white[i])
                {
                    sq[i + 2] -= SQ_MAX_PRO_HAND;
                    list[i + 2] = list_fw;
                }
                else
                    list[i + 2] = list_fb;
            }

            // 移動させた駒は最大2つある。その数
            auto& dp = now->dirty_piece;
            int k = dp.dirty_num;
            auto dirty = dp.piece_no[0];

// 書き方が決まっているので間違えないようにdefineする。
#define CALC_BKING calcKing<BLACK>(sum, sq[0], list_fb);
#define CALC_WKING calcKing<WHITE>(sum, sq[1], list_fw);
#define CALC_ROOK0 calcAll<ROOK, 0>(sum, sq[2], list[2], is_white[0]);
#define CALC_ROOK1 calcAll<ROOK, 1>(sum, sq[3], list[3], is_white[1]);
#define CALC_BISHOP0 calcAll<BISHOP, 0>(sum, sq[4], list[4], is_white[2]);
#define CALC_BISHOP1 calcAll<BISHOP, 1>(sum, sq[5], list[5], is_white[3]);

            if (k == 1) // 動いた駒が1枚
            {
                switch (dirty)
                {
                case PIECE_NO_BKING:
                    CALC_BKING;
                    break;

                case PIECE_NO_WKING:
                    CALC_WKING;
                    break;

                case PIECE_NO_ROOK: // 1枚目の飛車
                    CALC_ROOK0;
                    calcDiff<1, 3>(sum, sq, list, is_white, dp);
                    break;

                case PIECE_NO_ROOK + 1:
                    CALC_ROOK1;
                    calcDiff<1, 3>(sum, sq, list, is_white, dp);
                    break;

                case PIECE_NO_BISHOP:
                    CALC_BISHOP0;
                    calcDiff<1, 3>(sum, sq, list, is_white, dp);
                    break;

                case PIECE_NO_BISHOP + 1:
                    CALC_BISHOP1;
                    calcDiff<1, 3>(sum, sq, list, is_white, dp);
                    break;

                default: // 差分計算のみ
                    calcDiff<1, 0x3f>(sum, sq, list, is_white, dp);
                    break;
                }
            }
            else // 動いた駒が2枚。この場合が大変。
            {
                auto dirty2 = dp.piece_no[1];

                switch (dirty)
                {
                case PIECE_NO_BKING: // 1枚目が先手玉
                    CALC_BKING;

                    // もう一枚の駒で気合を入れてswitch。
                    switch (dirty2)
                    {
                        // もう一枚の駒がKINGはありえない。玉がとられることはないので...
                    case PIECE_NO_BKING: case PIECE_NO_WKING: assert(false); break;
                    case PIECE_NO_ROOK:
                        CALC_ROOK0;
                        calcKingDiff<WHITE, 2>(sum, sq[1], list_fw, dp);
                        break;

                    case PIECE_NO_ROOK + 1:
                        CALC_ROOK1;
                        calcKingDiff<WHITE, 2>(sum, sq[1], list_fw, dp);
                        break;

                    case PIECE_NO_BISHOP:
                        CALC_BISHOP0;
                        calcKingDiff<WHITE, 2>(sum, sq[1], list_fw, dp);
                        break;

                    case PIECE_NO_BISHOP + 1:
                        CALC_BISHOP1;
                        calcKingDiff<WHITE, 2>(sum, sq[1], list_fw, dp);
                        break;

                    default: // 先手玉を除いて差分計算
                        calcDiff<2, 0x3e>(sum, sq, list, is_white, dp);
                        break;
                    }

                    break;

                case PIECE_NO_WKING: // 一枚目が後手玉
                    CALC_WKING;

                    switch (dirty2)
                    {
                    case PIECE_NO_BKING: case PIECE_NO_WKING: assert(false); break;
                    case PIECE_NO_ROOK:
                        CALC_ROOK0;
                        calcKingDiff<BLACK, 2>(sum, sq[0], list_fb, dp);
                        break;

                    case PIECE_NO_ROOK + 1:
                        CALC_ROOK1;
                        calcKingDiff<BLACK, 2>(sum, sq[0], list_fb, dp);
                        break;

                    case PIECE_NO_BISHOP:
                        CALC_BISHOP0;
                        calcKingDiff<BLACK, 2>(sum, sq[0], list_fb, dp);
                        break;

                    case PIECE_NO_BISHOP + 1:
                        CALC_BISHOP1;
                        calcKingDiff<BLACK, 2>(sum, sq[0], list_fb, dp);
                        break;

                    default: // 後手玉を除いて差分計算
                        calcDiff<2, 0x3d>(sum, sq, list, is_white, dp);
                        break;
                    }
                    break;

                case PIECE_NO_ROOK: // 1枚目の飛車
                    CALC_ROOK0;

                    switch (dirty2)
                    {

                    case PIECE_NO_BKING: case PIECE_NO_WKING: case PIECE_NO_ROOK: assert(false); break;
                    case PIECE_NO_ROOK + 1:
                        CALC_ROOK1;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_BISHOP:
                        CALC_BISHOP0;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_BISHOP + 1:
                        CALC_BISHOP1;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    default:

                        // 2枚動いた場合で一つは大駒の場合は、玉を先に計算しないといけない。
                        calcDiff<3, 0x03>(sum, sq, list, is_white, dp);

                        // 2枚目の小駒は玉は計算済みなので省く。一枚目の飛車も全計算を済ませているので省く。
                        calcDiff<2, 0x38>(sum, sq, list, is_white, dp);

                        break;
                    }

                    break;

                case PIECE_NO_ROOK + 1: // 一枚目は二枚目の飛車
                    CALC_ROOK1;

                    switch (dirty2)
                    {
                    case PIECE_NO_BKING: case PIECE_NO_WKING: case PIECE_NO_ROOK + 1: assert(false); break;
                    case PIECE_NO_ROOK:
                        CALC_ROOK0;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_BISHOP:
                        CALC_BISHOP0;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_BISHOP + 1:
                        CALC_BISHOP1;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    default:
                        calcDiff<3, 0x03>(sum, sq, list, is_white, dp);
                        calcDiff<2, 0x34>(sum, sq, list, is_white, dp);
                        break;
                    }

                    break;

                case PIECE_NO_BISHOP: // 一枚目は一枚目の角
                    CALC_BISHOP0;

                    switch (dirty2)
                    {
                    case PIECE_NO_BKING: case PIECE_NO_WKING: case PIECE_NO_BISHOP: assert(false); break;
                    case PIECE_NO_ROOK:
                        CALC_ROOK0;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_ROOK + 1:
                        CALC_ROOK1;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_BISHOP + 1:
                        CALC_BISHOP1;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    default:
                        calcDiff<3, 0x03>(sum, sq, list, is_white, dp);
                        calcDiff<2, 0x2c>(sum, sq, list, is_white, dp);
                        break;
                    }

                    break;

                case PIECE_NO_BISHOP + 1: // 一枚目は2枚目の角
                    CALC_BISHOP1;

                    switch (dirty2)
                    {
                    case PIECE_NO_BKING: case PIECE_NO_WKING: case PIECE_NO_BISHOP + 1: assert(false); break;
                    case PIECE_NO_ROOK:
                        CALC_ROOK0;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_ROOK + 1:
                        CALC_ROOK1;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_BISHOP:
                        CALC_BISHOP0;
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    default:
                        calcDiff<3, 0x03>(sum, sq, list, is_white, dp);
                        calcDiff<2, 0x1c>(sum, sq, list, is_white, dp);
                        break;
                    }

                    break;

                default: // 一枚目は角以下の駒

                    switch (dirty2)
                    {
                    case PIECE_NO_BKING: case PIECE_NO_WKING: assert(false); break;

                    case PIECE_NO_ROOK:
                        CALC_ROOK0;
                        // 角以下の駒はすべて差分計算可能
                        calcDiff<1, 0x38>(sum, sq, list, is_white, dp);
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_ROOK + 1:
                        CALC_ROOK1;
                        calcDiff<1, 0x34>(sum, sq, list, is_white, dp);
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_BISHOP:
                        CALC_BISHOP0;
                        calcDiff<1, 0x2c>(sum, sq, list, is_white, dp);
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    case PIECE_NO_BISHOP + 1:
                        CALC_BISHOP1;
                        calcDiff<1, 0x1c>(sum, sq, list, is_white, dp);
                        calcDiff<3, 3>(sum, sq, list, is_white, dp);
                        break;

                    default:
                        calcDiff<3, 0x3f>(sum, sq, list, is_white, dp);
                        break;
                    }

                    break;
                }
            }
        }

        // 保存大事。
        b.state()->sum = sum;

        // 差分計算終わり
    CALC_DIFF_END:
        return (Score)(sum.calcScore() / FV_SCALE);
    }

    // 駒割り以外の全計算
    Score computeEvalKpp(const Board& b)
    {
        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk1 = inverse(b.kingSquare(WHITE));
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        EvalSum sum;
        sum.clear();
        BonaPiece k0, k1, k2, k3;
        for (int i = 0; i < PIECE_NO_KING; i++)
        {
            k0 = list_fb[i];
            k1 = list_fw[i];

            for (int j = 0; j < i; j++)
            {
                k2 = list_fb[j];
                k3 = list_fw[j];

                sum.sumKPP[BLACK] += kpp[sq_bk0][k0][k2];
                sum.sumKPP[WHITE] -= kpp[sq_wk1][k1][k3];
            }
        }

        b.state()->sum = sum;

        return Score(sum.calcScore() / FV_SCALE);
    }

    // 評価関数
    Score evaluate(const Board& b)
    {
        auto score = calcEvalDiff(b);

#if 0
        // 非差分計算
        auto sscore = computeEvalKpp(b);

        if (score != sscore)
        {
            std::cout << b << b.key();
        }
        assert(score == sscore);
#endif
        score += b.state()->material;

        return b.turn() == BLACK ? score : -score;
    }
}
#endif // EVAL_KPPT