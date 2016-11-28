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

#include "platform.h"
#include "move.h"
#include "score.h"
#include "search.h"

struct TTEntry
{
    Key     key()        const { return (Key  )  key32; }
    Move    move()       const { return (Move ) move32; }
    Score   score()      const { return (Score)score16; }
    Score   eval()       const { return (Score) eval16; }
    Depth   depth()      const { return (Depth)depth16; }
    Bound   bound()      const { return (Bound) bound8; }
    uint8_t generation() const { return    generation8; }

    // 書き込みはアトミックに行いたい。
    void save(Key k, Score s, Bound b, Depth d, Move m, Score ev, uint8_t g)
    {
        // 何も考えずに指し手を上書き(新しいデータのほうが価値があるので)
        if (m || (k >> 32) != key32)
            move32 = m;

        // このエントリーの現在の内容のほうが価値があるなら上書きしない。
        if ((k >> 32) != key32
            || d > depth16 - 4 * ONE_PLY 
            || b == BOUND_EXACT)
        {
            key32       = (uint32_t)(k >> 32);
            score16     = (int16_t)s;
            eval16      = (int16_t)ev;
            generation8 = (uint8_t)g;
            bound8      = (uint8_t)b;
            depth16     = (int16_t)d;
        }
    }

private:
    friend class TranspositionTable;

    // hash keyの上位32bit
    uint32_t key32;

    // 指し手
    uint32_t move32;

    // このnodeでの探索の結果スコア
    int16_t score16;

    // 評価関数の評価値
    int16_t eval16;

    // 残り深さ。ONE_PLYが1以外の場合もあり、中途半端なdepthも書き込みたいので16bitにしている。
    int16_t depth16;

    // 登録されたときの世代
    uint8_t generation8;

    // 評価値のタイプ
    uint8_t bound8;
};

class TranspositionTable 
{
    static const int CACHE_LINE_SIZE = 64;
    static const int CLUSTER_SIZE = 4;

    struct Cluster { TTEntry entry[CLUSTER_SIZE]; };
    
    static_assert(sizeof(Cluster) == CACHE_LINE_SIZE, "Cluster size incorrect");

public:
    ~TranspositionTable() { free(mem_); }
    void newSearch() { generation8_++; }
    uint8_t generation() const { return generation8_; }
    bool probe(const Key key, TTEntry* &ptt) const;
    int hashfull() const;
    void resize(size_t mb_size);
    void clear() { memset(table_, 0, cluster_count_ * sizeof(Cluster)); generation8_ = 0; }
    TTEntry* firstEntry(const Key key) const { return &table_[(size_t)key & (cluster_count_ - 1)].entry[0]; }

private:
    void* mem_;
    Cluster* table_;
    uint8_t generation8_;
    size_t cluster_count_;	
};

extern TranspositionTable TT;

