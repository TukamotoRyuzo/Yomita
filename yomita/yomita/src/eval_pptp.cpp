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

#include "eval_pptp.h"

#ifdef EVAL_PPTP

#include <fstream>
#include "usi.h"
#include "board.h"

#define pptp (*et.pptp_)

namespace Eval
{

    // PP
    EvalTable et;

    // 評価関数ファイルを読み込む
    void loadSub()
    {
        std::ifstream ifs(path((std::string)USI::Options["EvalDir"], PPTP_BIN), std::ios::binary);

        if (ifs)
        {
            ifs.read(reinterpret_cast<char*>(pptp), sizeof(pptp));
#ifdef LEARN
            evalLearnInit();
#endif
        }
        else
            std::cout << "\ninfo string open evaluation file failed.\n";

#if 0
        for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; p1++)
            for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; p2++)
            {
                int32_t pre_w[8];
                for (int i = 0; i < 8; i++)
                {
                    pre_w[i] = pptp[p1][p2].p[i * 2] | pptp[p1][p2].p[i * 2 + 1] << 16;
                }

                pptp[p1][p2].p[0] = pre_w[0];
                pptp[p1][p2].p[1] = pre_w[1];

                for (int i = 0; i < 14; i++)
                {
                    pptp[p1][p2].p[i + 2] = pre_w[(int)((double)i / 2.3) + 2];
                }
            }
#endif
#if 0
        PRNG rng(1000);
        for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; p1++)
            for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; p2++)
            {
                for (int i = 0; i < 16; i++)
                    pptp[p1][p2].p[i] = pptp[p2][p1].p[i] = rng.rand(1000)/* - 500*/;
            }
#endif
#if 0
        std::ifstream ifss(path("eval/ppt/25_2_282", "PP.bin"), std::ios::binary);
        int32_t(*ppt)[fe_end2][fe_end2][2];
        ppt = (int32_t(*)[fe_end2][fe_end2][2])new int32_t[fe_end2 * fe_end2 * 2];

        ifss.read(reinterpret_cast<char*>(*ppt), sizeof(*ppt));

        for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end2; p1++)
            for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end2; p2++)
            {
                    pptp[p1][p2].p[0] = (*ppt)[p1][p2][0];
                    pptp[p1][p2].p[1] = (*ppt)[p1][p2][1];
            }
#endif
    }

    int32_t EvalSum::calcScore(const Board& b)
    {
        assert(!b.state()->progress.isNone());
        const Turn t = b.turn();
        double progress = b.state()->progress.rate();

        // [0](先手玉KPP) + [1](後手玉KPP) + [2](KKとKKP) 
        int32_t score_board = p[0];

        // 手番側に与えられるボーナス
        int32_t score_turn = p[1+t];

        // 進行度ボーナス(手番と関係ないボーナス)
        int pro = int(progress * 5.0);
        pro = (pro < 0 ? 0 : pro > 4 ? 4 : pro);
        int32_t score_prog_f = p[3 + pro];

        // 進行度ボーナス(手番側に与えられるボーナス)
        int pro2 = int(progress * 4.0);
        pro2 = (pro2 < 0 ? 0 : pro2 > 3 ? 3 : pro2) * 2;
        int32_t score_prog_g = p[8 + pro2 + t];

        return (t == BLACK ? score_board + score_prog_f : -score_board - score_prog_f) + score_turn + score_prog_g;
    }

    // PPのスケール
    const int FV_SCALE = 32;

    // 駒割り以外の全計算
    Score computeEval(const Board& b)
    {
        assert(pptp != nullptr);

        // 先手から見た駒の位置だけわかればよい。
        auto list = b.evalList()->pieceListFb();

        EvalSum sum;
        sum.y[0] = sum.y[1] = _mm256_setzero_si256();
        
        for (int i = 0; i < PIECE_NO_NB; ++i)
            for (int j = 0; j < i; ++j)
            {
                //for (int k = 0; k < 16; k++)
                    //sum.p[k] = sum.p[k] + pptp[list[i]][list[j]].p[k];

                _mm256_store_si256(&sum.y[0], _mm256_add_epi32(sum.y[0], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[list[i]][list[j]].m, 0))));
                _mm256_store_si256(&sum.y[1], _mm256_add_epi32(sum.y[1], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[list[i]][list[j]].m, 1))));
            }

        b.state()->sum = sum;

        return Score(sum.calcScore(b) / FV_SCALE);
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
            _mm256_store_si256(&sum.y[0], _mm256_sub_epi32(sum.y[0], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[W0][list[i]].m, 0))));\
            _mm256_store_si256(&sum.y[1], _mm256_sub_epi32(sum.y[1], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[W0][list[i]].m, 1))));\
            _mm256_store_si256(&sum.y[0], _mm256_add_epi32(sum.y[0], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[W1][list[i]].m, 0))));\
            _mm256_store_si256(&sum.y[1], _mm256_add_epi32(sum.y[1], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[W1][list[i]].m, 1))));\
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

                _mm256_store_si256(&sum.y[0], _mm256_sub_epi32(sum.y[0], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[k0][m0].m, 0))));
                _mm256_store_si256(&sum.y[1], _mm256_sub_epi32(sum.y[1], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[k0][m0].m, 1))));
                _mm256_store_si256(&sum.y[0], _mm256_add_epi32(sum.y[0], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[k1][m1].m, 0))));
                _mm256_store_si256(&sum.y[1], _mm256_add_epi32(sum.y[1], _mm256_cvtepi16_epi32(_mm256_extracti128_si256(pptp[k1][m1].m, 1))));
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