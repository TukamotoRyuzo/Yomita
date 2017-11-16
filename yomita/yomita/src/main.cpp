﻿/*
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

#include "tt.h"
#include "usi.h"
#include "thread.h"

// bitboardのセッティング
extern void initTables();
extern void initByteboardTables();

int main(int argc, char** argv)
{
#ifdef USE_BITBOARD
    initTables();
#endif
#ifdef USE_BYTEBOARD
    initByteboardTables();
#endif
    Zobrist::init();    
    USI::Options.init();
    Threads.init();
    GlobalTT.resize(USI::Options["Hash"]);
    Search::init();
    USI::loop(argc, argv);
    Threads.exit();
    return 0;
}