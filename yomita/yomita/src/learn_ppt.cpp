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

#if defined LEARN && defined EVAL_PPT

#include <fstream>
#include "eval_ppt.h"
#include "usi.h"

#define ppt (*et.ppt_)

namespace Eval
{
    // 絶対値を抑制するマクロ
#define SET_A_LIMIT_TO(X, MIN, MAX)  \
    X[0] = std::min(X[0], (MAX));    \
    X[0] = std::max(X[0], (MIN));    \
    X[1] = std::min(X[1], (MAX));    \
    X[1] = std::max(X[1], (MIN));

    // ppt配列の同じ重みが入るべき場所に同じ値を書き込む。
    void writePp(BonaPiece p1, BonaPiece p2, ValuePp value)
    {
        assert(ppt[p1][p2] == ppt[p2][p1]);
        ppt[p1][p2] = ppt[p2][p1] = value;
    }

    // 書き込む場所を少なくする目的で、一番若いアドレスを返す。
    uint64_t getPpIndex(BonaPiece p1, BonaPiece p2)
    {
        assert(isOK(p1) && isOK(p2));

        const auto q0 = &ppt[0][0];
        auto q1 = &ppt[p1][p2] - q0;
        auto q2 = &ppt[p2][p1] - q0;
        return std::min(q1, q2);
    }

    // 勾配をdelta分増やす。
    void Weight::addGrad(WeightValue delta)
    {
        g[0] += delta[0];
        g[1] += delta[1];
#ifdef USE_SGD_UPDATE
        count++;
        assert(abs(g[0] / count) <= 1);
#endif
    }

    // 勾配を重みに反映させる。
    bool Weight::update(bool skip_update)
    {
#ifdef USE_SGD_UPDATE
        if (g[0] == 0 && g[1] == 0)
            return false;

        if (count)
        {
            g[0] /= count;
            g[1] /= count;
            count = 0;
            w = WeightValue{ w[0] - eta * g[0], w[1] - eta2 * g[1] };
        }

#elif defined USE_ADA_GRAD_UPDATE
        if (g[0] == 0 && g[1] == 0)
            return false;

        // g2[i] += g * g;
        // w[i] -= η * g / sqrt(g2[i]);
        // g = 0 // mini-batchならこのタイミングで勾配配列クリア。

        g2 += WeightValue{ g[0] * g[0] , g[1] * g[1] };

        // 値が小さいうちはskipする
        if (abs(g2[0]) >= 0.1f && !skip_update)
            w = WeightValue{ w[0] - eta * g[0] / sqrt(g2[0]) ,w[1] - eta2 * g[1] / sqrt(g2[1]) };
#endif

        g = { 0, 0 };

        return !skip_update;
    }

    LearnFloatType Weight::eta;
    LearnFloatType Weight::eta2;

    Weight(*pp_w_)[fe_end2][fe_end2];

#define pp_w (*pp_w_)

    void initGrad()
    {
        if (pp_w_ == nullptr)
        {
            const auto size_pp = uint64_t(fe_end2) * uint64_t(fe_end2);
            pp_w_ = (Weight(*)[fe_end2][fe_end2])new Weight[size_pp];
            memset(pp_w_, 0, sizeof(Weight) * size_pp);

#ifdef RESET_TO_ZERO_VECTOR
            std::cout << "\n[RESET_TO_ZERO_VECTOR]";
            memset(pp_, 0, sizeof(ValuePp) * size_pp);
#else
            // 元の重みをコピー
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; ++p2)
                {
                    pp_w[p1][p2].w[0] = LearnFloatType(ppt[p1][p2][0]);
                    pp_w[p1][p2].w[1] = LearnFloatType(ppt[p1][p2][1]);
                }
#endif
        }
    }

    // 現在の局面で出現している特徴すべてに対して、勾配値を勾配配列に加算する。
    void addGrad(Board& b, Turn root_turn, double delta_grad)
    {
        // 手番を考慮しない値と手番を考慮する値
        auto f = (root_turn == BLACK) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);
        auto g = (root_turn == b.turn()) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);
        auto list = b.evalList()->pieceListFb();

        for (int i = 0; i < PIECE_NO_NB; ++i)
            for (int j = 0; j < i; ++j)
                ((Weight*)pp_w_)[getPpIndex(list[i], list[j])].addGrad(WeightValue{ f, g });
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
        // kkpの一番大きな値を表示させることで学習が進んでいるかのチェックに用いる。
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        WeightValue max_pp{ 0.0f, 0.0f };
#endif

        // 学習メソッドに応じた学習率設定
#ifdef USE_SGD_UPDATE
#if defined (LOSS_FUNCTION_IS_CROSS_ENTOROPY)
        Weight::eta = 3.2f;
#elif defined (LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
        Weight::eta = 32.0f;
#endif
#elif defined USE_ADA_GRAD_UPDATE
        Weight::eta = 5.0f / float(mini_batch_size / 1000000);
#endif
        
        Weight::eta2 = Weight::eta / 4;

        for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; ++p1)
            for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; ++p2)
            {
                auto& w = pp_w[p1][p2];

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                max_pp[0] = std::max(max_pp[0], abs(w.w[0]));
                max_pp[1] = std::max(max_pp[1], abs(w.w[1]));
#endif
                if (w.update(skip_update))
                {
                    // 絶対値を抑制する。
                    SET_A_LIMIT_TO(w.w, (LearnFloatType)(INT32_MIN / 2), (LearnFloatType)(INT32_MAX / 2));
                    writePp(p1, p2, ValuePp{ (int32_t)w.w[0], (int32_t)w.w[1] });
                }
            }

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        std::cout << "\nmax_pp = " << max_pp[0] << "," << max_pp[1] << std::endl;
#endif
    }

    // 学習のためのテーブルの初期化
    void evalLearnInit()
    {
        // 次元下げとかいらないんじゃね？
    }

    void saveEval(std::string dir_name)
    {
        {
            auto eval_dir = path((std::string)USI::Options["EvalSaveDir"], dir_name);

            mkdir(eval_dir);

            // KPP
            std::ofstream ofsKPP(path(eval_dir, PP_BIN), std::ios::binary);
            if (!ofsKPP.write(reinterpret_cast<char*>(ppt), sizeof(ppt)))
                goto Error;

            std::cout << "save_eval() finished. folder = " << eval_dir << std::endl;

            return;
        }

    Error:;
        std::cout << "Error : save_eval() failed" << std::endl;
    }

} // namespace Eval

#endif