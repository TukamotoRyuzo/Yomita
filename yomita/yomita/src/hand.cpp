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

#include <string>
#include "hand.h"

// 持ち駒の状態を32bitにパックするために必要なシフト回数
const int Hand::HAND_SHIFT[] =
{
    BISHOP_SHIFT,
    ROOK_SHIFT,
    PAWN_SHIFT,
    LANCE_SHIFT,
    KNIGHT_SHIFT,
    SILVER_SHIFT,
    GOLD_SHIFT
};

// 持ち駒のあるなし、または枚数を取り出すためのマスク定数
const uint32_t Hand::HAND_MASK[] =
{
    BISHOP_MASK,
    ROOK_MASK,
    PAWN_MASK,
    LANCE_MASK,
    KNIGHT_MASK,
    SILVER_MASK,
    GOLD_MASK
};

// 持ち駒を使うとき、または駒を取ったときにこの値を足し引きする
const uint32_t Hand::HAND_INCREMENT[] =
{
    1 << BISHOP_SHIFT,
    1 << ROOK_SHIFT,
    1 << PAWN_SHIFT,
    1 << LANCE_SHIFT,
    1 << KNIGHT_SHIFT,
    1 << SILVER_SHIFT,
    1 << GOLD_SHIFT
};

#ifdef HELPER
std::ostream& operator << (std::ostream &os, const Hand& h)
{
    os << "持ち駒:";

    if (!h)
        os << "なし";
    else
        for (auto pt : HandPiece)
            if (h.exists(pt))
                os << pretty(pt) << h.count(pt);
    return os;
}
#endif