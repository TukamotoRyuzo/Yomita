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

#include "timeman.h"
#include "usi.h"

using namespace USI;
TimeManagement Time;

namespace 
{
    enum TimeType { OptimumTime, MaxTime };

    const int MoveHorizon = 50;
    const double MaxRatio = 7.09; 
    const double StealRatio = 0.35; 

    // move_importance() is a skew-logistic function based on naive statistical
    // analysis of "how many games are still undecided after n half-moves". Game
    // is considered "undecided" as long as neither side has >275cp advantage.
    // Data was extracted from the CCRL game database with some simple filtering criteria.

    double move_importance(int ply)
    {
        const double XScale = 7.64;
        const double XShift = 58.4;
        const double Skew = 0.183;
        return pow((1 + exp((ply - XShift) / XScale)), -Skew) + DBL_MIN; // Ensure non-zero
    }

    // 残り時間を計算する関数
    template<TimeType T>
    int remaining(int myTime, int movesToGo, int ply, int slowMover)
    {
        const double TMaxRatio = (T == OptimumTime ? 1 : MaxRatio);
        const double TStealRatio = (T == OptimumTime ? 0 : StealRatio);

        double moveImportance = move_importance(ply) * slowMover / 100;
        double otherMovesImportance = 0;

        for (int i = 1; i < movesToGo; ++i)
            otherMovesImportance += move_importance(ply + 2 * i);

        double ratio1 = (TMaxRatio * moveImportance) / (TMaxRatio * moveImportance + otherMovesImportance);
        double ratio2 = (moveImportance + TStealRatio * otherMovesImportance) / (moveImportance + otherMovesImportance);

        return int(myTime * std::min(ratio1, ratio2)); // Intel C++ asks for an explicit cast
    }

} // namespace

void TimeManagement::init(LimitsType & limits, Turn t, int ply)
{
    // 指す前に少なくともこの時間は思考する。
    int minThinkingTime = Options["Minimum_Thinking_Time"];

    // 許される追加時間
    int moveOverhead	= Options["Move_Overhead"];

    // 序盤重視度
    int slowMover		= Options["Slow_Mover"];

    // 探索ノード数モード
    int npmsec			= Options["nodestime"];

    // nodes as timeモード
    if (npmsec)
    {
        if (!availableNodes) // Only once at game start
            availableNodes = npmsec * limits.time[t]; // Time is in msec

        // Convert from millisecs to nodes
        limits.time[t] = (int)availableNodes;
        limits.inc[t] *= npmsec;
    }

    start_time = limits.start_time;
    
    // 残り時間すべてか最小思考時間の、小さいほうで初期化
    optimum_time = maximum_time = std::max(limits.time[t], minThinkingTime); 

    const int MaxMTG = MoveHorizon;

    // We calculate optimum time usage for different hypothetical "moves to go"-values
    // and choose the minimum of calculated search time values. Usually the greatest
    // hypMTG gives the minimum values.
    for (int hypMTG = 1; hypMTG <= MaxMTG; ++hypMTG)
    {
        // Calculate thinking time for hypothetical "moves to go"-value
        int hypMyTime = limits.time[t]
            + limits.inc[t] * (hypMTG - 1)
            - moveOverhead * (2 + std::min(hypMTG, 40));

        hypMyTime = std::max(hypMyTime, 0);

        int t1 = minThinkingTime + remaining<OptimumTime>(hypMyTime, hypMTG, ply, slowMover);
        int t2 = minThinkingTime + remaining<MaxTime    >(hypMyTime, hypMTG, ply, slowMover);

        optimum_time = std::min(t1, optimum_time);
        maximum_time = std::min(t2, maximum_time);
    }

    if (Options["Ponder"])
        optimum_time += optimum_time / 4;

    // 秒読みルールなら少し追加
    if (limits.byoyomi 
        && limits.time[t] 
        && limits.byoyomi * 12 >= limits.time[t])
    {
        optimum_time += limits.byoyomi;
        maximum_time += limits.byoyomi;
    }			

    optimum_time -= Options["byoyomi_margin"];
    maximum_time -= Options["byoyomi_margin"];
}
