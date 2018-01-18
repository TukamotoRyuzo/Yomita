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

#include "evaluate.h"

#if !defined USE_EVAL

#include "board.h"

    // この関数のために.cpp作るのもバカらしいのでここで定義する。
    namespace Eval
    {
        // 駒割だけ。手番から見た値を返す。
        Score evaluate(const Board& b) { return b.turn() == BLACK ? b.state()->material : -b.state()->material; }
    }
#else

#include <string>
#include <codecvt>
#include <fstream>
#include <iostream>
#ifdef _MSC_VER
#include <windows.h>
#endif

#include "usi.h"

namespace Eval
{
    // Grobalな評価関数テーブル
    Evaluater* GlobalEvaluater;

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

    // memory mapped fileに必要。
    void load()
    {
        const size_t size = sizeof(Evaluater);
        const std::string dir_name = USI::Options["EvalDir"];

#ifndef _MSC_VER
            GlobalEvaluater = new Evaluater;
            GlobalEvaluater->load(dir_name);
            SYNC_COUT << "info string use non-shared eval memory " << dir_name << SYNC_ENDL;
            return;
#else
        if (!(bool)USI::Options["EvalShare"])
        {
            GlobalEvaluater = new Evaluater;
            GlobalEvaluater->load(dir_name);
            SYNC_COUT << "info string use non-shared eval memory " << dir_name << SYNC_ENDL;
            return;
        }

        std::string wd = dir_name + USI::engineName();
        replace(wd.begin(), wd.end(), '\\', '_');
        replace(wd.begin(), wd.end(), '/', '_');
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cv;
        auto mapped = TEXT("YOMITA_KPPT_MMF") + cv.from_bytes(wd);
        auto mutex = TEXT("YOMITA_KPPT_MUTEX") + cv.from_bytes(wd);
        auto h_mutex = CreateMutex(NULL, FALSE, mutex.c_str());

        WaitForSingleObject(h_mutex, INFINITE);
        {
            auto h_map = CreateFileMapping(INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE,
                DWORD(size >> 32), DWORD(size & 0xffffffffULL),
                mapped.c_str());

            bool exists = GetLastError() == ERROR_ALREADY_EXISTS;
            GlobalEvaluater = (Evaluater*)MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, size);

            if (!exists)
            {
                GlobalEvaluater->load(dir_name);
                SYNC_COUT << "info string created shared eval memory " << dir_name << SYNC_ENDL;
            }
            else
                SYNC_COUT << "info string use shared eval memory " << dir_name << SYNC_ENDL;

            ReleaseMutex(h_mutex);
        }
#endif
    }
}

#endif
