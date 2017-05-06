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

#include "eval_kpptp.h"

#ifdef EVAL_KPPTP

#include <fstream>
#include "usi.h"
#include "board.h"

#define kk (*et.kk_)
#define kpp (*et.kpp_)
#define kkp (*et.kkp_)

namespace Eval
{
    EvalTable et;

    typedef std::array<int32_t, 2> ValueKk;
    typedef std::array<int16_t, 2> ValueKpp;
    typedef std::array<int32_t, 2> ValueKkp;

    ValueKk(*kk_w_)[SQ_MAX][SQ_MAX];
    ValueKpp(*kpp_w_)[SQ_MAX][fe_end][fe_end];
    ValueKkp(*kkp_w_)[SQ_MAX][SQ_MAX][fe_end];

    // 評価関数ファイルを読み込む
    void loadSub()
    { 
        const auto sizekk  = uint64_t(SQ_MAX) * uint64_t(SQ_MAX);
        const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
        const auto sizekkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end);

#if 1
        //std::ifstream ifsKK(path((std::string)USI::Options["EvalDir"], KK_BIN), std::ios::binary);
        //std::ifstream ifsKKP(path((std::string)USI::Options["EvalDir"], KKP_BIN), std::ios::binary);
        //std::ifstream ifsKPP(path((std::string)USI::Options["EvalDir"], KPP_BIN), std::ios::binary);
        std::ifstream ifsKK(path("C:/7", KK_BIN), std::ios::binary);
        std::ifstream ifsKKP(path("C:/7", KKP_BIN), std::ios::binary);
        std::ifstream ifsKPP(path("C:/7", KPP_BIN), std::ios::binary);

        if (!ifsKK || !ifsKKP || !ifsKPP)
        {
            std::cout << "\ninfo string open evaluation file failed.\n";
            return;
        }

        ifsKK.read(reinterpret_cast<char*>(kk),   sizeof(Value) * sizekk);
        ifsKKP.read(reinterpret_cast<char*>(kkp), sizeof(Value) * sizekkp);
        ifsKPP.read(reinterpret_cast<char*>(kpp), sizeof(Value) * sizekpp);
#endif

#if 0
        PRNG rng(1000);

        for (auto sq : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; p1++)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; p2++)
                {
                    auto w = kpp[sq][p1][p2];
                    auto v = w;

                    // BKPP = scoreBoard + ScoreTurn + ScoreProgress
                    // WKPP = scoreBoard - ScoreTurn + ScoreProgress
                    w.p[0] = v.p[0] + v.p[1] + v.p[2];
                    w.p[1] = v.p[0] - v.p[1] + v.p[2];
                    w.p[2] = v.p[0] + v.p[1] + v.p[3];
                    w.p[3] = v.p[0] - v.p[1] + v.p[3];
                    w.p[4] = v.p[0] + v.p[1] + v.p[4];
                    w.p[5] = v.p[0] - v.p[1] + v.p[4];
                    w.p[6] = v.p[0] + v.p[1] + v.p[5];
                    w.p[7] = v.p[0] - v.p[1] + v.p[5];
                    w.p[8] = v.p[0] + v.p[1] + v.p[6];
                    w.p[9] = v.p[0] - v.p[1] + v.p[6];
                    w.p[10] = v.p[0] + v.p[1] + v.p[7];
                    w.p[11] = v.p[0] - v.p[1] + v.p[7];

                    kpp[sq][p1][p2] = w;
                    //for (int i = 0; i < 16; i++)
                        //kpp[sq][p1][p2].p[i] = kpp[sq][p2][p1].p[i] = rng.rand(1000) - 500;
                }

        for (auto s1 : Squares)
            for (auto s2 : Squares)
            {
                auto w = kk[s1][s2];
                auto v = w;

                // BKPP = scoreBoard + ScoreTurn + ScoreProgress
                // WKPP = scoreBoard - ScoreTurn + ScoreProgress
                w.p[0] = v.p[0] + v.p[1] + v.p[2];
                w.p[1] = v.p[0] - v.p[1] + v.p[2];
                w.p[2] = v.p[0] + v.p[1] + v.p[3];
                w.p[3] = v.p[0] - v.p[1] + v.p[3];
                w.p[4] = v.p[0] + v.p[1] + v.p[4];
                w.p[5] = v.p[0] - v.p[1] + v.p[4];
                w.p[6] = v.p[0] + v.p[1] + v.p[5];
                w.p[7] = v.p[0] - v.p[1] + v.p[5];
                w.p[8] = v.p[0] + v.p[1] + v.p[6];
                w.p[9] = v.p[0] - v.p[1] + v.p[6];
                w.p[10] = v.p[0] + v.p[1] + v.p[7];
                w.p[11] = v.p[0] - v.p[1] + v.p[7];
                kk[s1][s2] = w;
                //for (int i = 0; i < 16; i++)
                    //kk[s1][s2].p[i] = kk[s2][s1].p[i] = rng.rand(1000) - 500;

                for (auto p = BONA_PIECE_ZERO; p < fe_end; p++)
                {
                    w = kkp[s1][s2][p];
                    v = w;

                    w.p[0] = v.p[0] + v.p[1] + v.p[2];
                    w.p[1] = v.p[0] - v.p[1] + v.p[2];
                    w.p[2] = v.p[0] + v.p[1] + v.p[3];
                    w.p[3] = v.p[0] - v.p[1] + v.p[3];
                    w.p[4] = v.p[0] + v.p[1] + v.p[4];
                    w.p[5] = v.p[0] - v.p[1] + v.p[4];
                    w.p[6] = v.p[0] + v.p[1] + v.p[5];
                    w.p[7] = v.p[0] - v.p[1] + v.p[5];
                    w.p[8] = v.p[0] + v.p[1] + v.p[6];
                    w.p[9] = v.p[0] - v.p[1] + v.p[6];
                    w.p[10] = v.p[0] + v.p[1] + v.p[7];
                    w.p[11] = v.p[0] - v.p[1] + v.p[7];
                    kkp[s1][s2][p] = w;
                        //for (int i = 0; i < 16; i++)
                        //kkp[s1][s2][p].p[i] = rng.rand(1000) - 500;
                }

            }

#endif
#if 0
        std::cout << "\ninfo string open evaluation file failed.\n";

        std::ifstream ifskk(path("C:/SDT4", KK_BIN), std::ios::binary);
        std::ifstream ifskkp(path("C:/SDT4", KKP_BIN), std::ios::binary);
        std::ifstream ifskpp(path("C:/SDT4", KPP_BIN), std::ios::binary);

        if (!ifskk || !ifskkp || !ifskpp)
        {
            std::cout << "\ninfo string open evaluation file failed.\n";
            return;
        }

        kk_w_ = (ValueKk(*)[SQ_MAX][SQ_MAX])        new ValueKk[sizekk];
        kpp_w_ = (ValueKpp(*)[SQ_MAX][fe_end][fe_end])new ValueKpp[sizekpp];
        kkp_w_ = (ValueKkp(*)[SQ_MAX][SQ_MAX][fe_end])new ValueKkp[sizekkp];

        ifskk.read(reinterpret_cast<char*>(kk_w_), sizeof(ValueKk)*sizekk);
        ifskkp.read(reinterpret_cast<char*>(kkp_w_), sizeof(ValueKkp)*sizekkp);
        ifskpp.read(reinterpret_cast<char*>(kpp_w_), sizeof(ValueKpp)*sizekpp);

        for (auto s1 : Squares)
            for (auto s2 : Squares)
            {
                kk[s1][s2].m = _mm256_setzero_si256();

                for (int i = 0; i < 1; i+=2)
                {
                    kk[s1][s2].p[i] = (int16_t)(*kk_w_)[s1][s2][0];
                    kk[s1][s2].p[i + 1] = (int16_t)(*kk_w_)[s1][s2][1];
                    kk[s1][s2].p[i + 2] = (int16_t)(*kk_w_)[s1][s2][1];
                }
            }

        for (auto sq : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; p1++)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; p2++)
                {
                    kpp[sq][p1][p2].m = _mm256_setzero_si256();

                    for (int i = 0; i < 1; i+=2)
                    {
                        kpp[sq][p1][p2].p[i] = (*kpp_w_)[sq][p1][p2][0];
                        kpp[sq][p1][p2].p[i + 1] = (*kpp_w_)[sq][p1][p2][1];
                        kpp[sq][p1][p2].p[i + 2] = (*kpp_w_)[sq][p1][p2][1];
                    }
                }

        for (auto s1 : Squares)
            for (auto s2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end; p++)
                {
                    kkp[s1][s2][p].m = _mm256_setzero_si256();

                    for (int i = 0; i < 1; i+=2)
                    {
                        kkp[s1][s2][p].p[i] = (int16_t)(*kkp_w_)[s1][s2][p][0];
                        kkp[s1][s2][p].p[i + 1] = (int16_t)(*kkp_w_)[s1][s2][p][1];
                        kkp[s1][s2][p].p[i + 2] = (int16_t)(*kkp_w_)[s1][s2][p][1];
                    }
                }

#endif
#ifdef LEARN
        evalLearnInit();
#endif
    }

    int32_t EvalSum::calcScore(const Board& b)
    {
        const Turn t = b.turn();
        double progress = b.state()->progress.rate();

#if 0
        // 進行度ボーナス
#if 0
        int pro = int(progress * 8.0);
        pro = (pro < 0 ? 0 : pro > 7 ? 7 : pro) * 2;
#else
        int pro = 0;
#endif
        int32_t score_board = u[KPP_B].p[pro + t] - u[KPP_W].p[pro + ~t] + u[KKP_KK].p[pro + t];
        return t == BLACK ? score_board : -score_board;
#elif 0
        // [0](先手玉KPP) + [1](後手玉KPP) + [2](KKとKKP) 
        int32_t score_board = u[KPP_B].p[ON_BOARD] - u[KPP_W].p[ON_BOARD] + u[KKP_KK].p[ON_BOARD];
        int32_t score_turn = u[KPP_B].p[TURN] + u[KPP_W].p[TURN] + u[KKP_KK].p[TURN];

        // 進行度ボーナス
        int pro = int(progress * 6.0);
        pro = pro < 0 ? 0 : pro > 5 ? 5 : pro;
        score_board += u[KPP_B].p[PROGRESS + pro] - u[KPP_W].p[PROGRESS + pro] + u[KKP_KK].p[PROGRESS + pro];
        return (t == BLACK ? score_board : -score_board) + score_turn;
#else
        // [0](先手玉KPP) + [1](後手玉KPP) + [2](KKとKKP) 
        int32_t score_board = u[KPP_B].p[ON_BOARD] - u[KPP_W].p[ON_BOARD] + u[KKP_KK].p[ON_BOARD];

        // 手番側に与えられるボーナス
        int32_t score_turn = u[KPP_B].p[TURN + t] + u[KPP_W].p[TURN + ~t] + u[KKP_KK].p[TURN + t];

        // 進行度ボーナス(手番と関係ないボーナス)
        int pro = int(progress * 5.0);
        pro = (pro < 0 ? 0 : pro > 4 ? 4 : pro);
        int32_t score_prog_f = u[KPP_B].p[3 + pro] - u[KPP_W].p[3 + pro] + u[KKP_KK].p[3 + pro];

        // 進行度ボーナス(手番側に与えられるボーナス)
        int pro2 = int(progress * 4.0);
        pro2 = (pro2 < 0 ? 0 : pro2 > 3 ? 3 : pro2) * 2;
        int32_t score_prog_g = u[KPP_B].p[8 + pro2 + t] + u[KPP_W].p[8 + pro2 + ~t] + u[KKP_KK].p[8 + pro2 + t];

        return (t == BLACK ? score_board + score_prog_f : -score_board - score_prog_f) + score_turn + score_prog_g;
#endif
    }

    // KPP,KPのスケール
    const int FV_SCALE = 32;

    // 駒割り以外の全計算
    // b.st->BKPP,WKPP,KPPを初期化する。Board::set()で一度だけ呼び出される。(以降は差分計算)
    Score computeEval(const Board& b)
    {
        assert(kpp != nullptr);

        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk0 = b.kingSquare(WHITE);
        auto sq_wk1 = inverse(sq_wk0);
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        EvalSum sum;
        sum.u[0].clear();
        sum.u[1].clear();

        // KKの計算
        sum.u[2] = kk[sq_bk0][sq_wk0];

        for (int i = 0; i < PIECE_NO_KING; ++i)
        {
            auto k0 = list_fb[i];
            auto k1 = list_fw[i];

            for (int j = 0; j < i; ++j)
            {
                auto l0 = list_fb[j];
                auto l1 = list_fw[j];
                sum.u[0] += kpp[sq_bk0][k0][l0];
                sum.u[1] += kpp[sq_wk1][k1][l1];
            }

            sum.u[2] += kkp[sq_bk0][sq_wk0][k0];
        }

        b.state()->sum = sum;

        return Score(sum.calcScore(b) / FV_SCALE);
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

        auto now = st;
        auto prev = st->previous;

        if (prev->sum.isNotEvaluated())
            return computeEval(b);

        // この差分を求める
        {
            auto sq_bk0 = b.kingSquare(BLACK);
            auto sq_wk0 = b.kingSquare(WHITE);
            auto sq_wk1 = inverse(sq_wk0);

            auto list_fb = b.evalList()->pieceListFb();
            auto list_fw = b.evalList()->pieceListFw();

            int k0, k1, k2, k3;
            int i, j;
            auto& dp = now->dirty_piece;
            sum = prev->sum;

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

                    // BKPP
                    sum.u[0].clear();

                    // このときKKPは差分で済まない。
                    sum.u[2] = kk[sq_bk0][sq_wk0];

                    // 片側まるごと計算
                    for (i = 0; i < PIECE_NO_KING; i++)
                    {
                        k0 = list_fb[i];
                        sum.u[2] += kkp[sq_bk0][sq_wk0][k0];

                        for (j = 0; j < i; j++)
                            sum.u[0] += kpp[sq_bk0][k0][list_fb[j]];
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
                            sum.u[1] -= kpp[sq_wk1][k1][list_fw[i]];
                            sum.u[1] += kpp[sq_wk1][k3][list_fw[i]];
                        }
                        for (++i; i < PIECE_NO_KING; ++i)
                        {
                            sum.u[1] -= kpp[sq_wk1][k1][list_fw[i]];
                            sum.u[1] += kpp[sq_wk1][k3][list_fw[i]];
                        }
                    }

                }
                else {
                    // ----------------------------
                    // 後手玉が移動したときの計算
                    // ----------------------------
                    assert(dirty == PIECE_NO_WKING);

                    sum.u[1].clear();
                    sum.u[2] = kk[sq_bk0][sq_wk0];

                    for (i = 0; i < PIECE_NO_KING; i++)
                    {
                        k0 = list_fb[i]; // これ、KKPテーブルにk1側も入れておいて欲しい気はするが..
                        k1 = list_fw[i];
                        sum.u[2] += kkp[sq_bk0][sq_wk0][k0];

                        for (j = 0; j < i; j++)
                            sum.u[1] += kpp[sq_wk1][k1][list_fw[j]];
                    }

                    if (k == 2)
                    {
                        k0 = dp.pre_piece[1].fb;
                        k2 = dp.now_piece[1].fb;

                        dirty = dp.piece_no[1];

                        for (i = 0; i < dirty; ++i)
                        {
                            sum.u[0] -= kpp[sq_bk0][k0][list_fb[i]];
                            sum.u[0] += kpp[sq_bk0][k2][list_fb[i]];
                        }
                        for (++i; i < PIECE_NO_KING; ++i)
                        {
                            sum.u[0] -= kpp[sq_bk0][k0][list_fb[i]];
                            sum.u[0] += kpp[sq_bk0][k2][list_fb[i]];
                        }
                    }
                }

            }
            else {
                // ----------------------------
                // 玉以外が移動したときの計算
                // ----------------------------

#define ADD_BWKPP(W0,W1,W2,W3) { \
          sum.u[0] -= kpp[sq_bk0][W0][list_fb[i]]; \
          sum.u[1] -= kpp[sq_wk1][W1][list_fw[i]]; \
          sum.u[0] += kpp[sq_bk0][W2][list_fb[i]]; \
          sum.u[1] += kpp[sq_wk1][W3][list_fw[i]]; \
}
                if (k == 1)
                {
                    // 移動した駒が一つ。

                    k0 = dp.pre_piece[0].fb;
                    k1 = dp.pre_piece[0].fw;
                    k2 = dp.now_piece[0].fb;
                    k3 = dp.now_piece[0].fw;

                    // KKP差分
                    sum.u[2] -= kkp[sq_bk0][sq_wk0][k0];
                    sum.u[2] += kkp[sq_bk0][sq_wk0][k2];

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
                    sum.u[2] -= kkp[sq_bk0][sq_wk0][k0];
                    sum.u[2] += kkp[sq_bk0][sq_wk0][k2];
                    sum.u[2] -= kkp[sq_bk0][sq_wk0][m0];
                    sum.u[2] += kkp[sq_bk0][sq_wk0][m2];

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

                    sum.u[0] -= kpp[sq_bk0][k0][m0];
                    sum.u[1] -= kpp[sq_wk1][k1][m1];
                    sum.u[0] += kpp[sq_bk0][k2][m2];
                    sum.u[1] += kpp[sq_wk1][k3][m3];
                }
            }
        }

        now->sum = sum;

        // 差分計算終わり
    CALC_DIFF_END:
        return (Score)(sum.calcScore(b) / FV_SCALE);
    }

    // 評価関数
    Score evaluate(const Board& b)
    {
        // 差分計算
        auto score = calcEvalDiff(b);

#if 0
        // 非差分計算
        auto sscore = computeEval(b);
        auto sum1 = b.state()->sum;

        // 差分計算と非差分計算との計算結果が合致するかのテスト。(さすがに重いのでコメントアウトしておく)
        if (score != sscore)
        {
            std::cout << b << score << sscore << std::endl;
        }
        assert(score == computeEval(b));
#endif
        auto material = b.turn() == BLACK ? b.state()->material : -b.state()->material;
        return score + material;
    }
}
#endif // EVAL_KPPT