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

#include "evaluate.h"

#if defined EVAL_PPT || defined EVAL_PPTP

#include <fstream>

#include "usi.h"
#include "board.h"
#include "learn.h"

#if defined EVAL_PPT
const std::string PP_BIN = "PP.bin";
#else
const std::string PP_BIN = "pptp.bin";
#endif

namespace Eval
{
    // 評価関数ファイルを読み込む
    void Evaluater::load(std::string dir)
    {
        std::ifstream ifs(path(dir, PP_BIN), std::ios::binary);
        
        if (ifs)
            ifs.read(reinterpret_cast<char*>(pp_), sizeof(pp_));

        else
            std::cout << "\ninfo string open evaluation file failed.\n";

#if 0
        std::ifstream ifss(path("eval/ppt/epoch10", "PP.bin"), std::ios::binary);
        int32_t(*ppt)[fe_end2][fe_end2][2];
        ppt = (int32_t(*)[fe_end2][fe_end2][2])new int32_t[fe_end2 * fe_end2 * 2];

        ifss.read(reinterpret_cast<char*>(*ppt), sizeof(*ppt));

        memset(pp_, 0, sizeof(pp_));
        for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; p1++)
            for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; p2++)
            {
                if (abs((*ppt)[p1][p2][0]) >= 16384
                    || abs((*ppt)[p1][p2][1]) >= 16384)
                {
                    std::cout << "!" << std::endl;
                }
                pp_[p1][p2].p[0] = (*ppt)[p1][p2][0];
                pp_[p1][p2].p[1] = (*ppt)[p1][p2][1];
            }
        save("f");
#endif

    }

    // PPのスケール
    const int FV_SCALE = 32;

#if defined EVAL_PPT
    int32_t EvalSum::calcScore(const Board& b)
    {
        return (b.turn() == BLACK ? p[0] : -p[0]) + p[1]; 
    }
#endif
    // 駒割り以外の全計算
    Score computeAll(const Board& b)
    {
        const auto pp = (*b.thisThread()->evaluater)->pp_;

        // 先手から見た駒の位置だけわかればよい。
        auto list = b.evalList()->pieceListFb();

        EvalSum sum;
        sum.clear();

        for (int i = 0; i < PIECE_NO_NB; ++i)
            for (int j = 0; j < i; ++j)
                sum += pp[list[i]][list[j]];
        
        b.state()->sum = sum;

        return Score(sum.calcScore(b) / FV_SCALE);
    }

    Score computeDiff(const Board& b)
    {
        EvalSum sum;
        auto now = b.state();

        if (!now->sum.isNotEvaluated())
        {
            sum = now->sum;
            goto CALC_DIFF_END;
        }

        auto prev = now->previous;

        if (prev->sum.isNotEvaluated())
            return computeAll(b);

        const auto pp = (*b.thisThread()->evaluater)->pp_;

        // この差分を求める
        {
            int i;
            sum = prev->sum;
            auto list = b.evalList()->pieceListFb();
            auto& dp = now->dirty_piece;
            int k = dp.dirty_num;			// 移動させた駒は最大2つある。その数
            auto dirty = dp.piece_no[0];

#define ADD_PP(W0,W1) { \
          sum -= pp[W0][list[i]]; \
          sum += pp[W1][list[i]]; \
}
            if (k == 1) // 移動した駒が一つ。
            {
                auto k0 = dp.pre_piece[0].fb;
                auto k1 = dp.now_piece[0].fb;

                for (i = 0; i < dirty; ++i)
                    ADD_PP(k0, k1);
                for (++i; i < PIECE_NO_NB; ++i)
                    ADD_PP(k0, k1);
            }
            else if (k == 2) // 移動した駒が二つ。
            {
                PieceNo dirty2 = dp.piece_no[1];

                if (dirty > dirty2) 
                    std::swap(dirty, dirty2);

                auto k0 = dp.pre_piece[0].fb;
                auto k1 = dp.now_piece[0].fb;
                auto m0 = dp.pre_piece[1].fb;
                auto m1 = dp.now_piece[1].fb;

                // PP差分
                for (i = 0; i < dirty; ++i)
                {
                    ADD_PP(k0, k1);
                    ADD_PP(m0, m1);
                }
                for (++i; i < dirty2; ++i)
                {
                    ADD_PP(k0, k1);
                    ADD_PP(m0, m1);
                }
                for (++i; i < PIECE_NO_NB; ++i)
                {
                    ADD_PP(k0, k1);
                    ADD_PP(m0, m1);
                }

                sum -= pp[k0][m0];
                sum += pp[k1][m1];
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
#if 1
        // 差分計算
        auto material = b.turn() == BLACK ? b.state()->material : -b.state()->material;
        auto score = computeDiff(b) + material;
        assert(score == computeAll(b) + material);
#else
        auto score = computeDiff(b);
        score = std::max((Score)-9999, score);
        score = std::min((Score) 9999, score);
#endif
        return score;
    }
#if defined LEARN
    void Evaluater::save(std::string eval_dir, bool save_in_eval_dir)
    {
        auto dir = path(save_in_eval_dir ? evalDir() : evalSaveDir(), eval_dir);
        mkdir(dir);
        std::ofstream ofsPP(path(dir, PP_BIN), std::ios::binary);

        if (!ofsPP.write(reinterpret_cast<char*>(pp_), sizeof(pp_)))
            std::cout << "Error : save_eval() failed" << std::endl;
        else
            std::cout << "save_eval() finished. folder = " << dir << std::endl;

        if (save_in_eval_dir)
        {
            std::ofstream fs(path(evalDir(), "config.txt"), std::ios::trunc);
            fs << eval_dir << std::endl;
        }
    }
#endif
}
#endif // defined EVAL_PPT || defined EVAL_PPTP

#if defined EVAL_PPT && defined LEARN

namespace Eval
{
    // 絶対値を抑制するマクロ
#define SET_A_LIMIT_TO(X, MIN, MAX)  \
    X[0] = std::min(X[0], (MAX));    \
    X[0] = std::max(X[0], (MIN));    \
    X[1] = std::min(X[1], (MAX));    \
    X[1] = std::max(X[1], (MIN));

#define ppt (GlobalEvaluater->pp_)

    // ppt配列の同じ重みが入るべき場所に同じ値を書き込む。
    void writePp(BonaPiece p1, BonaPiece p2, ValuePp& value)
    {
        //assert(ppt[p1][p2] == ppt[p2][p1]);
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
    void Weight::addGrad(WeightValue& delta)
    {
        g[0] += delta[0];
        g[1] += delta[1];
    }

#define eta2 (eta / 4.0f)

    // 勾配を重みに反映させる。
    bool Weight::update(bool skip_update)
    {
        if (g[0] == 0 && g[1] == 0)
            return false;

        // g2[i] += g * g;
        // w[i] -= η * g / sqrt(g2[i]);
        // g = 0 // mini-batchならこのタイミングで勾配配列クリア。

        g2 += WeightValue{ g[0] * g[0] , g[1] * g[1] };

        // 値が小さいうちはskipする
        if (abs(g2[0]) >= 0.1f && !skip_update)
            w = WeightValue{ w[0] - eta * g[0] / sqrt(g2[0]) ,w[1] - eta2 * g[1] / sqrt(g2[1]) };

        g = { 0, 0 };

        return !skip_update;
    }

    Weight(*pp_w_)[fe_end2][fe_end2];

#define pp_w (*pp_w_)

    void loadGrad()
    {
        const auto size_pp = uint64_t(fe_end2) * uint64_t(fe_end2);
        memset(pp_w_, 0, sizeof(Weight) * size_pp);

        // 元の重みをコピー
        for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; ++p1)
            for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; ++p2)
            {
                pp_w[p1][p2].w[0] = LearnFloatType(ppt[p1][p2].p[0]);
                pp_w[p1][p2].w[1] = LearnFloatType(ppt[p1][p2].p[1]);
            }
    }

    void initGrad()
    {
        const auto size_pp = uint64_t(fe_end2) * uint64_t(fe_end2);
        if (pp_w_ == nullptr)
        {
            const auto size_pp = uint64_t(fe_end2) * uint64_t(fe_end2);
            pp_w_ = (Weight(*)[fe_end2][fe_end2])new Weight[size_pp];
        }
#ifdef RESET_TO_ZERO_VECTOR
        std::cout << "\n[RESET_TO_ZERO_VECTOR]";
        memset(ppt, 0, sizeof(ValuePp) * size_pp);
#endif
        loadGrad();
    }

    // 現在の局面で出現している特徴すべてに対して、勾配値を勾配配列に加算する。
    void addGrad(Board& b, Turn root_turn, double delta_grad)
    {
        // 手番を考慮しない値と手番を考慮する値
        auto f = (root_turn == BLACK   ) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);
        auto g = (root_turn == b.turn()) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);
        auto list = b.evalList()->pieceListFb();

        for (int i = 0; i < PIECE_NO_NB; ++i)
            for (int j = 0; j < i; ++j)
                ((Weight*)pp_w_)[getPpIndex(list[i], list[j])].addGrad(WeightValue{ f, g });
    }

    void updateWeights(uint64_t epoch)
    {
        const bool skip_update = epoch <= Eval::Weight::skip_count;

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        WeightValue max_pp{ 0.0f, 0.0f };
#endif
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
                    SET_A_LIMIT_TO(w.w, (LearnFloatType)(INT16_MIN / 2), (LearnFloatType)(INT16_MAX / 2));
                    writePp(p1, p2, ValuePp{ (int32_t)w.w[0], (int32_t)w.w[1] });
                }
            }

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        std::cout << "\nmax_pp = " << max_pp[0] << "," << max_pp[1] << std::endl;
#endif
    }
} // namespace Eval
#endif // defined EVAL_PPT && defined LEARN