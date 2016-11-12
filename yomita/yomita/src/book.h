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

#include <unordered_map>
#include "move.h"
#include "board.h"

// 定跡処理関係
namespace Book
{
    // 局面における指し手(定跡を格納するのに用いる)
    struct BookPos
    {
        Move best_move; // この局面での指し手

        BookPos() : best_move(MOVE_NONE) {}
        BookPos(Move best) : best_move(best) {}
        bool operator == (const BookPos& rhs) const { return best_move == rhs.best_move; }
    };

    // メモリ上にある定跡ファイル
    // sfen文字列をkeyとして、局面の指し手へ変換。(重複した指し手は除外するものとする)
    typedef std::unordered_map<std::string, BookPos> MemoryBook;
    void makeBook(Board& b, std::string filename);
    void store(MemoryBook& memory_book, std::string book, std::string sfen, Move best_move);
    int readBook(const std::string& filename, MemoryBook& book);
    int writeBook(const std::string& filename, const MemoryBook& book);
    void insertBookPos(MemoryBook& book, const std::string sfen, const BookPos& bp);
}
