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
//#define USE_SGD_UPDATE
#define USE_ADA_GRAD_UPDATE
//#define USE_YANE_GRAD_UPDATE
//#define USE_ADAM_UPDATE

// 目的関数が勝率の差の二乗和
//#define LOSS_FUNCTION_IS_WINNING_PERCENTAGE

// 目的関数が交差エントロピー
#define LOSS_FUNCTION_IS_CROSS_ENTOROPY

// タイムスタンプの出力をこの回数に一回に抑制する。
#define GEN_SFENS_TIMESTAMP_OUTPUT_INTERVAL 1
//#define TEST_UNPACK_SFEN

// 浅い探索の値としてevaluate()を用いる
//#define USE_EVALUATE_FOR_SHALLOW_VALUE

// 学習時の浅い探索にqsearchを使うか。
#define USE_QSEARCH_FOR_SHALLOW_VALUE

// 何局面ごとに勾配を重みに反映させるか。
#define LEARN_MINI_BATCH_SIZE (1000 * 1000 * 1)

// mini batch何回ごとにrmseを計算するか。
#define LEARN_RMSE_OUTPUT_INTERVAL 10

// mini batch何回ごとに学習局面数を出力するか。
#define LEARN_TIMESTAMP_OUTPUT_INTERVAL 10

// evalフォルダの名前を変える周期。
#define EVAL_FILE_NAME_CHANGE_INTERVAL (1000 * 1000 * 1000)

// ゼロベクトルからの学習を行うか。
#define RESET_TO_ZERO_VECTOR

// KPP以外をリセットするか。
//#define RESET_TO_ZERO_EXCEPT_KPP

// qsearchで手に入ったpvが合法手を返しているかのテスト。
//#define TEST_LEGAL_LEAF

// 最大の値がついている重みを出力する。
#define DISPLAY_STATS_IN_UPDATE_WEIGHTS

// addGradとupdateWeightの同期を取るかどうか。評価項目が少ないときは絶対に定義するべき。
#define SYNC_UPDATE_WEIGHT

// 更新式に応じた文字列。(デバッグ用に出力する。)
#if defined(USE_SGD_UPDATE)
#define LEARN_UPDATE "SGD"
#elif defined(USE_ADA_GRAD_UPDATE)
#define LEARN_UPDATE "AdaGrad"
#elif defined(USE_YANE_GRAD_UPDATE)
#define LEARN_UPDATE "YaneGrad"
#elif defined(USE_ADAM_UPDATE)
#define LEARN_UPDATE "Adam"
#endif

#if defined(LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
#define LOSS_FUNCTION "WINNING_PERCENTAGE"
#elif defined(LOSS_FUNCTION_IS_CROSS_ENTOROPY)
#define LOSS_FUNCTION "CROSS_ENTOROPY"
#endif

  // 重み配列の精度
typedef double LearnFloatType;

namespace Eval
{
#if defined USE_EVAL_TURN
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

#elif defined USE_YANE_GRAD_UPDATE

        // YaneGradの更新式

        // gを勾配、wを評価関数パラメーター、ηを学習率、εを微小値とする。
        // ※　α = 0.99とかに設定しておき、v[i]が大きくなりすぎて他のパラメーターがあとから動いたときに、このv[i]が追随できないのを防ぐ。
        //  ※　また比較的大きなεを加算しておき、vが小さいときに変な方向に行くことを抑制する。
        // v = αv + g^2
        // w = w - ηg/sqrt(v+ε)

        // YaneGradのα値
        LearnFloatType Weight::alpha = 0.99f;

        // 学習率η
        static LearnFloatType eta;

        // 最初のほうの更新量を抑制する項を独自に追加しておく。
        static constexpr LearnFloatType epsilon = LearnFloatType(1.0f);

        // AdaGradのg2
        WeightValue g2;

#elif defined (USE_ADAM_UPDATE)

        // 普通のAdam
        // cf. http://qiita.com/skitaoka/items/e6afbe238cd69c899b2a

        //		const LearnFloatType alpha = 0.001f;

        // const double eta = 32.0/64.0;
        // と書くとなぜかeta == 0。コンパイラ最適化のバグか？defineで書く。
        // etaは学習率。FV_SCALE / 64

        static constexpr LearnFloatType beta = LearnFloatType(0.9);
        static constexpr LearnFloatType gamma = LearnFloatType(0.999);
        static constexpr LearnFloatType epsilon = LearnFloatType(10e-8);
        static LearnFloatType eta;
        //static constexpr LearnFloatType  eta = LearnFloatType(1.0);

        WeightValue v;
        WeightValue r;

        // これはupdate()呼び出し前に計算して代入されるものとする。
        // bt = pow(β,epoch) , rt = pow(γ,epoch)
        static double bt;
        static double rt;
#endif

#if defined USE_EVAL_TURN
        // 手番の学習率
        static LearnFloatType eta2;
#endif
        void addGrad(WeightValue delta);

        bool update(bool skip_update);
    };
} // namespace Eval
#endif