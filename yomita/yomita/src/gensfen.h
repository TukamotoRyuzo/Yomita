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

#if defined GENSFEN

#include "multi_think.h"
#include "sfen_rw.h"

namespace Learn
{
    // 複数スレッドでsfenを生成するためのクラス
    struct MultiThinkGenSfen : public MultiThink
    {
        MultiThinkGenSfen(int search_depth_, SfenWriter& sw_) : search_depth(search_depth_), sw(sw_)
        {
            // 乱数を時刻で初期化しないとまずい。
            // (同じ乱数列だと同じ棋譜が生成されかねないため)
            setPrng(PRNG());
        }

        virtual void work(size_t thread_id);
        void startFileWriteWorker() { sw.startFileWriteWorker(); }

        // 通常探索の探索深さ
        int search_depth;

        // 生成する局面の評価値の上限
        int eval_limit;

        // sfenの書き出し器
        SfenWriter& sw;
    };


} // namespace Learn

#endif