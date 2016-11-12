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

#ifdef EVAL_KPPT

#define KK_BIN "KK_synthesized.bin"
#define KKP_BIN "KKP_synthesized.bin"
#define KPP_BIN "KPP_synthesized.bin"

namespace Eval
{
    typedef std::array<int32_t, 2> ValueKk;
    typedef std::array<int16_t, 2> ValueKpp;
    typedef std::array<int32_t, 2> ValueKkp;

    // memory mapped file用
    struct SharedEval
    {
        ValueKk kk_[SQ_MAX][SQ_MAX];
        ValueKpp kpp_[SQ_MAX][fe_end][fe_end];
        ValueKkp kkp_[SQ_MAX][SQ_MAX][fe_end];
    };

    struct EvalTable
    {
        ValueKk(*kk_)[SQ_MAX][SQ_MAX];
        ValueKpp(*kpp_)[SQ_MAX][fe_end][fe_end];
        ValueKkp(*kkp_)[SQ_MAX][SQ_MAX][fe_end];

        void set(SharedEval* se)
        {
            kk_ = &se->kk_;
            kpp_ = &se->kpp_;
            kkp_ = &se->kkp_;
        }
    };

    extern EvalTable et;

} // namespace Eval
#endif