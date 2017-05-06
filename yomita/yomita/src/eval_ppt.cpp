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

#include "eval_ppt.h"

#ifdef EVAL_PPT

#include <fstream>
#include "usi.h"
#include "board.h"

#define ppt (*et.ppt_)

namespace Eval
{
    // PP
    EvalTable et;

    // 評価関数ファイルを読み込む
    void loadSub()
    {
        std::ifstream ifs(path((std::string)USI::Options["EvalDir"], PP_BIN), std::ios::binary);
        
        if (ifs)
        {
            ifs.read(reinterpret_cast<char*>(ppt), sizeof(ppt));
#ifdef LEARN
            evalLearnInit();
#endif
        }
        else
            std::cout << "\ninfo string open evaluation file failed.\n";
    }

    // PPのスケール
    const int FV_SCALE = 32;

    // 駒割り以外の全計算
    Score computeEval(const Board& b)
    {
        //assert(pp_ != nullptr);

        // 先手から見た駒の位置だけわかればよい。
        auto list = b.evalList()->pieceListFb();

        EvalSum sum;
        sum.u = 0;

        for (int i = 0; i < PIECE_NO_NB; ++i)
            for (int j = 0; j < i; ++j)
                sum.p += ppt[list[i]][list[j]];

        b.state()->sum = sum;

        return Score(sum.calcScore(b.turn()) / FV_SCALE);
    }

    Score calcEvalDiff(const Board& b)
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
            return computeEval(b);

        // この差分を求める
        {
            int i;
            sum = prev->sum;
            auto list = b.evalList()->pieceListFb();
            auto& dp = now->dirty_piece;
            int k = dp.dirty_num;			// 移動させた駒は最大2つある。その数
            auto dirty = dp.piece_no[0];

#define ADD_PP(W0,W1) { \
          sum.p -= ppt[W0][list[i]]; \
          sum.p += ppt[W1][list[i]]; \
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

                sum.p -= ppt[k0][m0];
                sum.p += ppt[k1][m1];
            }
        }

        now->sum = sum;

        // 差分計算終わり
    CALC_DIFF_END:
        return (Score)(sum.calcScore(b.turn()) / FV_SCALE);
    }

    // 評価関数
    Score evaluate(const Board& b)
    {
        // 差分計算
        auto material = b.turn() == BLACK ? b.state()->material : -b.state()->material;
        auto score = calcEvalDiff(b) + material;

#if 0
        // 非差分計算
        auto sscore = computeEval(b) + material;

        if (score != sscore)
        {
            std::cout << score << " " << sscore;
        }
#endif
        assert(score == computeEval(b) + material);

        return score;
    }
}
#endif // EVAL_PPT