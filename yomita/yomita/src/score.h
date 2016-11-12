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

#include <climits>
#include "enumoperator.h"
#include "platform.h"
#include "common.h"

// スコア(評価値)がどういう探索の結果得た値なのかを表現する。
enum Bound { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT = BOUND_UPPER | BOUND_LOWER, };

// 評価値
enum Score
{
    SCORE_ZERO = 0,
    SCORE_DRAW = -50,
    SCORE_MAX_EVALUATED = 30000,
    SCORE_NOT_EVALUATED = INT_MAX,
    SCORE_INFINITE = 32601,
    SCORE_NONE = 32602,
    SCORE_SUPERIOR = 30000,
    SCORE_INFERIOR = -30000,
    
    SCORE_MATE = 32000, 
    
    SCORE_MATE_IN_MAX_PLY = SCORE_MATE - MAX_PLY,
    SCORE_MATED_IN_MAX_PLY = -SCORE_MATE + MAX_PLY,

    SCORE_KNOWN_WIN = 10000,
    SCORE_MATE_LONG = 30002,
    SCORE_MATE_1PLY = 32599,
    SCORE_MATE_0PLY = 32600,
};

ENABLE_OPERATORS_ON(Score);

// ply手で詰ました時のスコア、詰まされた時のスコア。
inline Score mateIn(int ply)  { return  SCORE_MATE - ply; }
inline Score matedIn(int ply) { return -SCORE_MATE + ply; }
