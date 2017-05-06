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

#include "learn.h"

#if defined LEARN && defined EVAL_KPPTP

#include <codecvt>
#include <fstream>
#include <unordered_set>
#include "eval_kpptp.h"
#include "usi.h"

#define kk (*et.kk_)
#define kpp (*et.kpp_)
#define kkp (*et.kkp_)

// 次元下げを行うとき定義
//#define DIMENSION_DOWN_KPP

namespace Eval
{
    // 絶対値を抑制するマクロ
#define SET_A_LIMIT_TO(X, MIN, MAX)  \
for (int ss = 0; ss < 16; ss++){\
    X[ss] = std::min(X[ss], (MAX));    \
    X[ss] = std::max(X[ss], (MIN));    \
}

    BonaPiece INV_PIECE[fe_end], MIR_PIECE[fe_end];

    // kpp配列の同じ重みが入るべき場所に同じ値を書き込む。
    void writeKpp(Square k1, BonaPiece p1, BonaPiece p2, Value& value)
    {
        assert(isOK(k1) && isOK2(p1) && isOK2(p2));
#ifdef DIMENSION_DOWN_KPP
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirror(k1);

        /*assert(kpp[k1][p1][p2] == kpp[k1][p2][p1]);
        assert(kpp[k1][p1][p2] == kpp[mk1][mp1][mp2] || fileOf(k1) == FILE_5);
        assert(kpp[k1][p1][p2] == kpp[mk1][mp2][mp1] || fileOf(k1) == FILE_5);
*/
        kpp[k1][p1][p2]
            = kpp[k1][p2][p1]
            = kpp[mk1][mp1][mp2]
            = kpp[mk1][mp2][mp1]
            = value;
#else
        kpp[k1][p1][p2] = kpp[k1][p2][p1] = value;
#endif
    }

    // 書き込む場所を少なくする目的で、一番若いアドレスを返す。
    uint64_t getKppIndex(Square k1, BonaPiece p1, BonaPiece p2)
    {
        assert(isOK(k1) && isOK2(p1) && isOK2(p2));

        const auto q0 = &kpp[0][0][0];

#ifdef DIMENSION_DOWN_KPP
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirror(k1);

        auto q1 = &kpp[k1][p1][p2] - q0;
        auto q2 = &kpp[k1][p2][p1] - q0;
        auto q3 = &kpp[mk1][mp1][mp2] - q0;
        auto q4 = &kpp[mk1][mp2][mp1] - q0;

        return std::min({ q1, q2, q3, q4 });
#else
        auto q1 = &kpp[k1][p1][p2] - q0;
        auto q2 = &kpp[k1][p2][p1] - q0;

        return std::min(q1, q2);
#endif
    }

    void writeKkp(Square k1, Square k2, BonaPiece p1, Value& value)
    {
        kkp[k1][k2][p1] = value;
    }

    // 勾配をdelta分増やす。
    void Weight::addGrad(WeightValue& delta)
    {
        g += delta;

#ifdef USE_SGD_UPDATE
        count++;
#endif
    }

#define eta2 (eta / 4.0)
#define eta3 (eta / 8.0)

    // 勾配を重みに反映させる。
    bool Weight::update(bool skip_update)
    {
#ifdef USE_SGD_UPDATE
        
        if (!g)
            return false;

        if (count)
        {
            g /= count;
            count = 0;

            //g.p[0] *= eta;
            //g.p[1] *= eta2;
            //g.p[2] *= eta2;

            //for (int i = 3; i < 16; i++)
            //    g.p[i] *= eta3;

            g *= eta;
            //g *= 0.1;
            w -= g;
        }

#elif defined USE_ADA_GRAD_UPDATE
        bool update = false;

        auto func = [&](int i, double e) 
        {
            if (g.p[i])
            {
                g2.p[i] += g.p[i] * g.p[i];

                // 値が小さいうちはskipする
                if (!skip_update && g2.p[i] >= 0.1f)
                {
                    update = true;
                    w.p[i] = w.p[i] - e * g.p[i] / sqrt(g2.p[i]);
                }
            }
        };

        func(0, eta);
        func(1, eta2);
        func(2, eta2);

        for (int i = 3; i < 16; i++)
            func(i, eta3);

#endif
        for (int i = 0; i < M_CNT; i++)
            g.m[i] = _mm256_setzero_p();

#ifdef USE_ADA_GRAD_UPDATE
        return update;
#else
        return !skip_update;
#endif
}

    LearnFloatType Weight::eta;

    Weight(*kk_w_)[SQ_MAX][SQ_MAX];
    Weight(*kpp_w_)[SQ_MAX][fe_end][fe_end];
    Weight(*kkp_w_)[SQ_MAX][SQ_MAX][fe_end];

#define kk_w (*kk_w_)
#define kpp_w (*kpp_w_)
#define kkp_w (*kkp_w_)

    void initGrad()
    {
        if (kk_w_ == nullptr)
        {
            const auto sizekk  = uint64_t(SQ_MAX) * uint64_t(SQ_MAX);
            const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
            const auto sizekkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end);

            kk_w_  = (Weight(*)[SQ_MAX][SQ_MAX])        _aligned_malloc(sizekk  * sizeof(Weight), 32);
            kpp_w_ = (Weight(*)[SQ_MAX][fe_end][fe_end])_aligned_malloc(sizekpp * sizeof(Weight), 32);
            kkp_w_ = (Weight(*)[SQ_MAX][SQ_MAX][fe_end])_aligned_malloc(sizekkp * sizeof(Weight), 32);

            memset(kk_w_,  0, sizeof(Weight) * sizekk);
            memset(kpp_w_, 0, sizeof(Weight) * sizekpp);
            memset(kkp_w_, 0, sizeof(Weight) * sizekkp);

#ifdef RESET_TO_ZERO_VECTOR
            std::cout << "\n[RESET_TO_ZERO_VECTOR]";
            /*memset(kk,  0, sizeof(Value) * sizekk);
            memset(kpp, 0, sizeof(Value) * sizekpp);
            memset(kkp, 0, sizeof(Value) * sizekkp);
*/
            for (auto k1 : Squares)
                for (auto k2 : Squares)
                {
                    kk[k1][k2].m = _mm256_setzero_si256();

                    for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                        kkp[k1][k2][p].m = _mm256_setzero_si256();
                };

            for (auto k : Squares)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                        kpp[k][p1][p2].m = _mm256_setzero_si256();
#else
            // 元の重みをコピー
            for (auto k1 : Squares)
                for (auto k2 : Squares)
                {
                    for (int i = 0; i < PARAMS_CNT; i++)
                        kk_w[k1][k2].w.p[i] = kk[k1][k2].p[i];

                    for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                        for (int i = 0; i < PARAMS_CNT; i++)
                            kkp_w[k1][k2][p].w.p[i] = kkp[k1][k2][p].p[i];
                };

            for (auto k : Squares)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                        for (int i = 0; i < PARAMS_CNT; i++)
                            kpp_w[k][p1][p2].w.p[i] = kpp[k][p1][p2].p[i];
#endif
        }
    }

    // 現在の局面で出現している特徴すべてに対して、勾配値を勾配配列に加算する。
    void addGrad(Board& b, Turn root_turn, double delta_grad)
    {
        auto sq_bk = b.kingSquare(BLACK);
        auto sq_wk = b.kingSquare(WHITE);
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();
        int i, j;
        BonaPiece k0, k1, l0, l1;

        // 手番を考慮しない値
        auto f = (root_turn == BLACK) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);

        // 手番を考慮する値
        auto g = (root_turn == b.turn()) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);

#ifdef USE_AVX
        WeightValue w, mw;

        for (int i = 0; i < M_CNT; i++)
            w.m[i] = _mm256_setzero_p();
#else
        WeightValue w = { 0 };
#endif
#if 0
        w.p[0] = f;
        w.p[1] = g;
        int pro = int(6.0 * b.state()->progress.rate());
        pro = pro < 0 ? 0 : pro > 5 ? 5 : pro;
        w.p[2 + pro] = f;

        mw.p[0] = -w.p[0];
        mw.p[1] = w.p[1];
        for (int i = 2; i < PARAMS_CNT; i++)
            mw.p[i] = -w.p[i];
#endif
        mw = w;
#if 0
        int pro = int(b.state()->progress.rate() * 8.0);
        pro = (pro < 0 ? 0 : pro > 7 ? 7 : pro) * 2;
#elif 0
        w.p[pro + b.turn()] = f;
        mw.p[pro + ~b.turn()] = -f;
#else
        //w.p[0] = f;
        //w.p[1 + b.turn()] = g;
        double progress = b.state()->progress.rate();
        // 進行度ボーナス(手番と関係ないボーナス)
        int pro = int(progress * 5.0);
        pro = (pro < 0 ? 0 : pro > 4 ? 4 : pro);
        w.p[3 + pro] = f;

        // 進行度ボーナス(手番側に与えられるボーナス)
        int pro2 = int(progress * 4.0);
        pro2 = (pro2 < 0 ? 0 : pro2 > 3 ? 3 : pro2) * 2;
        w.p[8 + pro2 + b.turn()] = g;

        //mw.p[0] = -f;
        //mw.p[1 + ~b.turn()] = g;
        mw.p[3 + pro] = -f;
        mw.p[8 + pro2 + ~b.turn()] = g;
#endif
        // KK
        kk_w[sq_bk][sq_wk].addGrad(w);

        for (i = 0; i < PIECE_NO_KING; ++i)
        {
            k0 = list_fb[i];
            k1 = list_fw[i];

            for (j = 0; j < i; ++j)
            {
                l0 = list_fb[j];
                l1 = list_fw[j];

                // kpp配列に関してはミラー(左右判定)とフリップ(180度回転)の次元下げを行う。
                ((Weight*)kpp_w_)[getKppIndex(sq_bk, k0, l0)].addGrad(w);
                ((Weight*)kpp_w_)[getKppIndex(inverse(sq_wk), k1, l1)].addGrad(mw);
            }

            kkp_w[sq_bk][sq_wk][k0].addGrad(w);
        }
    }


    void updateWeights(uint64_t mini_batch_size, uint64_t epoch)
    {
        // 3回目まではwのupdateを保留する。
        // ただし、SGDは履歴がないのでこれを行なう必要がない。
        bool skip_update =
#ifdef USE_SGD_UPDATE
            false;
#else
            epoch <= 100;
#endif
        // kkpの一番大きな値を表示させることで学習が進んでいるかのチェックに用いる。
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        LearnFloatType max_kktp[PARAMS_CNT] = { 0.0 };
#endif
        // 学習メソッドに応じた学習率設定
#ifdef USE_SGD_UPDATE
#if defined (LOSS_FUNCTION_IS_CROSS_ENTOROPY)
        Weight::eta = 1.0f;
#elif defined (LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
        Weight::eta = 256.0f;
#endif
#elif defined USE_ADA_GRAD_UPDATE
        Weight::eta = 8.0;
#endif

        auto func = [&](size_t id)
        {
            for (auto k1 = 9 * id; k1 < 9 * (id + 1); k1++)
                for (auto k2 : Squares)
                {
                    auto& w = kk_w[k1][k2];

                    // wの値にupdateがあったなら、値を制限して、かつ、kkに反映させる。
                    if (w.update(skip_update))
                    {
                        // 絶対値を抑制する。
                        SET_A_LIMIT_TO(w.w.p, (LearnFloatType)(INT16_MIN), (LearnFloatType)(INT16_MAX));

                        for (int i = 0; i < PARAMS_CNT; i++)
                            kk[k1][k2].p[i] = w.w.p[i];
                    }
                }

            for (Square k = Square(9 * id); k < 9 * (id + 1); k++)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    {
                        auto& w = kpp_w[k][p1][p2];

                        if (w.update(skip_update))
                        {
                            // 絶対値を抑制する。
                            SET_A_LIMIT_TO(w.w.p, (LearnFloatType)(INT16_MIN), (LearnFloatType)(INT16_MAX));

                            auto w2 = kpp[k][p1][p2];

                            for (int i = 0; i < PARAMS_CNT; i++)
                                w2.p[i] = w.w.p[i];

                            writeKpp(k, p1, p2, w2);
                        }
                    }

            for (auto k1 = 9 * id; k1 < 9 * (id + 1); k1++)
                for (auto k2 : Squares)
                    for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                    {
                        auto& w = kkp_w[k1][k2][p];
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                        for (int i = 0; i < PARAMS_CNT; i++)
                            max_kktp[i] = std::max(max_kktp[i], abs(w.w.p[i]));
#endif
                        if (w.update(skip_update))
                        {
                            SET_A_LIMIT_TO(w.w.p, (LearnFloatType)(INT16_MIN), (LearnFloatType)(INT16_MAX));

                            auto w2 = kkp[k1][k2][p];

                            for (int i = 0; i < PARAMS_CNT; i++)
                                w2.p[i] = w.w.p[i];

                            kkp[k1][k2][p] = w2;
                        }
                    }
        };

        std::thread th[9];

        for (int i = 0; i < 9; i++)
            th[i] = std::thread(func, i);

        for (int i = 0; i < 9; i++)
            th[i].join();
#if 0
        for (auto k1 : Squares)
            for (auto k2 : Squares)
            {
                auto& w = kk_w[k1][k2];

                // wの値にupdateがあったなら、値を制限して、かつ、kkに反映させる。
                if (w.update(skip_update))
                {
                    // 絶対値を抑制する。
                    SET_A_LIMIT_TO(w.w.p, (LearnFloatType)(INT16_MIN), (LearnFloatType)(INT16_MAX));

                    for (int i = 0; i < PARAMS_CNT; i++)
                        kk[k1][k2].p[i] = w.w.p[i];
                }
            }

        for (auto k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                {
                    auto& w = kpp_w[k][p1][p2];
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                    for (int i = 0; i < PARAMS_CNT; i++)
                        max_kktp[i] = std::max(max_kktp[i], abs(w.w.p[i]));
#endif
                    if (w.update(skip_update))
                    {
                        // 絶対値を抑制する。
                        SET_A_LIMIT_TO(w.w.p, (LearnFloatType)(INT16_MIN), (LearnFloatType)(INT16_MAX));

                        auto w2 = kpp[k][p1][p2];
                        
                        for (int i = 0; i < PARAMS_CNT; i++)
                            w2.p[i] = w.w.p[i];

                        writeKpp(k, p1, p2, w2);
                    }
                }

        for (auto k1 : Squares)
            for (auto k2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                {
                    auto& w = kkp_w[k1][k2][p];


                    // std::cout << "\n" << w.g[0] << " & " << w.g[1];

                    if (w.update(skip_update))
                    {
                        SET_A_LIMIT_TO(w.w.p, (LearnFloatType)(INT16_MIN), (LearnFloatType)(INT16_MAX));

                        auto w2 = kkp[k1][k2][p];

                        for (int i = 0; i < PARAMS_CNT; i++)
                            w2.p[i] = w.w.p[i];

                        kkp[k1][k2][p] = w2;
                    }
                }
#endif
        
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        std::cout << "\nmax_kk = ";

        for (int i = 0; i < PARAMS_CNT; i++)
            std::cout << max_kktp[i] << ", ";

        std::cout << std::endl;
#endif
    }

    // 学習のためのテーブルの初期化
    void evalLearnInit()
    {
        // fとeとの交換
        int t[] = {
            f_hand_pawn - 1    , e_hand_pawn - 1   ,
            f_hand_lance - 1   , e_hand_lance - 1  ,
            f_hand_knight - 1  , e_hand_knight - 1 ,
            f_hand_silver - 1  , e_hand_silver - 1 ,
            f_hand_gold - 1    , e_hand_gold - 1   ,
            f_hand_bishop - 1  , e_hand_bishop - 1 ,
            f_hand_rook - 1    , e_hand_rook - 1   ,
            f_pawn             , e_pawn            ,
            f_lance            , e_lance           ,
            f_knight           , e_knight          ,
            f_silver           , e_silver          ,
            f_gold             , e_gold            ,
            f_bishop           , e_bishop          ,
            f_horse            , e_horse           ,
            f_rook             , e_rook            ,
            f_dragon           , e_dragon          ,
        };

        // 未初期化の値を突っ込んでおく。
        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
        {
            INV_PIECE[p] = (BonaPiece)-1;

            // mirrorは手駒に対しては機能しない。元の値を返すだけ。
            MIR_PIECE[p] = (p < f_pawn) ? p : (BonaPiece)-1;
        }

        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
        {
            for (int i = 0; i < 32 /* t.size() */; i += 2)
            {
                if (t[i] <= p && p < t[i + 1])
                {
                    Square sq = (Square)(p - t[i]);

                    // 見つかった!!
                    BonaPiece q = (p < fe_hand_end) ? BonaPiece(sq + t[i + 1]) : (BonaPiece)(inverse(sq) + t[i + 1]);
                    INV_PIECE[p] = q;
                    INV_PIECE[q] = p;

                    /*
                    ちょっとトリッキーだが、pに関して盤上の駒は
                    p >= fe_hand_end
                    のとき。

                    このpに対して、nを整数として(上のコードのiは偶数しかとらない)、
                    a)  t[2n + 0] <= p < t[2n + 1] のときは先手の駒
                    b)  t[2n + 1] <= p < t[2n + 2] のときは後手の駒
                    である。

                    ゆえに、a)の範囲にあるpをq = inverse(p-t[2n+0]) + t[2n+1] とすると180度回転させた升にある後手の駒となる。
                    そこでpとqをswapさせてINV_PIECE[ ]を初期化してある。
                    */

                    // 手駒に関してはmirrorなど存在しない。
                    if (p < fe_hand_end)
                        continue;

                    BonaPiece r1 = (BonaPiece)(mirror(sq) + t[i]);
                    MIR_PIECE[p] = r1;
                    MIR_PIECE[r1] = p;

                    BonaPiece p2 = (BonaPiece)(sq + t[i + 1]);
                    BonaPiece r2 = (BonaPiece)(mirror(sq) + t[i + 1]);
                    MIR_PIECE[p2] = r2;
                    MIR_PIECE[r2] = p2;

                    break;
                }
            }
        }

        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
            if (INV_PIECE[p] == (BonaPiece)-1
                || MIR_PIECE[p] == (BonaPiece)-1
                )
            {
                // 未初期化のままになっている。上のテーブルの初期化コードがおかしい。
                assert(false);
            }

#if 0
        // 評価関数のミラーをしても大丈夫であるかの事前検証
        // 値を書き込んだときにassertionがあるので、ミラーしてダメである場合、
        // そのassertに引っかかるはず。

        // AperyのWCSC26の評価関数、kppのp1==0とかp1==20(後手の0枚目の歩)とかの
        // ところにゴミが入っていて、これを回避しないとassertに引っかかる。

        std::unordered_set<BonaPiece> s;
        std::vector<int> a =
        {
            f_hand_pawn - 1,e_hand_pawn - 1,
            f_hand_lance - 1, e_hand_lance - 1,
            f_hand_knight - 1, e_hand_knight - 1,
            f_hand_silver - 1, e_hand_silver - 1,
            f_hand_gold - 1, e_hand_gold - 1,
            f_hand_bishop - 1, e_hand_bishop - 1,
            f_hand_rook - 1, e_hand_rook - 1,
        };
        for (auto b : a)
            s.insert((BonaPiece)b);

        // さらに出現しない升の盤上の歩、香、桂も除外(Aperyはここにもゴミが入っている)
        for (Rank r = RANK_1; r <= RANK_2; ++r)
            for (File f = FILE_1; f <= FILE_9; ++f)
            {
                if (r == RANK_1)
                {
                    // 1段目の歩
                    BonaPiece b1 = BonaPiece(f_pawn + sqOf(f, r));
                    s.insert(b1);
                    s.insert(INV_PIECE[b1]);

                    // 1段目の香
                    BonaPiece b2 = BonaPiece(f_lance + sqOf(f, r));
                    s.insert(b2);
                    s.insert(INV_PIECE[b2]);
                }

                // 1,2段目の桂
                BonaPiece b = BonaPiece(f_knight + sqOf(f, r));
                s.insert(b);
                s.insert(INV_PIECE[b]);
            }

        std::cout << "\nchecking writeKpp()..";
        for (auto sq : Squares)
        {
            std::cout << sq << ' ';
            for (BonaPiece p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (BonaPiece p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    if (!s.count(p1) && !s.count(p2))
                        writeKpp(sq, p1, p2, kpp[sq][p1][p2]);
        }
        std::cout << "\nchecking kkp_write()..";

        for (auto sq1 : Squares)
        {
            std::cout << sq1 << ' ';
            for (auto sq2 : Squares)
                for (BonaPiece p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    if (!s.count(p1))
                        writeKkp(sq1, sq2, p1, kkp[sq1][sq2][p1]);
        }
        std::cout << "..done!" << std::endl;
#endif
    }


    void saveEval(std::string dir_name)
    {
        {
            auto eval_dir = path((std::string)USI::Options["EvalSaveDir"], dir_name);

            mkdir(eval_dir);

            // KK
            std::ofstream ofsKK(path(eval_dir, KK_BIN), std::ios::binary);
            if (!ofsKK.write(reinterpret_cast<char*>(kk), sizeof(kk)))
                goto Error;

            // KKP
            std::ofstream ofsKKP(path(eval_dir, KKP_BIN), std::ios::binary);
            if (!ofsKKP.write(reinterpret_cast<char*>(kkp), sizeof(kkp)))
                goto Error;

            const size_t kpp_size = sizeof(Value) * size_t(SQ_MAX) * size_t(fe_end) * size_t(fe_end);
            // KPP
            std::ofstream ofsKPP(path(eval_dir, KPP_BIN), std::ios::binary);
            if (!ofsKPP.write(reinterpret_cast<char*>(kpp), kpp_size))
                goto Error;

            std::cout << "save_eval() finished. folder = " << eval_dir << std::endl;

            return;
        }

    Error:;
        std::cout << "Error : save_eval() failed" << std::endl;
    }

} // namespace Eval

#endif
