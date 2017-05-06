/*
ì«Ç›ëæÅiyomitaÅj, a USI shogi (Japanese chess) playing engine derived from
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

#ifdef EVAL_KPPTP
#include <iostream>
#define KK_BIN "KK_synthesized.bin"
#define KKP_BIN "KKP_synthesized.bin"
#define KPP_BIN "KPP_synthesized.bin"

namespace Eval
{
    // memory mapped fileóp
    struct SharedEval
    {
        Value(*kk_)[SQ_MAX][SQ_MAX];
        Value(*kpp_)[SQ_MAX][fe_end][fe_end];
        Value(*kkp_)[SQ_MAX][SQ_MAX][fe_end];

        void malloc()
        {
            const auto sizekk  = uint64_t(SQ_MAX) * uint64_t(SQ_MAX);
            const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
            const auto sizekkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end);

            kk_  = (Value(*)[SQ_MAX][SQ_MAX])        _aligned_malloc(sizeof(Value) * sizekk,  32);
            kpp_ = (Value(*)[SQ_MAX][fe_end][fe_end])_aligned_malloc(sizeof(Value) * sizekpp, 32);
            kkp_ = (Value(*)[SQ_MAX][SQ_MAX][fe_end])_aligned_malloc(sizeof(Value) * sizekkp, 32);
            
            if (kk_ == NULL)
                std::cout << "kk: memory alloc error\n";

            if (kpp_ == NULL)
                std::cout << "kpp: memory alloc error\n";

            if (kkp_ == NULL)
                std::cout << "kkp: memory alloc error\n";
        }
    };

    struct EvalTable
    {
        Value(*kk_)[SQ_MAX][SQ_MAX];
        Value(*kpp_)[SQ_MAX][fe_end][fe_end];
        Value(*kkp_)[SQ_MAX][SQ_MAX][fe_end];

        void set(SharedEval* se)
        {
            kk_ = se->kk_;
            kpp_ = se->kpp_;
            kkp_ = se->kkp_;
        }
    };

    extern EvalTable et;

} // namespace Eval
#endif