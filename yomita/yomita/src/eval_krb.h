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

#pragma once

#include "evaluate.h"

#ifdef EVAL_KRB

#include "evalsum.h"

#define KPP_BIN "kpp.bin"
#define RPP_BIN "rpp.bin"
#define BPP_BIN "bpp.bin"

namespace Eval
{
    // 全部16bitでいいや。
    typedef int16_t Value;

    // memory mapped file用
    struct SharedEval
    {
        Value kpp_[SQ_MAX][fe_end][fe_end];

        // 生飛車、角の場合はSQ_MAXまで。
        // 竜、馬の場合はSQ_MAX から 2 * SQ_MAXまで。
        // 持ち駒になっている場合は、SQ_MAX * 2。
        Value rpp_[SQ_MAX_PRO_HAND][fe_gold_end][fe_gold_end];
        Value bpp_[SQ_MAX_PRO_HAND][fe_gold_end][fe_gold_end];
    };

    struct EvalTable
    {
        Value(*kpp_)[SQ_MAX][fe_end][fe_end];
        Value(*rpp_)[SQ_MAX_PRO_HAND][fe_gold_end][fe_gold_end];
        Value(*bpp_)[SQ_MAX_PRO_HAND][fe_gold_end][fe_gold_end];

        void set(SharedEval* se)
        {
            kpp_ = &se->kpp_;
            rpp_ = &se->rpp_;
            bpp_ = &se->bpp_;
        }
    };

    extern EvalTable et;

} // namespace Eval
#endif