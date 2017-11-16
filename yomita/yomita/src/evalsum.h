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

#pragma once

#include "config.h"

#if defined EVAL_KPPT

#include <array>

#include "platform.h"

typedef std::array<int32_t, 2> ValueKk;
typedef std::array<int16_t, 2> ValueKpp;
typedef std::array<int32_t, 2> ValueKkp;

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
#elif defined HAVE_SSE2 || defined HAVE_SSE4
            m[0] = _mm_add_epi32(m[0], rhs.m[0]);
            m[1] = _mm_add_epi32(m[1], rhs.m[1]);
#else
            data[0] += rhs.data[0];
            data[1] += rhs.data[1];
            data[2] += rhs.data[2];
#endif
            return *this;
        }

        EvalSum& operator -= (const EvalSum& rhs) 
        {
#ifdef HAVE_BMI2
            mm = _mm256_sub_epi32(mm, rhs.mm);
#elif defined HAVE_SSE2 || defined HAVE_SSE4
            m[0] = _mm_sub_epi32(m[0], rhs.m[0]);
            m[1] = _mm_sub_epi32(m[1], rhs.m[1]);
#else
            data[0] -= rhs.data[0];
            data[1] -= rhs.data[1];
            data[2] -= rhs.data[2];
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

#if defined HAVE_SSE2 || defined HAVE_SSE4
#if defined HAVE_BMI2
            __m256i mm;
#endif
            __m128i m[2];
#endif
        };
    };
} // namespace Eval

#elif defined EVAL_PPT

#include <array>
#include "platform.h"

union ValuePp
{
    ValuePp& operator = (ValuePp& b) { u = b.u; return *this; }
    std::array<int32_t, 2> p;
    uint64_t u;
};

class Board;

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

        EvalSum& operator += (const ValuePp& rhs)
        {
            p[0] += rhs.p[0];
            p[1] += rhs.p[1];
           // u += rhs.u;
            //u = _mm_extract_epi64(_mm_add_epi32(_mm_cvtsi64_si128(u), _mm_cvtsi64_si128(rhs.u)), 0);
            return *this;
        }
        EvalSum& operator -= (const ValuePp& rhs)
        {
            p[0] -= rhs.p[0];
            p[1] -= rhs.p[1];
            //u -= rhs.u;
            //u = _mm_extract_epi64(_mm_sub_epi32(_mm_cvtsi64_si128(u), _mm_cvtsi64_si128(rhs.u)), 0);
            return *this;
        }

        int32_t calcScore(const Board& b);

        void clear() { u = 0; }

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

#elif defined EVAL_PPTP

#include <array>
#include "platform.h"

// [0] : scoreboard
// [1] : scoreturn
// [2]~[7] : scoreprogress
union ValuePp
{
    ValuePp& operator = (ValuePp& b) { _mm_store_si128(&this->m, b.m); return *this; }
    std::array<int16_t, 8> p;
    __m128i m;
};

class Board;

namespace Eval
{
    struct EvalSum
    {
        EvalSum() {}
#ifdef HAVE_BMI2
        EvalSum(const EvalSum& es) { _mm256_store_si256(&y, es.y); }
        EvalSum& operator = (const EvalSum& rhs) { _mm256_store_si256(&y, rhs.y); return *this; }
        EvalSum& operator += (const ValuePp& rhs) 
        { 
            _mm256_store_si256(&y, _mm256_add_epi32(y, _mm256_cvtepi16_epi32(rhs.m))); 
            return *this;
        }
        EvalSum& operator -= (const ValuePp& rhs)
        {
            _mm256_store_si256(&y, _mm256_sub_epi32(y, _mm256_cvtepi16_epi32(rhs.m)));
            return *this;
        }
        void clear() { y = _mm256_setzero_si256(); }

#elif defined HAVE_SSE4
        EvalSum& operator = (const EvalSum& rhs) 
        {
            _mm_store_si128(&m[0], rhs.m[0]);
            _mm_store_si128(&m[1], rhs.m[1]);
            return *this;
        }
        EvalSum& operator += (const ValuePp& rhs)
        {
            const __m128i izero = _mm_setzero_si128();
            __m128i words8hi = _mm_cmpgt_epi16(izero, rhs.m);
            _mm_store_si128(&m[0], _mm_add_epi32(m[0], _mm_unpacklo_epi16(rhs.m, words8hi)));
            _mm_store_si128(&m[1], _mm_add_epi32(m[1], _mm_unpackhi_epi16(rhs.m, words8hi)));
            return *this;
        }
        EvalSum& operator -= (const ValuePp& rhs)
        {
            const __m128i izero = _mm_setzero_si128();
            __m128i words8hi = _mm_cmpgt_epi16(izero, rhs.m);
            _mm_store_si128(&m[0], _mm_sub_epi32(m[0], _mm_unpacklo_epi16(rhs.m, words8hi)));
            _mm_store_si128(&m[1], _mm_sub_epi32(m[1], _mm_unpackhi_epi16(rhs.m, words8hi)));
            return *this;
        }
        void clear() { m[0] = m[1] = _mm_setzero_si128(); }
#else
        static_assert(false, "Your PC is too OLD!.");
#endif
        int32_t calcScore(const Board& b);

        // その局面でevaluate()が呼ばれており評価値がセットされているかどうかを返す。
        bool isNotEvaluated() { return p[0] == SCORE_NOT_EVALUATED; }

        // まだその局面でevaluate()が呼ばれていないことを表す値をセットしておく。
        void setNotEvaluated() { p[0] = SCORE_NOT_EVALUATED; }

        union
        {
            std::array<int32_t, 8> p;
#if defined HAVE_SSE2 || defined HAVE_SSE4
#if defined HAVE_BMI2
            __m256i y;
#endif
            __m128i m[2];
#endif
        };
    };
} // namespace Eval
#endif