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

#include "config.h"

#ifdef EVAL_KPP

#include "platform.h"
#include "turn.h"
#include "score.h"

namespace Eval
{
    // 特に必要ではないが、評価関数の差分計算のinterfaceをそろえるために定義しておく。
    struct EvalSum
    {
        EvalSum() {}

        // その局面でevaluate()が呼ばれており評価値がセットされているかどうかを返す。
        bool isNotEvaluated() { return sumKKP == SCORE_NOT_EVALUATED; }

        // まだその局面でevaluate()が呼ばれていないことを表す値をセットしておく。
        void setNotEvaluated() { sumKKP = SCORE_NOT_EVALUATED; }

        int sumKKP;
        int sumBKPP;
        int sumWKPP;
    };

} // namespace Eval

#elif defined EVAL_KPPT

#include <array>
#include "platform.h"
#include "turn.h"

namespace Eval 
{
    template <typename Tl, typename Tr>
    inline std::array<Tl, 2> operator += (std::array<Tl, 2>& lhs, const std::array<Tr, 2>& rhs) 
    {
        lhs[0] += rhs[0];
        lhs[1] += rhs[1];
        return lhs;
    }
    template <typename Tl, typename Tr>
    inline std::array<Tl, 2> operator -= (std::array<Tl, 2>& lhs, const std::array<Tr, 2>& rhs) 
    {
        lhs[0] -= rhs[0];
        lhs[1] -= rhs[1];
        return lhs;
    }

    struct EvalSum 
    {
#if defined HAVE_BMI2
        EvalSum(const EvalSum& es) { _mm256_store_si256(&mm, es.mm); }
        EvalSum& operator = (const EvalSum& rhs) { _mm256_store_si256(&mm, rhs.mm); return *this; }
#elif defined HAVE_SSE4
        EvalSum(const EvalSum& es)
        {
            _mm_store_si128(&m[0], es.m[0]);
            _mm_store_si128(&m[1], es.m[1]);
        }
        EvalSum& operator = (const EvalSum& rhs) 
        {
            _mm_store_si128(&m[0], rhs.m[0]);
            _mm_store_si128(&m[1], rhs.m[1]);
            return *this;
        }
#endif
        EvalSum() {}

        int32_t sum(const Turn t) const 
        {
            // cf. http://www.computer-shogi.org/wcsc24/appeal/NineDayFever/NDF.txt

            // [0](先手玉) + [1](後手玉) + [2](KKとKKP) 
            const int32_t scoreBoard = p[0][0] - p[1][0] + p[2][0];
            const int32_t scoreTurn  = p[0][1] + p[1][1] + p[2][1];

            return (t == BLACK ? scoreBoard : -scoreBoard) + scoreTurn;
        }
        EvalSum& operator += (const EvalSum& rhs) 
        {
#if defined HAVE_BMI2
            mm = _mm256_add_epi32(mm, rhs.mm);
#else
            m[0] = _mm_add_epi32(m[0], rhs.m[0]);
            m[1] = _mm_add_epi32(m[1], rhs.m[1]);
#endif
            return *this;
        }
        EvalSum& operator -= (const EvalSum& rhs) 
        {
#ifdef HAVE_BMI2
            mm = _mm256_sub_epi32(mm, rhs.mm);
#else
            m[0] = _mm_sub_epi32(m[0], rhs.m[0]);
            m[1] = _mm_sub_epi32(m[1], rhs.m[1]);
#endif
            return *this;
        }
        EvalSum operator + (const EvalSum& rhs) const { return EvalSum(*this) += rhs; }
        EvalSum operator - (const EvalSum& rhs) const { return EvalSum(*this) -= rhs; }

        // その局面でevaluate()が呼ばれており評価値がセットされているかどうかを返す。
        bool isNotEvaluated() { return p[2][0] == SCORE_NOT_EVALUATED; }

        // まだその局面でevaluate()が呼ばれていないことを表す値をセットしておく。
        void setNotEvaluated() { p[2][0] = SCORE_NOT_EVALUATED; }

        union 
        {
            // p[0]はBKPP。p[0][0]が位置評価。p[0][1]はその局面で手番を握っている側のボーナス。
            // p[1]はWKPP
            // p[2]はKKPらしい。
            std::array<std::array<int32_t, 2>, 3> p;
            struct 
            {
                uint64_t data[3];
                uint64_t key; 
            };
#if defined HAVE_BMI2
            __m256i mm;
            __m128i m[2];
#else 
            __m128i m[2];
#endif
        };
    };

    static std::ostream& operator << (std::ostream& os, const EvalSum& sum)
    {
        os << "sum BKPP = " << sum.p[0][0] << " + " << sum.p[0][1] << std::endl;
        os << "sum WKPP = " << sum.p[1][0] << " + " << sum.p[1][1] << std::endl;
        os << "sum KK   = " << sum.p[2][0] << " + " << sum.p[2][1] << std::endl;
        return os;
    }

} // namespace Eval

#elif defined EVAL_PPT

#include <array>
#include "platform.h"
#include "turn.h"

namespace Eval
{
    template <typename Tl, typename Tr>
    inline std::array<Tl, 2> operator += (std::array<Tl, 2>& lhs, const std::array<Tr, 2>& rhs)
    {
        lhs[0] += rhs[0];
        lhs[1] += rhs[1];
        return lhs;
    }
    template <typename Tl, typename Tr>
    inline std::array<Tl, 2> operator -= (std::array<Tl, 2>& lhs, const std::array<Tr, 2>& rhs)
    {
        lhs[0] -= rhs[0];
        lhs[1] -= rhs[1];
        return lhs;
    }

    struct EvalSum
    {
        EvalSum() {}

        int32_t calcScore(const Turn t) { return (t == BLACK ? p[0] : -p[0]) + p[1]; }

        // その局面でevaluate()が呼ばれており評価値がセットされているかどうかを返す。
        bool isNotEvaluated() { return p[0] == SCORE_NOT_EVALUATED; }

        // まだその局面でevaluate()が呼ばれていないことを表す値をセットしておく。
        void setNotEvaluated() { p[0] = SCORE_NOT_EVALUATED; }

        union
        {
            std::array<int32_t, 2> p;
            uint64_t u;
        };
    };
} // namespace Eval

#endif