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

#include "evaluate.h"

#ifdef EVAL_KPP

#include <fstream>
#include <iostream>
#include "board.h"
#include "usi.h"

using namespace std;
extern uint64_t g[30];

#if defined ENABLE_COUNT
#define COUNT(n) g[n]++
#else
#define COUNT(n) 
#endif

namespace Eval
{
    // KKPファイル名
#define KKP_BIN "kkp32ap.bin"

    // KPPファイル名
#define KPP_BIN "kpp16ap.bin"

    typedef int16_t ValueKpp;
    typedef int32_t ValueKkp;

    // KPP
    ValueKpp kpp[SQ_MAX_PLUS1][fe_end][fe_end];

    // KKP
    // fe_endまでにしてしまう（本当はもっと小さくてすむの意味）。これによりPiece番号がKPPとKKPとで共通になる。
    // さらに、2パラ目のKはinverse(K2)を渡すものとすればkppと同じinverse(K2)で済む。
    // [][][fe_end]のところはKK定数にしてあるものとする。
    ValueKkp kkp[SQ_MAX_PLUS1][SQ_MAX_PLUS1][fe_end + 1];

    const size_t size_kkp = SQ_MAX_PLUS1 * (int)SQ_MAX_PLUS1 * ((int)(fe_end) + 1);
    const size_t size_kpp = SQ_MAX_PLUS1 * (int)fe_end * (int)fe_end;

    // 評価関数ファイルを読み込む
    // ほぼやねうら王2015のkpp、kkpバイナリの読み込み専用である。
    void load()
    {
        ifstream ifs_kkp(path((std::string)USI::Options["EvalDir"], KKP_BIN), ios::in | ios::binary);
        ifstream ifs_kpp(path((std::string)USI::Options["EvalDir"], KPP_BIN), ios::in | ios::binary);
        
        if (ifs_kpp.fail() || ifs_kkp.fail())
            goto Error;

        ifs_kkp.read((char*)&kkp, sizeof(kkp));
        ifs_kpp.read((char*)&kpp, sizeof(kpp));
        
#ifdef USE_FILE_SQUARE_EVAL
        {
            ValueKkp* kkp2 = new ValueKkp[size_kkp];
            ValueKpp* kpp2 = new ValueKpp[size_kpp];

#define KKP2(k1, k2, p) kkp2[k1 * (int)SQ_MAX_PLUS1 * (int)(fe_end + 1) + k2 * (int)(fe_end + 1) + p]
#define KPP2(k, p1, p2) kpp2[k * (int)fe_end * (int)fe_end + p1 * (int)fe_end + p2]

            memset(kkp2, 0, sizeof(kkp));
            memset(kpp2, 0, sizeof(kpp));

            // なんかバイナリを作るときにミスってたらしく変換しないといけないらしい。
            for (int k1 = 0; k1 < SQ_MAX_PLUS1; ++k1)
                for (int k2 = 0; k2 < SQ_MAX_PLUS1; ++k2)
                    for (int j = 1; j < fe_end + 1; ++j)
                    {
                        int j2 = j < fe_hand_end ? j - 1 : j;
                        KKP2(k1, k2, j) = kkp[k1][k2][j2];
                    }

            for (int k = 0; k < SQ_MAX_PLUS1; ++k)
                for (int i = 1; i < fe_end; ++i)
                    for (int j = 1; j < fe_end; ++j)
                    {
                        int i2 = i < fe_hand_end ? i - 1 : i;
                        int j2 = j < fe_hand_end ? j - 1 : j;
                        KPP2(k, i, j) = kpp[k][i2][j2];
                    }

            memcpy(kkp, kkp2, sizeof(kkp));
            memcpy(kpp, kpp2, sizeof(kpp));

            delete[] kkp2;
            delete[] kpp2;
        }
#endif
        return;

    Error:;
        std::cout << "\ninfo string open evaluation file failed.\n";
    }

    // KKPのスケール
    const int FV_SCALE_KKP = 512;

    // KPP,KPのスケール
    const int FV_SCALE = 32;

    // 駒割り以外の全計算
    Score computeEval(const Board& b)
    {
        auto sq_bk0 = toEvalSq(b.kingSquare(BLACK));
        auto sq_wk1 = toEvalSq(inverse(b.kingSquare(WHITE)));
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        int i, j;
        BonaPiece k0, k1;

        int32_t sumKKP = kkp[sq_bk0][sq_wk1][fe_end];
        int32_t sumBKPP = 0;
        int32_t sumWKPP = 0;

        for (i = 0; i < PIECE_NO_KING; i++)
        {
            k0 = list_fb[i];
            k1 = list_fw[i];
            sumKKP += kkp[sq_bk0][sq_wk1][k0];

            for (j = 0; j < i; j++)
            {
                sumBKPP += kpp[sq_bk0][k0][list_fb[j]];
                sumWKPP -= kpp[sq_wk1][k1][list_fw[j]];
            }
        }

        auto& info = *b.state();
        info.sum.sumKKP = Score(sumKKP);
        info.sum.sumBKPP = Score(sumBKPP);
        info.sum.sumWKPP = Score(sumWKPP);

        return Score((sumBKPP + sumWKPP + sumKKP / FV_SCALE_KKP) / FV_SCALE);
    }

    Score calcEvalDiff(const Board& b)
    {
        auto st = b.state();
        int sumKKP, sumBKPP, sumWKPP;

        if (!st->sum.isNotEvaluated())
        {
            sumKKP = st->sum.sumKKP;
            sumBKPP = st->sum.sumBKPP;
            sumWKPP = st->sum.sumWKPP;
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
            sumKKP = now->sum.sumKKP;
            sumBKPP = now->sum.sumBKPP;
            sumWKPP = now->sum.sumWKPP;
            goto CALC_DIFF_END;
        }

        // この差分を求める
        {
            sumKKP = prev->sum.sumKKP;
            sumBKPP = prev->sum.sumBKPP;
            sumWKPP = prev->sum.sumWKPP;
            int k0, k1, k2, k3;

            auto sq_bk0 = toEvalSq(b.kingSquare(BLACK));
            auto sq_wk1 = toEvalSq(inverse(b.kingSquare(WHITE)));

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
                    // ----------------------------
                    // 先手玉が移動したときの計算
                    // ----------------------------

                    // 現在の玉の位置に移動させて計算する。
                    // 先手玉に関するKKP,KPPは全計算なので一つ前の値は関係ない。

                    sumBKPP = 0;

                    // このときKKPは差分で済まない。
                    sumKKP = Eval::kkp[sq_bk0][sq_wk1][fe_end];
 
                    // 片側まるごと計算
                    for (i = 0; i < PIECE_NO_KING; i++)
                    {
                        k0 = now_list_fb[i];
                        sumKKP += Eval::kkp[sq_bk0][sq_wk1][k0];

                        for (j = 0; j < i; j++)
                            sumBKPP += Eval::kpp[sq_bk0][k0][now_list_fb[j]];
                    }

                    // もうひとつの駒がないならこれで計算終わりなのだが。
                    if (k == 2)
                    {
                        // この駒についての差分計算をしないといけない。
                        k1 = dp.pre_piece[1].fw;
                        k3 = dp.now_piece[1].fw;

                        dirty = dp.piece_no[1];

                        // BKPPはすでに計算済みなのでWKPPのみ。
                        // WKは移動していないのでこれは前のままでいい。
                        for (i = 0; i < dirty; ++i)
                        {
                            sumWKPP += Eval::kpp[sq_wk1][k1][now_list_fw[i]];
                            sumWKPP -= Eval::kpp[sq_wk1][k3][now_list_fw[i]];
                        }
                        for (++i; i < PIECE_NO_KING; ++i)
                        {
                            sumWKPP += Eval::kpp[sq_wk1][k1][now_list_fw[i]];
                            sumWKPP -= Eval::kpp[sq_wk1][k3][now_list_fw[i]];
                        }
                    }

                }
                else {
                    // ----------------------------
                    // 後手玉が移動したときの計算
                    // ----------------------------
                    assert(dirty == PIECE_NO_WKING);

                    sumWKPP = 0;
                    sumKKP = Eval::kkp[sq_bk0][sq_wk1][fe_end];


                    for (i = 0; i < PIECE_NO_KING; i++)
                    {
                        k0 = now_list_fb[i]; // これ、KKPテーブルにk1側も入れておいて欲しい気はするが..
                        k1 = now_list_fw[i];
                        sumKKP += Eval::kkp[sq_bk0][sq_wk1][k0];

                        for (j = 0; j < i; j++)
                            sumWKPP -= Eval::kpp[sq_wk1][k1][now_list_fw[j]];
                    }

                    if (k == 2)
                    {
                        k0 = dp.pre_piece[1].fb;
                        k2 = dp.now_piece[1].fb;

                        dirty = dp.piece_no[1];

                        for (i = 0; i < dirty; ++i)
                        {
                            sumBKPP -= Eval::kpp[sq_bk0][k0][now_list_fb[i]];
                            sumBKPP += Eval::kpp[sq_bk0][k2][now_list_fb[i]];
                        }
                        for (++i; i < PIECE_NO_KING; ++i)
                        {
                            sumBKPP -= Eval::kpp[sq_bk0][k0][now_list_fb[i]];
                            sumBKPP += Eval::kpp[sq_bk0][k2][now_list_fb[i]];
                        }
                    }
                }

            }
            else {
                // ----------------------------
                // 玉以外が移動したときの計算
                // ----------------------------

#define ADD_BWKPP(W0,W1,W2,W3) { \
          sumBKPP -= Eval::kpp[sq_bk0][W0][now_list_fb[i]]; \
          sumWKPP += Eval::kpp[sq_wk1][W1][now_list_fw[i]]; \
          sumBKPP += Eval::kpp[sq_bk0][W2][now_list_fb[i]]; \
          sumWKPP -= Eval::kpp[sq_wk1][W3][now_list_fw[i]]; \
}
                if (k == 1)
                {
                    // 移動した駒が一つ。

                    k0 = dp.pre_piece[0].fb;
                    k1 = dp.pre_piece[0].fw;
                    k2 = dp.now_piece[0].fb;
                    k3 = dp.now_piece[0].fw;

                    // KKP差分
                    sumKKP -= Eval::kkp[sq_bk0][sq_wk1][k0];
                    sumKKP += Eval::kkp[sq_bk0][sq_wk1][k2];

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
                    if (dirty > dirty2) swap(dirty, dirty2);
                    // PIECE_NO_ZERO <= dirty < dirty2 < PIECE_NO_KING
                    // にしておく。

                    k0 = dp.pre_piece[0].fb;
                    k1 = dp.pre_piece[0].fw;
                    k2 = dp.now_piece[0].fb;
                    k3 = dp.now_piece[0].fw;

                    int m0, m1, m2, m3;
                    m0 = dp.pre_piece[1].fb;
                    m1 = dp.pre_piece[1].fw;
                    m2 = dp.now_piece[1].fb;
                    m3 = dp.now_piece[1].fw;

                    // KKP差分
                    sumKKP -= Eval::kkp[sq_bk0][sq_wk1][k0];
                    sumKKP += Eval::kkp[sq_bk0][sq_wk1][k2];
                    sumKKP -= Eval::kkp[sq_bk0][sq_wk1][m0];
                    sumKKP += Eval::kkp[sq_bk0][sq_wk1][m2];

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

                    sumBKPP -= Eval::kpp[sq_bk0][k0][m0];
                    sumWKPP += Eval::kpp[sq_wk1][k1][m1];
                    sumBKPP += Eval::kpp[sq_bk0][k2][m2];
                    sumWKPP -= Eval::kpp[sq_wk1][k3][m3];

                }
            }
        }

        now->sum.sumKKP = sumKKP;
        now->sum.sumBKPP = sumBKPP;
        now->sum.sumWKPP = sumWKPP;

        // 差分計算終わり
    CALC_DIFF_END:
        return (Score)((sumBKPP + sumWKPP + sumKKP / FV_SCALE_KKP) / FV_SCALE);
    }

    // 評価関数
    Score evaluate(const Board& b)
    {
        // 差分計算
        auto score = calcEvalDiff(b) + b.state()->material;

        // 非差分計算
        // auto sscore = computeEval(b) + b.state()->material;

        // 差分計算と非差分計算との計算結果が合致するかのテスト。(さすがに重いのでコメントアウトしておく)
        assert(score == computeEval(b) + b.state()->material);

        return b.turn() == BLACK ? score : -score;
    }

#ifdef USE_FILE_SQUARE_EVAL
    // 縦型Square型のevalファイルを横型Square型のevalファイルに変換する。
    void convertEval()
    {
        USI::isReady();

        BonaPiece y2b[fe_end + 1];

        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end + 1; p++)
        {
            if (p < fe_hand_end)
            {
                y2b[p] = p;
            }
            else if (p == fe_end)
            {
                y2b[p] = fe_end;
            }
            else
            {
                auto wp = p - fe_hand_end;
                Square sq = Square(wp % 81);
                EvalSquare rsq = toEvalSq(sq);
                auto np = p - sq + rsq;
                y2b[p] = np;
            }
        }

        auto wkpp = new ValueKpp[size_kpp];
        auto wkkp = new ValueKkp[size_kkp];

        // 元の重みをコピー

#define WKKP(k1, k2, p) wkkp[k1 * (int)SQ_MAX_PLUS1 * (int)(fe_end + 1) + k2 * (int)(fe_end + 1) + p]
#define WKPP(k, p1, p2) wkpp[k * (int)fe_end * (int)fe_end + p1 * (int)fe_end + p2]
    
        for (Square k1 : Squares)
            for (Square k2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end + 1; ++p)
                {
                    WKKP(k1, k2, p) = kkp[toEvalSq(k1)][toEvalSq(k2)][y2b[p]];
                }

        for (Square k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                {
                    WKPP(k, p1, p2) = kpp[toEvalSq(k)][y2b[p1]][y2b[p2]];
                }

        const std::string eval_dir = "new_eval";
        mkdir(eval_dir);

        // KKP
        std::ofstream ofsKKP(path(eval_dir, KKP_BIN), std::ios::binary);
        // KPP
        std::ofstream ofsKPP(path(eval_dir, KPP_BIN), std::ios::binary);

        if (!ofsKKP.write(reinterpret_cast<char*>(wkkp), sizeof(kkp)))
            goto Error;

        if (!ofsKPP.write(reinterpret_cast<char*>(wkpp), sizeof(kpp)))
            goto Error;

        std::cout << "finished. folder = " << eval_dir << std::endl;

        delete[] wkpp;
        delete[] wkkp;

        return;
    Error:;
        std::cout << "Error : save_eval() failed" << std::endl;
    }
#endif // USE_FILE_SQUARE_EVAL
}
#endif