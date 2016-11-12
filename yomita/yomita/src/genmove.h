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

#include "board.h"
#include "move.h"

// TURNから見た敵陣を取り出すマスク
extern const uint64_t ENEMY_MASK[TURN_MAX];

// TURNから見た自陣を取り出すマスク
extern const uint64_t SELF_MASK[TURN_MAX];

// bitboardのb[0]とb[1]のかぶっていないビットを取り出すマスク
extern const uint64_t EXCEPT_MASK[ID_MAX];

// MoveType の全ての指し手を生成
template <MoveType MT> MoveStack* generate(MoveStack* mlist, const Board& b);
template <MoveType MT> MoveStack* generate(MoveStack* mlist, const Board& b, const Square to);

// MoveTypeに応じたMoveStackのリストを作るクラス
template <MoveType MT> class MoveList
{
public:
    explicit MoveList(const Board& b) : curr_(mlist_), last_(generate<MT>(mlist_, b)) {}
    void operator ++ () { ++curr_; }
    const MoveStack* begin() const { return mlist_; }
    const MoveStack* end() const { return last_; }
    Move move() const { return curr_->move; }

    // 生成した手の数を返す
    size_t size() const { return static_cast<size_t>(last_ - mlist_); }

    // 渡されたmoveがリストの中にあるかどうか
    bool contains(const Move move) const
    {
        for (const MoveStack* it(mlist_); it != last_; ++it)
            if (it->move == move)
                return true;

        return false;
    }

private:
    MoveStack mlist_[MAX_MOVES];
    MoveStack* curr_;
    MoveStack* last_;
};
