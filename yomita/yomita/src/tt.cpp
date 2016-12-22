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

#include "tt.h"

TranspositionTable TT;

void TranspositionTable::resize(size_t mb_size)
{
    size_t new_cluster_count = size_t(1) << bsr64((mb_size * 1024 * 1024) / sizeof(Cluster));

    // 現在確保中の置換表用のメモリと等しいならば再確保は行わない
    if (new_cluster_count == cluster_count_)
        return;

    assert(new_cluster_count >= 1000 / CLUSTER_SIZE);

    cluster_count_ = new_cluster_count;

    free(mem_);

    mem_ = calloc(cluster_count_ * sizeof(Cluster) + CACHE_LINE_SIZE - 1, 1);

    if (!mem_)
    {
        std::cerr << "Failed to allocate " << mb_size << "MB for transposition table." << std::endl;
        exit(EXIT_FAILURE);
    }

    // mem_から64バイトでアラインされたアドレスを得て、それをtableとして使う。
    table_ = (Cluster*)((uintptr_t(mem_) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1));

}

// TTEのポインタ、見つからなかったらreplaceできるTTEのポインタがpttに代入される
bool TranspositionTable::probe(const Key key, TTEntry* &ptt, TTEntry* copy) const
{
    TTEntry* const tte = firstEntry(key);
    const uint32_t key32 = key >> 32;

    for (int i = 0; i < CLUSTER_SIZE; ++i)
    {
        // コピーしてから比較する。
        *copy = tte[i];

        if (!copy->key32 || copy->key32 == key32) // 空か、同じ局面が見つかった
        {
            if (copy->generation8 != generation8_ && copy->key32)
                tte[i].generation8 = generation8_; // Refresh

            // 空だったら見つかってないのでfalse ※Clusterは先頭から順番に埋まっていく
            ptt = &tte[i];
            return (bool)copy->key32;
        }
    }

    // 見つからなかったら、replaceできるポインタを返す。
    TTEntry* replace = tte;

    for (int i = 1; i < CLUSTER_SIZE; ++i)
        if (replace->depth16 > tte[i].depth16) // 一番残り深さの少ない局面をreplace候補とする
            replace = &tte[i];

    ptt = replace;
    return false;
}

// 1000個ほどサンプルを取り、今回の探索でどれほどハッシュ表を使ったかを返す。
int TranspositionTable::hashfull() const
{
    int cnt = 0;

    for (int i = 0; i < 1000 / CLUSTER_SIZE; i++)
    {
        const TTEntry* tte = &table_[i].entry[0];

        for (int j = 0; j < CLUSTER_SIZE; j++)
            if (tte[j].generation() == generation8_)
                cnt++;
    }

    return cnt;
}

