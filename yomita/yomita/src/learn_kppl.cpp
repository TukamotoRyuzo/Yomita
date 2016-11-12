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

#if defined LEARN && defined EVAL_KPPL

#include <codecvt>
#include <fstream>
#include <unordered_set>
#include "eval_kppl.h"
#include "usi.h"

#define kpp (*et.kpp_)

namespace Eval
{
    // 絶対値を抑制するマクロ
#define SET_A_LIMIT_TO(X,MIN,MAX)  \
    X = std::min(X,(MAX));   \
    X = std::max(X,(MIN));   \

    BonaPiece INV_PIECE[fe_end], MIR_PIECE[fe_end];

    // kpp配列の同じ重みが入るべき場所に同じ値を書き込む。
    void writeKpp(Square k1, BonaPiece p1, BonaPiece p2, Value value)
    {
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirror(k1);

        //std::cout << k1 << p1 << p2 << std::endl;
        //std::cout << mk1 << mp1 << mp2 << std::endl;

        assert(kpp[k1][p1][p2] == kpp[k1][p2][p1]);
        assert(kpp[k1][p1][p2] == kpp[mk1][mp1][mp2] || fileOf(k1) == FILE_5);
        assert(kpp[k1][p1][p2] == kpp[mk1][mp2][mp1] || fileOf(k1) == FILE_5);

        kpp[k1][p1][p2] = kpp[k1][p2][p1] = kpp[mk1][mp1][mp2] = kpp[mk1][mp2][mp1] = value;
    }

    // 一番若いアドレスを返す。
    uint64_t getKppIndex(Square k1, BonaPiece p1, BonaPiece p2)
    {
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirror(k1);

        const auto q0 = &kpp[0][0][0];

        auto q1 = &kpp[k1][p1][p2] - q0;
        auto q2 = &kpp[k1][p2][p1] - q0;
        auto q3 = &kpp[mk1][mp1][mp2] - q0;
        auto q4 = &kpp[mk1][mp2][mp1] - q0;

        return std::min({ q1, q2, q3, q4 });
    }

    // 勾配をdelta分増やす。
    void Weight::addGrad(WeightValue delta)
    {
        g += delta;
#ifdef USE_SGD_UPDATE
        count++;
        assert(abs(g / count) <= 1);
#endif
    }

    // 勾配を重みに反映させる。
    bool Weight::update(bool skip_update)
    {
#ifdef USE_SGD_UPDATE
        if (g == 0)
            return false;

        if (count)
        {
            g /= count;
            count = 0;
            w = w - eta * g;
        }

#elif defined USE_ADA_GRAD_UPDATE
        if (g == 0)
            return false;

        g2 += WeightValue(g * g);

        // 値が小さいうちはskipする
        if (g2 >= 0.1f && !skip_update)
            w = w - eta * g / sqrt(g2);

#elif defined USE_YANE_GRAD_UPDATE
        if (g == 0)
            return false;

        g2 = alpha * g2 + g * g;

        // skip_updateのときはg2の更新に留める。
        if (!skip_update)
        {
            //std::cout << "w = " << w;
            w = w - eta * g / sqrt(g2 + epsilon);
            //std::cout << "→" << w << std::endl;
        }

#elif defined USE_ADAM_UPDATE
        v = beta * v + (1 - beta) * g;
        r = gamma * r + (1 - gamma) * g * g;

        if (!skip_update)
        {
            //std::cout << "g = " << g << " v = " << v << " r = " << r << "\n";
            //std::cout << "w = " << w;
            w = w - eta / (sqrt((double)r / rt) + epsilon) * v / bt;
            //std::cout << "→ " << w << std::endl;
        }


#endif
        g = 0;

        return !skip_update;
    }

#ifdef USE_ADAM_UPDATE
    double Weight::bt;
    double Weight::rt;
#else
    LearnFloatType Weight::eta;
#endif

    Weight(*kpp_w_)[SQ_MAX][fe_end][fe_end];

#define kpp_w (*kpp_w_)

    void initGrad()
    {
        if (kpp_w_ == nullptr)
        {
            const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);

            kpp_w_ = (Weight(*)[SQ_MAX][fe_end][fe_end])new Weight[sizekpp];

            memset(kpp_w_, 0, sizeof(Weight) * sizekpp);

#ifdef RESET_TO_ZERO_VECTOR
            std::cout << "\n[RESET_TO_ZERO_VECTOR]";
            memset(&kpp, 0, sizeof(Value) * sizekpp);
#else
            for (auto k : Squares)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                        kpp_w[k][p1][p2].w = LearnFloatType(kpp[k][p1][p2]);

#endif
        }
    }

    // 現在の局面で出現している特徴すべてに対して、勾配値を勾配配列に加算する。
    void addGrad(Board& b, Turn root_turn, double delta_grad)
    {
        // 手番を考慮しない値
        auto f = (root_turn == BLACK) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);

        auto sq_bk = b.kingSquare(BLACK);
        auto sq_wk = inverse(b.kingSquare(WHITE));
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        int i, j;
        BonaPiece k0, k1, k2, k3;

        for (i = 0; i < PIECE_NO_KING; i++)
        {
            k0 = list_fb[i];
            k1 = list_fw[i];

            for (j = 0; j < i; j++)
            {
                k2 = list_fb[j];
                k3 = list_fw[j];

                ((Weight*)kpp_w_)[getKppIndex(sq_bk, k0, k2)].addGrad(f);
                ((Weight*)kpp_w_)[getKppIndex(sq_wk, k1, k3)].addGrad(-f);
            }
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
            epoch <= 3;
#endif

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        WeightValue max_kpp = 0.0f;
#endif

        // 学習メソッドに応じた学習率設定
#ifdef USE_SGD_UPDATE
#if defined (LOSS_FUNCTION_IS_CROSS_ENTOROPY)
        Weight::eta = 3.2f;
#elif defined (LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
        Weight::eta = 32.0f;
#endif
#elif defined USE_ADA_GRAD_UPDATE
        Weight::eta = 5.0f / float(mini_batch_size/ 1000000.0 );
#elif defined USE_YANE_GRAD_UPDATE
#if defined (LOSS_FUNCTION_IS_CROSS_ENTOROPY)
        Weight::eta = 5.0f / float(1000000.0 / mini_batch_size);
#elif defined (LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
        Weight::eta = 20.0f / float(1000000.0 / mini_batch_size);
#endif
#elif defined USE_ADAM_UPDATE
        Weight::bt = 1.0 - pow((double)Weight::beta, (double)epoch);
        Weight::rt = 1.0 - pow((double)Weight::gamma, (double)epoch);
#endif

        for (auto k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                {
                    auto& w = kpp_w[k][p1][p2];

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                    if (max_kpp < w.w)
                        max_kpp = w.w;
#endif
                    if (w.update(skip_update))
                    {
                        // 絶対値を抑制する。
                        SET_A_LIMIT_TO(w.w, (LearnFloatType)(INT16_MIN / 2), (LearnFloatType)(INT16_MAX / 2));
                        writeKpp(k, p1, p2, Value((int16_t)w.w));
                    }
                }

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        SYNC_COUT << "max_kpp = " << max_kpp << SYNC_ENDL;
#endif
    }

    // 学習のためのテーブルの初期化
    void evalLearnInit()
    {
        // fとeとの交換
        int t[] =
        {
            f_hand_pawn    , e_hand_pawn   ,
            f_hand_lance    , e_hand_lance  ,
            f_hand_knight   , e_hand_knight  ,
            f_hand_silver   , e_hand_silver ,
            f_hand_gold   , e_hand_gold   ,
            f_hand_bishop   , e_hand_bishop  ,
            f_hand_rook    , e_hand_rook    ,
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

        INV_PIECE[0] = BonaPiece(0);
        MIR_PIECE[0] = BonaPiece(0);

        // 未初期化の値を突っ込んでおく。
        for (BonaPiece p = BONA_PIECE_ZERO + 1; p < fe_end; ++p)
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

                    // 持ち駒ならそのままe_にして、盤面上の駒ならinverseする。
                    BonaPiece q = (p < fe_hand_end) ? BonaPiece(sq + t[i + 1]) : (BonaPiece)(inverse(sq) + t[i + 1]);
                    INV_PIECE[p] = q;
                    INV_PIECE[q] = p;

                    // 手駒に関してはmirrorなど存在しない。
                    if (p < fe_hand_end)
                        break;

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
            if (INV_PIECE[p] == (BonaPiece)-1 || MIR_PIECE[p] == (BonaPiece)-1)
            {
                // 未初期化のままになっている。上のテーブルの初期化コードがおかしい。
                assert(false);
            }

#if 0
        // 評価関数のミラーをしても大丈夫であるかの事前検証
        // 値を書き込んだときにassertionがあるので、ミラーしてダメである場合、
        // そのassertに引っかかるはず。

        // 調べるべきではないBonaPiece。
        std::unordered_set<BonaPiece> s;

        s.insert(BONA_PIECE_ZERO);

        // さらに出現しない升の盤上の歩、香、桂も除外(Aperyはここにもゴミが入っている)
        for (Rank r = RANK_1; r <= RANK_2; ++r)
            for (File f = FILE_9; f <= FILE_1; ++f)
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

#if 1
        std::cout << "\nchecking writeKpp()..";

        for (auto sq : Squares)
        {
            std::cout << sq << ' ';

            for (BonaPiece p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (BonaPiece p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    if (!s.count(p1) && !s.count(p2))
                        writeKpp(sq, p1, p2, kpp[sq][p1][p2]);
        }
#endif
#endif
    }

    void saveEval(std::string dir_name)
    {
        {
            auto eval_dir = path((std::string)USI::Options["EvalSaveDir"], dir_name);

            mkdir(eval_dir);

            // KPP
            std::ofstream ofsKK(path(eval_dir, KPP_BIN), std::ios::binary);
            if (!ofsKK.write(reinterpret_cast<char*>(kpp), sizeof(kpp)))
                goto Error;

            std::cout << "save_eval() finished. folder = " << eval_dir << std::endl;

            return;
        }

    Error:;
        std::cout << "Error : save_eval() failed" << std::endl;
    }

} // namespace Eval

#endif
