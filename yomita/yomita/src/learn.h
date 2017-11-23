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
#include <cmath>

#include "config.h"
#include "search.h"

namespace Learn
{
    // シグモイド関数
    inline double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

    // シグモイド関数の導関数。
    inline double dsigmoid(double x) { return sigmoid(x) * (1.0 - sigmoid(x)); }
}

#ifdef LEARN

// 最大の値がついている重みを出力する。
//#define DISPLAY_STATS_IN_UPDATE_WEIGHTS

#define WEIGHTTYPE_FLOAT

// 一度に学習させるパラメータの個数
const int PARAMS_CNT = 8;

// 重み配列の精度
#ifdef WEIGHTTYPE_FLOAT
typedef float LearnFloatType;
const int M_CNT = PARAMS_CNT / 8;
#define _mm256_store_p(a,b) _mm256_store_ps((a), (b));
#define _mm256_add_p(a, b) _mm256_add_ps((a), (b))
#define _mm256_sub_p(a, b) _mm256_sub_ps((a), (b))
#define _mm256_mul_p(a, b) _mm256_mul_ps((a), (b))
#define _mm256_div_p(a, b) _mm256_div_ps((a), (b))
#define _mm256_set1_p(n) _mm256_set1_ps((n))
#define _mm256_setzero_p _mm256_setzero_ps
#else
typedef double LearnFloatType;
const int M_CNT = PARAMS_CNT / 4;
#define _mm256_store_p(a, b) _mm256_store_pd((a), (b))
#define _mm256_add_p(a, b) _mm256_add_pd((a), (b))
#define _mm256_sub_p(a, b) _mm256_sub_pd((a), (b))
#define _mm256_mul_p(a, b) _mm256_mul_pd((a), (b))
#define _mm256_div_p(a, b) _mm256_div_pd((a), (b))
#define _mm256_set1_p(n) _mm256_set1_pd((n))
#define _mm256_setzero_p _mm256_setzero_pd
#endif

#ifdef HAVE_BMI2
#define USE_AVX
#endif

namespace Eval
{
    void initGrad();
    void addGrad(Board& b, Turn root_turn, double delta_grad);
    void updateWeights(uint64_t epoch);

#if defined EVAL_PPTP
    struct WeightValue
    {
        WeightValue& operator = (WeightValue& w)
        {
#ifdef USE_AVX
            for (int i = 0; i < M_CNT; i++)
                _mm256_store_p((LearnFloatType*)&m[i], w.m[i]);
#else
            for (int i = 0; i < PARAMS_CNT; i++)
                p[i] = w.p[i];
#endif
            return *this;
        }

        WeightValue& operator += (WeightValue& w)
        {
#ifdef USE_AVX
            for (int i = 0; i < M_CNT; i++)
                _mm256_store_p((LearnFloatType*)&m[i], _mm256_add_p(w.m[i], m[i]));
#else
            for (int i = 0; i < PARAMS_CNT; i++)
                p[i] += w.p[i];
#endif
            return *this;
        }

        WeightValue& operator -= (WeightValue& w)
        {
#ifdef USE_AVX
            for (int i = 0; i < M_CNT; i++)
                _mm256_store_p((LearnFloatType*)&m[i], _mm256_sub_p(m[i], w.m[i]));
#else
            for (int i = 0; i < PARAMS_CNT; i++)
                p[i] -= w.p[i];
#endif
            return *this;
        }

        WeightValue& operator *= (LearnFloatType n)
        {
#ifdef USE_AVX
            for (int i = 0; i < M_CNT; i++)
                _mm256_store_p((LearnFloatType*)&m[i], _mm256_mul_p(m[i], _mm256_set1_p(n)));
#else
            for (int i = 0; i < PARAMS_CNT; i++)
                p[i] *= n;
#endif
            return *this;
        }

        WeightValue& operator /= (LearnFloatType n)
        {
#ifdef USE_AVX
            for (int i = 0; i < M_CNT; i++)
                _mm256_store_p((LearnFloatType*)&m[i], _mm256_div_p(m[i], _mm256_set1_p(n)));
#else
            for (int i = 0; i < PARAMS_CNT; i++)
                p[i] /= n;
#endif
            return *this;
        }

        operator bool() const
        {
            // 実数ymmに対して非ゼロを判定するうまい命令が見当たらない……
            for (int i = 0; i < PARAMS_CNT; i++)
                if (p[i])
                    return true;

            return false;
        };

        union
        {
            std::array<LearnFloatType, PARAMS_CNT> p;
#ifdef WEIGHTTYPE_FLOAT
            __m256 m[M_CNT];
#else
            __m256d m[M_CNT];
#endif
        };
    };
#else
    typedef std::array<LearnFloatType, 2> WeightValue;
#endif
    struct Weight
    {
        // w : 元のパラメータ値。
        // g : mini-batch一回分の勾配
        WeightValue w, g;

        // 学習率
        static LearnFloatType eta;
        static int skip_count;

        WeightValue g2;

        void addGrad(WeightValue& delta);
        bool update(bool skip_update);
    };

} // namespace Eval
#endif
