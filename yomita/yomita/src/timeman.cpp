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

#include "usi.h"
#include "timeman.h"

TimeManagement Time;

// 256手指せる程度に時間を使う。
void TimeManagement::init(LimitsType& limits, Turn t, int ply)
{
    if (limits.time[t] == 0 && limits.byoyomi)
        limits.move_time = limits.byoyomi - USI::Options["NetworkDelay"];

    int remain_ply = std::max(1, (256 - ply) / (limits.inc[t] ? 8 : 2));
    int remain_time = limits.time[t] + limits.byoyomi;
    optimum_time = limits.byoyomi ? limits.byoyomi : remain_time / remain_ply;
    int maxtime = (limits.inc[t] || limits.byoyomi) ? optimum_time * 10 : optimum_time * 3;
    maximum_time = std::min(maxtime, remain_time);
    optimum_time -= USI::Options["NetworkDelay"];
    maximum_time -= USI::Options["NetworkDelay"];
    start_time = limits.start_time;
}
