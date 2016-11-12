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

#include "evaluate.h"

#ifdef USE_EVAL

#include <string>

namespace Eval
{
    ExtBonaPiece BP_BOARD_ID[PIECE_MAX] =
    {
        { BONA_PIECE_ZERO, BONA_PIECE_ZERO },
        { f_bishop, e_bishop },
        { f_rook, e_rook },
        { f_pawn, e_pawn },
        { f_lance, e_lance },
        { f_knight, e_knight },
        { f_silver, e_silver },
        { f_gold, e_gold },
        { f_king, e_king },
        { f_horse, e_horse }, // 馬
        { f_dragon, e_dragon }, // 龍
        { f_gold, e_gold }, // 成歩
        { f_gold, e_gold }, // 成香
        { f_gold, e_gold }, // 成桂
        { f_gold, e_gold }, // 成銀

        { BONA_PIECE_ZERO, BONA_PIECE_ZERO }, // 金の成りはない

        // 後手から見た場合。fとeが入れ替わる。
        { BONA_PIECE_ZERO, BONA_PIECE_ZERO },
        { e_bishop, f_bishop },
        { e_rook, f_rook },
        { e_pawn, f_pawn },
        { e_lance, f_lance },
        { e_knight, f_knight },
        { e_silver, f_silver },
        { e_gold, f_gold },
        { e_king, f_king },
        { e_horse, f_horse }, // 馬
        { e_dragon, f_dragon }, // 龍
        { e_gold, f_gold }, // 成歩
        { e_gold, f_gold }, // 成香
        { e_gold, f_gold }, // 成桂
        { e_gold, f_gold }, // 成銀

        //{ BONA_PIECE_ZERO, BONA_PIECE_ZERO }, // 金の成りはない
    };

    ExtBonaPiece BP_HAND_ID[TURN_MAX][KING] =
    {
        {
            { BONA_PIECE_ZERO, BONA_PIECE_ZERO },
            { f_hand_bishop, e_hand_bishop },
            { f_hand_rook, e_hand_rook },
            { f_hand_pawn, e_hand_pawn },
            { f_hand_lance, e_hand_lance },
            { f_hand_knight, e_hand_knight },
            { f_hand_silver, e_hand_silver },
            { f_hand_gold, e_hand_gold },
        },
        {
            { BONA_PIECE_ZERO, BONA_PIECE_ZERO },
            { e_hand_bishop, f_hand_bishop },
            { e_hand_rook, f_hand_rook },
            { e_hand_pawn, f_hand_pawn },
            { e_hand_lance, f_hand_lance },
            { e_hand_knight, f_hand_knight },
            { e_hand_silver, f_hand_silver },
            { e_hand_gold, f_hand_gold },
        },
    };

    // BonaPieceの内容を表示する。手駒ならH,盤上の駒なら升目。例) HP3 (3枚目の手駒の歩)
    std::ostream& operator << (std::ostream& os, BonaPiece bp)
    {
        if (bp < fe_hand_end)
        {
            auto c = BLACK;
            for (PieceType pc = BISHOP; pc < KING; ++pc)
            {
                auto diff = BP_HAND_ID[c][pc].fw - BP_HAND_ID[c][pc].fb;

                if (BP_HAND_ID[c][pc].fb <= bp && bp < BP_HAND_ID[c][pc].fw)
                {
                    os << "FH" << pretty(pc) << int(bp - BP_HAND_ID[c][pc].fb + 1); // ex.HP3
                    goto End;
                }
                else if (BP_HAND_ID[c][pc].fw <= bp && bp < BP_HAND_ID[c][pc].fw + diff)
                {
                    os << "EH" << pretty(pc) << int(bp - BP_HAND_ID[c][pc].fw + 1); // ex.HP3
                    goto End;
                }
            }
        }
        else 
        {
            for (auto pc = B_BISHOP; pc < PIECE_MAX; ++pc)
                if (BP_BOARD_ID[pc].fb <= bp && bp < BP_BOARD_ID[pc].fb + SQ_MAX)
                {
                    os << pretty(Square(bp - BP_BOARD_ID[pc].fb)) << toUSI(pc); // ex.32P
                    break;
                }
        }
    End:;
        return os;
    }
}

#endif