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

#pragma once

#include "move.h"
#include "board.h"
#include "search.h"

struct HistoryStats
{
    static const int Max = 1 << 28;

    int get(Turn t, Move m) const { return table[t][isDrop(m) ? toSq(m) : fromSq(m)][toSq(m)]; }
    void clear() { std::memset(table, 0, sizeof(table)); }
    void update(Turn t, Move m, int s)
    {
        // 324を超えると、ヒストリ値が一定の値で飽和しなくなる。
        const int D = 324;
        assert(abs(s) <= D);

        Square to = toSq(m);
        Square from = isDrop(m) ? to : fromSq(m);

        table[t][from][to] -= table[t][from][to] * abs(s) / D;
        table[t][from][to] += s * 32;
    }

private:
    Score table[TURN_MAX][SQ_MAX][SQ_MAX];
};

// Statsは指し手の統計を保存する。テンプレートパラメータに応じてクラスはhistoryとcountermoveを保存することができる。
// Historyのレコード(現在の探索中にどれくらいの頻度で異なるmoveが成功or失敗したか)は、
// 手のオーダリングの決定とreductionに利用される。
// countermoveは以前の手をやり込める手を格納する。
// entryは移動先と動かした駒を使って格納される。したがって異なる手が同じ移動先と駒種ならば
// 2つの手は同一とみなされる。
template<typename T>
struct Stats
{
    void clear() { std::memset(table, 0, sizeof(table)); }

    // drop, turn, piecetype, toを呼び出し側で計算するより隠蔽してMoveで渡してもらったほうが実装が楽。
    T* refer() { return (T*)table; }
    T* refer(const Move m)       { return &table[isDrop(m)][turnOf(m)][movedPieceTypeTo(m) - 1][toSq(m)]; }
    T  value(const Move m) const { return  table[isDrop(m)][turnOf(m)][movedPieceTypeTo(m) - 1][toSq(m)]; }
    void update(const Move m, Move sm)  {  table[isDrop(m)][turnOf(m)][movedPieceTypeTo(m) - 1][toSq(m)] = sm; }

    void update(const Move m, int s)
    {
        const int D = 936;
        assert(abs(s) <= D);
        T& t = *refer(m);
        t -= t * abs(s) / D;
        t += s * 32;
    }

private:

    // [is_drop][turn][piecetype][square]
    T table[2][TURN_MAX][PRO_SILVER][SQ_MAX];
};

// ある指し手に対する指し手を登録しておくためのもの
typedef Stats<Move> MoveStats;

// ある指し手に対する点数を保存しておくためのもの
typedef Stats<int> CounterMoveStats;

// ある指し手に対するカウンター手とスコアの表
typedef Stats<CounterMoveStats> CounterMoveHistoryStats;

// 指し手を生成していい順番にひとつずつ手を取り出してくれる。
// いきなり全部の指し手を生成するわけではないので効率がいい。
struct MovePicker
{
    // ProbCut用
    MovePicker(const Board&, Move, Score);

    // 静止探索から呼び出される時用。
    MovePicker(const Board&, Move, Depth, Move);

    // 通常探索から呼び出されるとき用。
    MovePicker(const Board&, Move, Depth, Search::Stack*);

    // 次の指し手をひとつ返す
    // 指し手が尽きればMOVE_NONEが返る。
    Move nextMove();
    int seeSign() const;
private:
#ifdef USE_BYTEBOARD
    void initReCaptures(Square rsq);
    void initQuietChecks();
#endif
    void scoreQuiets();
    void scoreEvasions();
    void scoreCaptures();

    MoveStack* begin() { return moves; }
    MoveStack* end() { assert(end_moves < moves + MAX_MOVES); return end_moves; }

    bool inRange(MoveStack* ms) const
    {
        return moves <= ms && ms <= moves + MAX_MOVES;
    }

    const Board& board;
    const Search::Stack *ss;
    Move counter_move;
    Depth depth;
    MoveStack killers[3];
    Score threshold;
    Square recapture_square;
    Move tt_move;
    int stage;

    MoveStack *cur, *end_moves, *end_bads;
    MoveStack moves[MAX_MOVES];
};
