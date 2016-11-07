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

#ifdef USE_PROGRESS

#include "platform.h"
#include "evaluate.h"

#define PROGRESS_DIR "progress"
#define PROGRESS_BIN "progress.bin"
#define SAVE_PROGRESS_DIR "progress/save"

namespace Learn
{
    extern double sigmoid(double x);
}

namespace Prog
{
    // 進行度
    // KP値により、局面の進行度を取得する。
    // 進行度は32bitで表され、教師棋譜から開始局面を進行度0(0%)、終了局面を進行度1(=100%)として学習を行う。
    typedef int32_t ValueProg;

    extern ValueProg PROGRESS[SQ_MAX][Eval::fe_end];

    // 進行度ファイルの読み込み
    void load();

    // 進行度を全計算
    double computeProgress(const Board& b);

    // 進行度を差分計算
    double calcProgressDiff(const Board& b);

    struct ProgressSum
    {
        ProgressSum() {};

        void set(int b, int w) { bkp = b; wkp = w; }

        // 進行度を計算する。
        // 2^20は適当に決めた。
        double sum() const { return double(bkp + wkp) / double(1 << 20); }

        // まだ進行度の計算を済ませていないかどうかを返す。
        inline bool isNoProgress() const { return bkp == INT64_MAX; }

        // まだ進行度の計算を済ませていないことを示す値を入れておく。
        inline void setNoProgress() { bkp = INT64_MAX; }
        
        int64_t bkp, wkp;
    };
    
} // namespace Prog

#endif