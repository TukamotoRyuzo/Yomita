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

#include "eval_kppl.h"

// kppだけ。
#ifdef EVAL_KPPL

#include <fstream>
#include "usi.h"
#include "board.h"

#define kpp (*(et.kpp_))

namespace Eval
{
    EvalTable et;

    // 評価関数ファイルを読み込む
    void loadSub()
    {
        std::ifstream ifs_kpp(path((std::string)USI::Options["EvalDir"], KPP_BIN), std::ios::in | std::ios::binary);

        if (ifs_kpp.fail())
            goto Error;

        const size_t size = (size_t)SQ_MAX * (size_t)fe_end * (size_t)fe_end;
        int16_t* tmp = new int16_t[size];
        ifs_kpp.read(reinterpret_cast<char*>(tmp), sizeof(kpp));

        for (auto sq : Squares)
            for (int i = 0; i < fe_end; i++)
                for (int j = 0; j < fe_end; j++)
                    kpp[sq][i][j] = tmp[(size_t)sq * (size_t)fe_end * (size_t)fe_end + i * (size_t)fe_end + j];

        delete[] tmp;

#ifdef LEARN
        evalLearnInit();
#endif
            return;
        
    Error:;
        std::cout << "\ninfo string open evaluation file failed.\n";
    }

    // KPP,KPのスケール
    const int FV_SCALE = 32;

    // 駒割り以外の全計算
    Score computeEval(const Board& b)
    {
        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk1 = inverse(b.kingSquare(WHITE));
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        EvalSum sum;

        sum.sumBKPP = sum.sumWKPP = 0;

        BonaPiece k0, k1, k2, k3;
        for (int i = 0; i < PIECE_NO_KING; i++)
        {
            k0 = list_fb[i];
            k1 = list_fw[i];

            for (int j = 0; j < i; j++)
            {
                k2 = list_fb[j];
                k3 = list_fw[j];

                sum.sumBKPP += kpp[sq_bk0][k0][k2];
                sum.sumWKPP -= kpp[sq_wk1][k1][k3];
            }
        }

        b.state()->sum = sum;

        return Score(sum.calcScore() / FV_SCALE);
    }

    Score calcEvalDiff(const Board& b)
    {
        auto st = b.state();
        EvalSum sum;

        if (!st->sum.isNotEvaluated())
        {
            sum = st->sum;
            goto CALC_DIFF_END;
        }

        // 一つだけ遡る
        // ひとつずつ遡りながらsumKPPがSCORE_NONEでないところまで探してそこからの差分を計算することは出来るが
        // レアケースだし、StateInfoのEvalListを持たせる必要が出てきて、あまり得しない。
        auto now = st;
        auto prev = st->previous;

        if (prev->sum.isNotEvaluated())
        {
            // 全計算
            computeEval(b);
            sum = now->sum;
            goto CALC_DIFF_END;
        }

        // この差分を求める
        {
            sum = prev->sum;

            auto sq_bk0 = b.kingSquare(BLACK);
            auto sq_wk1 = inverse(b.kingSquare(WHITE));
            auto now_list_fb = b.evalList()->pieceListFb();
            auto now_list_fw = b.evalList()->pieceListFw();

            int i, j;
            auto& dp = now->dirty_piece;

            // 移動させた駒は最大2つある。その数
            int k = dp.dirty_num;

            auto dirty = dp.piece_no[0];

            if (dirty >= PIECE_NO_KING) // 王と王でないかで場合分け
            {
                if (dirty == PIECE_NO_BKING)
                {
                    sum.sumBKPP = 0;


                    // 片側まるごと計算
                    for (i = 0; i < PIECE_NO_KING; i++)
                    {
                        auto k0 = now_list_fb[i];

                        for (j = 0; j < i; j++)
                        {
                            auto k1 = now_list_fb[j];

                            sum.sumBKPP += kpp[sq_bk0][k0][k1];
                        }
                    }

                    // もうひとつの駒がないならこれで計算終わりなのだが。
                    if (k == 2)
                    {
                        // この駒についての差分計算をしないといけない。
                        auto k1 = dp.pre_piece[1].fw;
                        auto k3 = dp.now_piece[1].fw;

                        dirty = dp.piece_no[1];

                        // BKPPはすでに計算済みなのでWKPPのみ。
                        // WKは移動していないのでこれは前のままでいい。
                        for (i = 0; i < dirty; ++i)
                        {
                            auto k2 = now_list_fw[i];
                            sum.sumWKPP += kpp[sq_wk1][k1][k2];
                            sum.sumWKPP -= kpp[sq_wk1][k3][k2];
                        }
                        for (++i; i < PIECE_NO_KING; ++i)
                        {
                            auto k2 = now_list_fw[i];
                            sum.sumWKPP += kpp[sq_wk1][k1][k2];
                            sum.sumWKPP -= kpp[sq_wk1][k3][k2];
                        }
                    }
                }
                else 
                {
                    assert(dirty == PIECE_NO_WKING);

                    sum.sumWKPP = 0;

                    for (i = 0; i < PIECE_NO_KING; i++)
                    {
                        auto k1 = now_list_fw[i];
    
                        for (j = 0; j < i; j++)
                        {
                            auto k2 = now_list_fw[j];
                            sum.sumWKPP -= kpp[sq_wk1][k1][k2];
                        }
                    }

                    if (k == 2)
                    {
                        auto k0 = dp.pre_piece[1].fb;
                        auto k2 = dp.now_piece[1].fb;

                        dirty = dp.piece_no[1];

                        for (i = 0; i < dirty; ++i)
                        {
                            auto k3 = now_list_fb[i];
                            sum.sumBKPP -= kpp[sq_bk0][k0][k3];
                            sum.sumBKPP += kpp[sq_bk0][k2][k3];
                        }
                        for (++i; i < PIECE_NO_KING; ++i)
                        {
                            auto k3 = now_list_fb[i];
                            sum.sumBKPP -= kpp[sq_bk0][k0][now_list_fb[i]];
                            sum.sumBKPP += kpp[sq_bk0][k2][now_list_fb[i]];
                        }
                    }
                }

            }
            else 
            {
                if (k == 1)
                {
                    // 移動した駒が一つ。

                    auto k0 = dp.pre_piece[0].fb;
                    auto k1 = dp.pre_piece[0].fw;
                    auto k2 = dp.now_piece[0].fb;
                    auto k3 = dp.now_piece[0].fw;

                    // KP値、要らんのでi==dirtyを除く
                    for (i = 0; i < dirty; ++i)
                        ADD_BWKPP(k0, k1, k2, k3);
                    for (++i; i < PIECE_NO_KING; ++i)
                        ADD_BWKPP(k0, k1, k2, k3);

                }
                else if (k == 2) 
                {

                    // 移動する駒が王以外の2つ。
                    PieceNo dirty2 = dp.piece_no[1];
                    if (dirty > dirty2) std::swap(dirty, dirty2);
                    // PIECE_NO_ZERO <= dirty < dirty2 < PIECE_NO_KING
                    // にしておく。

                    auto k0 = dp.pre_piece[0].fb;
                    auto k1 = dp.pre_piece[0].fw;
                    auto k2 = dp.now_piece[0].fb;
                    auto k3 = dp.now_piece[0].fw;
                    auto m0 = dp.pre_piece[1].fb;
                    auto m1 = dp.pre_piece[1].fw;
                    auto m2 = dp.now_piece[1].fb;
                    auto m3 = dp.now_piece[1].fw;

                    // KPP差分
                    for (i = 0; i < dirty; ++i)
                    {
                        ADD_BWKPP(k0, k1, k2, k3);
                        ADD_BWKPP(m0, m1, m2, m3);
                    }
                    for (++i; i < dirty2; ++i)
                    {
                        ADD_BWKPP(k0, k1, k2, k3);
                        ADD_BWKPP(m0, m1, m2, m3);
                    }
                    for (++i; i < PIECE_NO_KING; ++i)
                    {
                        ADD_BWKPP(k0, k1, k2, k3);
                        ADD_BWKPP(m0, m1, m2, m3);
                    }

                    sum.sumBKPP -= kpp[sq_bk0][k0][m0];
                    sum.sumWKPP += kpp[sq_wk1][k1][m1];
                    sum.sumBKPP += kpp[sq_bk0][k2][m2];
                    sum.sumWKPP -= kpp[sq_wk1][k3][m3];

                }
            }
        }

        now->sum = sum;
        // 差分計算終わり
    CALC_DIFF_END:
        return (Score)(sum.calcScore() / FV_SCALE);
    }

    // 評価関数
    Score evaluate(const Board& b)
    {
        // 差分計算
        auto score = calcEvalDiff(b);

#if 0
        // 非差分計算
        auto sscore = computeEval(b);

        // 差分計算と非差分計算との計算結果が合致するかのテスト。(さすがに重いのでコメントアウトしておく)
        if (score != sscore)
            std::cout << b << std::endl;
        assert(score == sscore);
#endif
        score += b.state()->material;
        return b.turn() == BLACK ? score : -score;
    }
}
#endif