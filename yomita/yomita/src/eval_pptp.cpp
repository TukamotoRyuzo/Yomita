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

#ifdef EVAL_PPTP

#include <fstream>

#include "usi.h"
#include "board.h"
#include "learn.h"

#define PPTP_BIN "pptp.bin"

namespace Eval
{
    // computeAll, computeDiff, evaluateはpptと同じなので共通化。
    int32_t EvalSum::calcScore(const Board& b)
    {
        const Turn t = b.turn();
        int pro = int(Progress::evaluate(b) * 6.0);
        pro = (pro < 0 ? 0 : pro > 5 ? 5 : pro);
        int32_t score_board = p[0] + p[2 + pro];
        int32_t score_turn = p[1];
        return (t == BLACK ? score_board : -score_board) + score_turn;
    }

#if defined LEARN

    // 絶対値を抑制するマクロ
#define SET_A_LIMIT_TO(X, MIN, MAX)  \
for (int ss = 0; ss < 8; ss++){\
    X[ss] = std::min(X[ss], (MAX));    \
    X[ss] = std::max(X[ss], (MIN));    \
}
#define pptp (GlobalEvaluater->pp_)

    // pptp配列の同じ重みが入るべき場所に同じ値を書き込む。
    void writePp(BonaPiece p1, BonaPiece p2, ValuePp& value)
    {
        pptp[p1][p2] = pptp[p2][p1] = value;
    }

    // 書き込む場所を少なくする目的で、一番若いアドレスを返す。
    uint64_t getPpIndex(BonaPiece p1, BonaPiece p2)
    {
        assert(isOK(p1) && isOK(p2));

        const auto q0 = &pptp[0][0];
        auto q1 = &pptp[p1][p2] - q0;
        auto q2 = &pptp[p2][p1] - q0;
        return std::min(q1, q2);
    }

    // 勾配をdelta分増やす。
    void Weight::addGrad(WeightValue& delta)
    {
        g += delta;
    }

    // 勾配を重みに反映させる。
    bool Weight::update(bool skip_update)
    {
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

        for (int i = 0; i < 8; i++)
            func(i, eta);

        for (int i = 0; i < M_CNT; i++)
            g.m[i] = _mm256_setzero_p();

        return update;
    }

    Weight(*pptp_w_)[fe_end2][fe_end2];
#define pptp_w (*pptp_w_)

    void initGrad()
    {
        if (pptp_w_ == nullptr)
        {
            const auto size_pp = uint64_t(fe_end2) * uint64_t(fe_end2);
            pptp_w_ = (Weight(*)[fe_end2][fe_end2])_aligned_malloc(size_pp * sizeof(Weight), 32);
            memset(pptp_w_, 0, sizeof(Weight) * size_pp);
#ifdef RESET_TO_ZERO_VECTOR
            std::cout << "\n[RESET_TO_ZERO_VECTOR]";
            memset(pptp, 0, sizeof(ValuePp) * size_pp);
#else
            // 元の重みをコピー
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; ++p2)
                    for (int i = 0; i < 8; i++)
                        pptp_w[p1][p2].w.p[i] = pptp[p1][p2].p[i];
#endif  
        }
    }

    // 現在の局面で出現している特徴すべてに対して、勾配値を勾配配列に加算する。
    void addGrad(Board& b, Turn root_turn, double delta_grad)
    {
        // 手番を考慮しない値と手番を考慮する値
        auto f = (root_turn ==    BLACK) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);
        auto g = (root_turn == b.turn()) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);
        auto list = b.evalList()->pieceListFb();

#ifdef USE_AVX
        WeightValue w;

        for (int i = 0; i < M_CNT; i++)
            w.m[i] = _mm256_setzero_p();
#else
        WeightValue w = { 0 };
#endif
        double progress = Progress::evaluate(b);
        int pro = int(progress * 6.0);
        pro = (pro < 0 ? 0 : pro > 5 ? 5 : pro);
        
        w.p[0] = f;
        w.p[1] = g;
        w.p[2 + pro] = f;

        for (int i = 0; i < PIECE_NO_NB; ++i)
            for (int j = 0; j < i; ++j)
                ((Weight*)pptp_w_)[getPpIndex(list[i], list[j])].addGrad(w);
    }

    void updateWeights(uint64_t epoch)
    {
        const bool skip_update = epoch <= Eval::Weight::skip_count;

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        LearnFloatType max_pptp[8] = { 0.0 };
#endif

#if 1
        auto func = [&](size_t id)
        {
            for (auto p1 = int(fe_end2 / 10) * id; p1 < int(fe_end2 / 10) * (id + 1); ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; ++p2)
                {
                    auto& w = pptp_w[p1][p2];

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                    for (int i = 0; i < 8; i++)
                        max_pptp[i] = std::max(max_pptp[i], abs(w.w.p[i]));
#endif
                    if (w.update(skip_update))
                    {
                        // 絶対値を抑制する。
                        SET_A_LIMIT_TO(w.w.p, (LearnFloatType)(INT16_MIN), (LearnFloatType)(INT16_MAX));
                        ValuePp v;

                        for (int i = 0; i < 8; i++)
                            v.p[i] = std::round(w.w.p[i]);

                        writePp((BonaPiece)p1, p2, v);
                    }
                }
        };

        std::thread th[10];

        for (int i = 0; i < 10; i++)
            th[i] = std::thread(func, i);

        for (int i = 0; i < 10; i++)
            th[i].join();
#else
        for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; ++p1)
            for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; ++p2)
            {
                auto& w = pptp_w[p1][p2];

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                for (int i = 0; i < 16; i++)
                    max_pptp[i] = std::max(max_pptp[i], abs(w.w.p[i]));
#endif
                if (w.update(skip_update))
                {
                    // 絶対値を抑制する。
                    SET_A_LIMIT_TO(w.w.p, (LearnFloatType)(INT16_MIN), (LearnFloatType)(INT16_MAX));
                    ValuePp v;

                    for (int i = 0; i < 8; i++)
                        v.p[i] = w.w.p[i];

                    writePp(p1, p2, v);
                }
            }
#endif
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        std::cout << "\nmax_pp = ";

        for (int i = 0; i < 8; i++)
            std::cout << max_pptp[i] << ", ";

        std::cout << std::endl;
#endif
    }
#endif // LEARN
} // namespace Eval
#endif // EVAL_PPTP