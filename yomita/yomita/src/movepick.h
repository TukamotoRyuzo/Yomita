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

#include "board.h"
#include "move.h"
#include "genmove.h"
#include "search.h"

// Statsは指し手の統計を保存する。テンプレートパラメータに応じてクラスはhistoryとcountermoveを保存することができる。
// Historyのレコード(現在の探索中にどれくらいの頻度で異なるmoveが成功or失敗したか)は、
// 手のオーダリングの決定とreductionに利用される。
// countermoveは以前の手をやり込める手を格納する。
// entryは移動先と動かした駒を使って格納される。したがって異なる手が同じ移動先と駒種ならば
// 2つの手は同一とみなされる。
template<typename T, bool CM = false>
struct Stats
{
    static const Score Max = Score(1 << 28);

    void clear() { std::memset(table, 0, sizeof(table)); }

    // drop, turn, piecetype, toを呼び出し側で計算するより隠蔽してMoveで渡してもらったほうが実装が楽。
    T& refer(const Move m) 
    { 
        bool d = isDrop(m);
        Turn t = turnOf(m);
        int p = movedPieceTypeTo(m) - 1;
        Square to = toSq(m);

        assert(!(d < 0 || d > 1 || t < 0 || t > 1 || p < 0 || p > 13 || to < 0 || to > 80));

        return table[d][t][p][to]; 
    }
    T  value(const Move m) const 
    {
        bool d = isDrop(m);
        Turn t = turnOf(m);
        int p = movedPieceTypeTo(m) - 1;
        Square to = toSq(m);

        assert(!(d < 0 || d > 1 || t < 0 || t > 1 || p < 0 || p > 13 || to < 0 || to > 80));

        return table[d][t][p][to];
    }
    void update(const Move i, Move m) 
    {
        bool d = isDrop(i);
        Turn t = turnOf(i);
        int p = movedPieceTypeTo(i) - 1;
        Square to = toSq(i);

        assert(!(d < 0 || d > 1 || t < 0 || t > 1 || p < 0 || p > 13 || to < 0 || to > 80));

        table[d][t][p][to] = m;
    }

    void update(const Move m, Score s)
    {
        if (abs(int(s)) >= 324)
            return;

        T& t = refer(m);
        t -= t * abs(int(s)) / (CM ? 936 : 324);
        t += int(s) * 32;
    }

private:

    // [is_drop][turn][piecetype][square]
    T table[2][TURN_MAX][PRO_SILVER][SQ_MAX];
};

// ある指し手に対する指し手を登録しておくためのもの
typedef Stats<Move> MoveStats;

// history。beta cutoffした指し手に加点して、それ以外のQuietな手には減点したもの。
typedef Stats<Score, false> HistoryStats;

// ある指し手に対する点数を保存しておくためのもの
typedef Stats<Score, true> CounterMoveStats;

// ある指し手に対するカウンター手とスコアの表
typedef Stats<CounterMoveStats> CounterMoveHistoryStats;

#if 0
const size_t t_size = sizeof(MoveStats) + sizeof(HistoryStats) + sizeof(CounterMoveStats) + sizeof(CounterMoveHistoryStats);
const int mbsize = t_size >> 20;
#endif

struct FromToStats
{
    Score get(Turn t, Move m) const { return table[t][isDrop(m) ? SQ_MAX : fromSq(m)][toSq(m)]; }
    void clear() { std::memset(table, 0, sizeof(table)); }
    void update(Turn t, Move m, Score s)
    {
        if (abs(int(s)) >= 324)
            return;

        Square from = isDrop(m) ? SQ_MAX : fromSq(m);
        Square to = toSq(m);

        table[t][from][to] -= table[t][from][to] * abs(int(s)) / 324;
        table[t][from][to] += int(s) * 32;
    }

private:
    Score table[TURN_MAX][SQ_MAX_PLUS1][SQ_MAX];
};
enum Stage
{
    // 置換表の指し手、駒を取る手、取らない手
    MAIN_SEARCH, GOOD_CAPTURES, KILLERS, GOOD_QUIETS, BAD_QUIETS, BAD_CAPTURES,

    // 回避
    EVASION, ALL_EVASIONS,

    // 駒を取る手と王手
    QSEARCH_WITH_CHECKS, QCAPTURES_1, CHECKS,

    // 駒を取る手
    QSEARCH_WITHOUT_CHECKS, QCAPTURES_2,

    // ProbCutフェーズ
    PROBCUT, PROBCUT_CAPTURES,

    // 取り返しの手
    RECAPTURE, PH_RECAPTURES,

    // これになったら指し手生成をストップ
    STOP,
};

ENABLE_OPERATORS_ON(Stage);

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
    void scoreCaptures();
    void scoreQuiets();
    void scoreEvasions();
    void nextStage();
    MoveStack* begin() { return moves; }
    MoveStack* end() { assert(end_moves < moves + MAX_MOVES); return end_moves; }

    const Board& board;
    const Search::Stack *ss;
    Move counter_move;
    Depth depth;
    MoveStack killers[3];
    Score threshold;
    Square recapture_square;
    Move tt_move;
    MoveStack *end_quiets, *end_bad_captures = moves + MAX_MOVES - 1;
    MoveStack moves[MAX_MOVES], *current = moves, *end_moves = moves;
    Stage stage;
};
