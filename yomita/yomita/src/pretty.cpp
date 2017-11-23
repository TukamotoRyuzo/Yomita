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

#include <string>

#include "move.h"
#include "types.h"

const Score SCORE[] =
{
    SCORE_ZERO, BISHOP_SCORE, ROOK_SCORE, PAWN_SCORE, LANCE_SCORE, KNIGHT_SCORE, SILVER_SCORE, GOLD_SCORE,
    SCORE_ZERO, // king
    HORSE_SCORE, DRAGON_SCORE, PRO_PAWN_SCORE, PRO_LANCE_SCORE, PRO_KNIGHT_SCORE, PRO_SILVER_SCORE,
    SCORE_ZERO, SCORE_ZERO,
    -BISHOP_SCORE, -ROOK_SCORE, -PAWN_SCORE, -LANCE_SCORE, -KNIGHT_SCORE, -SILVER_SCORE, -GOLD_SCORE,
    SCORE_ZERO, // king
    -HORSE_SCORE, -DRAGON_SCORE, -PRO_PAWN_SCORE, -PRO_LANCE_SCORE, -PRO_KNIGHT_SCORE, -PRO_SILVER_SCORE, 
};

const Score CAPTURE_SCORE[] =
{
    SCORE_ZERO, CAPTURE_BISHOP_SCORE, CAPTURE_ROOK_SCORE, CAPTURE_PAWN_SCORE, CAPTURE_LANCE_SCORE, CAPTURE_KNIGHT_SCORE, CAPTURE_SILVER_SCORE, CAPTURE_GOLD_SCORE,
    SCORE_ZERO, // king
    CAPTURE_HORSE_SCORE, CAPTURE_DRAGON_SCORE, CAPTURE_PRO_PAWN_SCORE, CAPTURE_PRO_LANCE_SCORE, CAPTURE_PRO_KNIGHT_SCORE, CAPTURE_PRO_SILVER_SCORE,
    SCORE_ZERO, SCORE_ZERO,
    -CAPTURE_BISHOP_SCORE, -CAPTURE_ROOK_SCORE, -CAPTURE_PAWN_SCORE, -CAPTURE_LANCE_SCORE, -CAPTURE_KNIGHT_SCORE, -CAPTURE_SILVER_SCORE, -CAPTURE_GOLD_SCORE,
    SCORE_ZERO, // king
    -CAPTURE_HORSE_SCORE, -CAPTURE_DRAGON_SCORE, -CAPTURE_PRO_PAWN_SCORE, -CAPTURE_PRO_LANCE_SCORE, -CAPTURE_PRO_KNIGHT_SCORE, -CAPTURE_PRO_SILVER_SCORE,
};

const Score PROMOTE_SCORE[] =
{
    SCORE_ZERO, PROMOTE_BISHOP_SCORE, PROMOTE_ROOK_SCORE, PROMOTE_PAWN_SCORE, PROMOTE_LANCE_SCORE, PROMOTE_KNIGHT_SCORE, PROMOTE_SILVER_SCORE,
    SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO,
    -PROMOTE_BISHOP_SCORE, -PROMOTE_ROOK_SCORE, -PROMOTE_PAWN_SCORE, -PROMOTE_LANCE_SCORE, -PROMOTE_KNIGHT_SCORE, -PROMOTE_SILVER_SCORE,
    SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO, SCORE_ZERO
};

std::string toUSI(const Move m)
{
    // 持ち駒を打つとき、USIにこの文字列を送信する。
    const std::string hand_to_usi_str[] = { "", "B*", "R*", "P*", "L*", "N*", "S*", "G*", };

    if (m == MOVE_NONE)
        return "resign";

    if (m == MOVE_NULL)
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

std::string toUSI(Piece pc)
{
    auto s = std::string(". B R P L N S G K +B+R+P+L+N+S+G+.b r p l n s g k +b+r+p+l+n+s+g+k").substr(pc * 2, 2);

    if (s[1] == ' ')
        s.resize(1);

    return s;
}

std::string toUSI(const File f) { const char c[] = { (char)('9' - f), '\0' }; return std::string(c); }
std::string toUSI(const Rank r) { const char c[] = { (char)('a' + r), '\0' }; return std::string(c); }
std::string toUSI(const Square sq) { return toUSI(fileOf(sq)) + toUSI(rankOf(sq)); }

std::string pretty(const PieceType pt)
{
    assert(isOK(pt));
    const char* str[] = { "□", "角", "飛", "歩", "香", "桂", "銀", "金", "玉", "馬", "竜", "と", "杏", "圭", "全", "error", "error" };

    return str[pt];
}

std::ostream& operator << (std::ostream &os, const Piece p)
{
    const char* str[] = { "□", "角", "飛", "歩", "香", "桂", "銀", "金", "玉", "馬", "竜", "と", "杏", "圭", "全", "error", "error", 
        "^角", "^飛", "^歩", "^香", "^桂", "^銀", "^金", "^玉", "^馬", "^竜", "^と", "^杏", "^圭", "^全" };
    os << str[p];
    return os;
}

std::string toCSA(const Move m)
{
    const std::string piece_to_csa[] = { "", "KA", "HI", "FU", "KY", "KE", "GI", "KI", "OU", "UM", "RY", "TO", "NY", "NK", "NG" };

    if (m == MOVE_NONE)
        return "resign";

    if (m == MOVE_NULL)
        return "0000";

    if (isDrop(m))
        return "00" + ::toCSA(toSq(m)) + piece_to_csa[movedPieceType(m)];

    std::string csa = ::toCSA(fromSq(m)) + ::toCSA(toSq(m)) + piece_to_csa[movedPieceTypeTo(m)];

    return csa;
}

std::string pretty(const Move m)
{
    if (m == MOVE_NONE)
        return "none";

    if (m == MOVE_NULL)
        return "null";

    std::string ret = ::pretty(toSq(m)) + ::pretty(movedPieceType(m)) + (isDrop(m) ? "打" : isPromote(m) ? "成" : "");

    if (!isDrop(m))
        ret += "(" + fromPretty(fromSq(m)) + ")";

    return ret;
}


std::ostream& operator << (std::ostream& os, const Move m) { os << toUSI(m); return os; }

// 人がわかりやすい形に変換
std::string pretty(const File f)
{
    const char* str_file[] = { "９", "８", "７", "６", "５", "４", "３", "２", "１" };
    return std::string(str_file[f]);
}

std::string pretty(const Rank r)
{
    const char* str_rank[] = { "一", "二", "三", "四", "五", "六", "七", "八", "九" };
    return std::string(str_rank[r]);
}

std::string pretty(const Square sq) { return pretty(fileOf(sq)) + pretty(rankOf(sq)); }
std::string toCSA(const File f) { const char c[] = { (char)('9' - f), '\0' }; return std::string(c); }
std::string toCSA(const Rank r) { const char c[] = { (char)('1' + r), '\0' }; return std::string(c); }
std::string toCSA(const Square sq) { return toCSA(fileOf(sq)) + toCSA(rankOf(sq)); }
std::string fromPretty(const Square sq) { return toUSI(fileOf(sq)) + toUSI(inverse(File(rankOf(sq)))); }
std::ostream& operator << (std::ostream& os, const File f) { os << toUSI(f); return os; }
std::ostream& operator << (std::ostream& os, const Rank r) { os << toUSI(r); return os; }
std::ostream& operator << (std::ostream& os, const Square sq)
{
    Square d = sq;

    if (d >= SQ_MAX)
        d -= SQ_MAX;

    if (d == SQ_MAX)
        os << "Hand";
    else
        os << fileOf(d) << rankOf(d);

    return os;
}
