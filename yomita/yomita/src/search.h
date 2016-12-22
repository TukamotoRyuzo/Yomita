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

#include <vector>
#include <memory>
#include <stack>
#include "move.h"
#include "board.h"

enum Depth : int16_t
{
    ONE_PLY = 1,

    DEPTH_ZERO = 0 * ONE_PLY,
    DEPTH_QS_CHECKS = 0 * ONE_PLY,
    DEPTH_QS_NO_CHECKS = -1 * ONE_PLY,
    DEPTH_QS_RECAPTURES = -5 * ONE_PLY,

    DEPTH_NONE = -6 * ONE_PLY,
    DEPTH_MAX = MAX_PLY * ONE_PLY,
};

static_assert(!(ONE_PLY & (ONE_PLY - 1)), "ONE_PLY is not a power of 2");

ENABLE_OPERATORS_ON(Depth);

typedef std::unique_ptr<aligned_stack<StateInfo>>  StateStackPtr;
template<typename T, bool CM> struct Stats;
typedef Stats<Score, true> CounterMoveStats;
struct TTEntry;

namespace Search
{
    struct Stack
    {
        Move* pv;
        int ply; // ルートからの深さ
        Move current_move;
        Move excluded_move;
        Move killers[2];
        Score static_eval; // 現局面で評価関数を呼び出した時のスコア
        Score history;
        int move_count;
        CounterMoveStats* counter_moves;

        Stack() {};
        Stack(const Stack& s)
        {
            killers[0] = s.killers[0];
            killers[1] = s.killers[1];
        }
    };

    struct RootMove
    {
        explicit RootMove(Move m) : pv(1, m) {}

        bool operator < (const RootMove& m) const { return m.score < score; } // Descending sort
        bool operator == (const Move& m) const { return pv[0] == m; }
        bool extractPonderFromTT(Board& b, Move weak_ponder);

        Score score = -SCORE_INFINITE;
        Score previous_score = -SCORE_INFINITE;

        // この局面での最善応手列
        std::vector<Move> pv;
    };

    typedef std::vector<RootMove> RootMoves;

    void init();
    void clear();

    extern StateStackPtr setup_status;
} // namespace Search

#if defined LEARN || defined GENSFEN
namespace Learn
{
    std::pair<Score, std::vector<Move>> qsearch(Board& b, Score alpha, Score beta);
    std::pair<Score, std::vector<Move>>  search(Board& b, Score alpha, Score beta, int depth);
} // namespace Learn
#endif