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

#ifdef LEARN

#include "search.h"

// 重みの更新式。
// やねうら王より。
#define USE_SGD_UPDATE
// #define USE_ADA_GRAD_UPDATE

// 目的関数が勝率の差の二乗和
#define LOSS_FUNCTION_IS_WINNING_PERCENTAGE

// 目的関数が交差エントロピー
//#define LOSS_FUNCTION_IS_CROSS_ENTOROPY

// タイムスタンプの出力をこの回数に一回に抑制する。
#define GEN_SFENS_TIMESTAMP_OUTPUT_INTERVAL 1
//#define TEST_UNPACK_SFEN

// 浅い探索の値としてevaluate()を用いる
//#define USE_EVALUATE_FOR_SHALLOW_VALUE

// 学習時の浅い探索にqsearchを使うか。
#define USE_QSEARCH_FOR_SHALLOW_VALUE

// 何局面ごとに勾配を重みに反映させるか。
#define LEARN_MINI_BATCH_SIZE (1 * 1000 * 1)

// mini batch何回ごとにrmseを計算するか。
#define LEARN_RMSE_OUTPUT_INTERVAL 10000

// mini batch何回ごとに学習局面数を出力するか。
#define LEARN_TIMESTAMP_OUTPUT_INTERVAL 10000

// evalフォルダの名前を変える周期。
#define EVAL_FILE_NAME_CHANGE_INTERVAL (1000 * 1000 * 1000)

// ゼロベクトルからの学習を行うか。
//#define RESET_TO_ZERO_VECTOR

// KPP以外をリセットするか。
//#define RESET_TO_ZERO_EXCEPT_KPP

// qsearchで手に入ったpvが合法手を返しているかのテスト。
//#define TEST_LEGAL_LEAF

// 最大の値がついている重みを出力する。
//#define DISPLAY_STATS_IN_UPDATE_WEIGHTS

// addGradとupdateWeightの同期を取るかどうか。評価項目が少ないときは絶対に定義するべき。
#define SYNC_UPDATE_WEIGHT

// 更新式に応じた文字列。(デバッグ用に出力する。)
#if defined(USE_SGD_UPDATE)
#define LEARN_UPDATE "SGD"
#elif defined(USE_ADA_GRAD_UPDATE)
#define LEARN_UPDATE "AdaGrad"
#endif

#if defined(LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
#define LOSS_FUNCTION "WINNING_PERCENTAGE"
#elif defined(LOSS_FUNCTION_IS_CROSS_ENTOROPY)
#define LOSS_FUNCTION "CROSS_ENTOROPY"
#endif

#define WEIGHTTYPE_FLOAT

// 一度に学習させるパラメータの個数
const int PARAMS_CNT = 16;

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
#if defined EVAL_PPTP || defined EVAL_KPPTP
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
#elif defined USE_EVAL_TURN
    typedef std::array<LearnFloatType, 2> WeightValue;
#elif defined USE_EVAL_NO_TURN
    typedef LearnFloatType WeightValue;
#endif

    struct Weight
    {
        // w : 元のパラメータ値。
        // g : mini-batch一回分の勾配
        WeightValue w, g;

#if defined (USE_SGD_UPDATE)

        // SGDの更新式 : w = w - ηg
        // SGDの場合、勾配自動調整ではないので、損失関数に合わせて適宜調整する必要がある。
        static LearnFloatType eta;

        // この特徴の出現回数
        uint32_t count;

#elif defined (USE_ADA_GRAD_UPDATE)

        // AdaGradの更新式
        //   v = v + g^2
        // ※　vベクトルの各要素に対して、gの各要素の2乗を加算するの意味
        //   w = w - ηg/sqrt(v)
        // 学習率η = 0.01として勾配が一定な場合、1万回でη×199ぐらい。
        // cf. [AdaGradのすすめ](http://qiita.com/ak11/items/7f63a1198c345a138150)
        // 初回更新量はeta。そこから小さくなっていく。

        static LearnFloatType eta;

        // AdaGradのg2
        WeightValue g2;
#endif
        void addGrad(WeightValue& delta);
        bool update(bool skip_update);
    };
} // namespace Eval
#endif