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
#include "piece.h"
#include "piecescore.h"
#include "bitboard.h"

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

std::string toUSI(Piece pc) 
{ 
    auto s = std::string(". B R P L N S G K +B+R+P+L+N+S+G+.b r p l n s g k +b+r+p+l+n+s+g+k").substr(pc * 2, 2); 

    if (s[1] == ' ')
        s.resize(1);

    return s;
}
