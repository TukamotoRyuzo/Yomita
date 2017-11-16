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

#pragma once

#include <unordered_map>

#include "move.h"
#include "board.h"

struct BookEntry {
    Move best, ponder;
    int score, depth;
    uint64_t count;
};

// insertionSort() や std::sort() で必要
inline bool operator < (const BookEntry& f, const BookEntry& s) { return f.count < s.count; }
inline bool operator > (const BookEntry& f, const BookEntry& s) { return f.count > s.count; }

// 定跡処理関係
struct MemoryBook
{
    // 定跡ファイルの読み込み、書き込み
    int read(const std::string filename);
    int write(const std::string filename);

    // bookに指し手を加える。
    void insert(const std::string sfen, const BookEntry m);

    // 局面のsfenをkeyとして定跡登録されていればmoveを返す。
    // 登録されていなければMOVE_NONEを返す。
    Move probe(const Board& b) const;

private:
    std::unordered_map<std::string, std::vector<BookEntry>> book;
};

extern MemoryBook Book;
