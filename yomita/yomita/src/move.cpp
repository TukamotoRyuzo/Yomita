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
#include "move.h"

std::string toUSI(const Move m)
{
    // 持ち駒を打つとき、USIにこの文字列を送信する。
    static const std::string hand_to_usi_str[] = { "", "B*", "R*", "P*", "L*", "N*", "S*", "G*", };

    if (isNone(m))
        return "resign";

    if (isNull(m))
        return "0000";

    // 駒打ちなら
    // 打った駒の種類を大文字 + * + 打つ場所をUSI用に変換して返す。
    if (isDrop(m))
        return hand_to_usi_str[movedPieceType(m)] + ::toUSI(toSq(m));

    // 盤上の手なら、移動前 + 移動後 という形で表す。
    std::string usi = ::toUSI(fromSq(m)) + ::toUSI(toSq(m));

    if (isPromote(m)) 
        usi += "+";

    // これで変換完了！
    return usi;
}


std::string toCSA(const Move m)
{
    static const std::string piece_to_csa[] = { "", "KA", "HI", "FU", "KY", "KE", "GI", "KI", "OU", "UM", "RY", "TO", "NY", "NK", "NG" };

    if (isNone(m))
        return "resign";

    if (isNull(m))
        return "0000";

    if (isDrop(m))
        return "00" + ::toCSA(toSq(m)) + piece_to_csa[movedPieceType(m)];

    std::string csa = ::toCSA(fromSq(m)) + ::toCSA(toSq(m)) + piece_to_csa[movedPieceTypeTo(m)];

    return csa;
}

std::string pretty(const Move m)
{ 
    if (isNone(m))
        return "none";

    if (isNull(m))
        return "null";

    std::string ret = ::pretty(toSq(m)) + ::pretty(movedPieceType(m)) + (isDrop(m) ? "打" : isPromote(m) ? "成" : "");

    if (!isDrop(m))
        ret += "(" + fromPretty(fromSq(m)) + ")";

    return ret; 
}


std::ostream& operator << (std::ostream& os, const Move m) { os << toUSI(m); return os; }