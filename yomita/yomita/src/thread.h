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

#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <vector>

#include "board.h"
#include "score.h"
#include "move.h"
#include "search.h"
#include "movepick.h"

const int MAX_THREADS = 64;

typedef std::mutex Mutex;
typedef std::condition_variable ConditionVariable;
struct LimitsType;

struct Thread 
{	
    Thread();
    virtual void search();
    void idleLoop();
    void terminate();

    // bがtrueになるまで待つ
    void wait(std::atomic_bool& b);
    
    // bがtrueの間待つ
    void waitWhile(std::atomic_bool& b);

    // このスレッドに大して探索を開始させるときはこれを呼び出す
    void startSearching(bool resume = false);

    // スレッドの番号
    size_t threadId() const { return idx; }

    // メインスレッドか否か
    bool isMain() const { return idx == 0; }

    // このスレッドのsearchingフラグがfalseになるのを待つ(MainThreadがslaveの探索が終了するのを待機するのに使う)
    void join() { waitWhile(searching); }

    Board root_board;
    Search::RootMoves root_moves;
    Depth root_depth, completed_depth;
    size_t pv_idx;

    // calls_count : この変数を++した回数でcheckTime()を呼び出すかどうかを判定する.
    // max_ply : 現在探索しているノードのうち、最大の探索深さを表す
    int max_ply, calls_count;

    // checkTime()を呼び出すのをやめるかどうかのフラグ
    std::atomic_bool reset_calls;

    // beta cutoffした指し手に加点して、それ以外のQuietな手には減点したもの
    HistoryStats history;

    // ある指し手に対する指し手を保存しておく配列
    MoveStats counter_moves;

    // ある指し手に対するすべての指し手の点数を保存しておく配列
    CounterMoveHistoryStats counter_move_history;

    // fromからtoへの移動と手番に対する点数を保存しておく配列
    FromToStats from_to_history;

    // pv
    Move pv[MAX_PLY][MAX_PLY + 1];

    // 学習で使うフラグ
#ifdef LEARN
    ConditionVariable cond;
    Mutex update_mutex;
    std::atomic_bool add_grading;
#endif

protected:
    std::thread native_thread;
    ConditionVariable sleep_condition;
    Mutex mutex;
    std::atomic_bool exit, searching;
    size_t idx;
};

struct MainThread : public Thread
{
    virtual void search();

    bool easy_move_played, failed_low;
    double best_move_changes;
    Score previous_score;
};

// MainThreadを除くループをまわすためのもの
struct Slaves 
{
    std::vector<Thread*>::iterator begin() const;
    std::vector<Thread*>::iterator end() const;
};

struct ThreadPool : public std::vector<Thread*>
{
    void init();
    void exit();
    MainThread* main() { return static_cast<MainThread*>(at(0)); }
    void startThinking(const Board& b, const LimitsType& limits);
    int64_t nodeSearched() { int64_t nodes = 0; for (auto* th : *this) nodes += th->root_board.nodeSearched(); return nodes; }
    Slaves slaves;
    void readUsiOptions();
};

extern ThreadPool Threads;
enum SyncCout { IO_LOCK, IO_UNLOCK };

inline std::ostream& operator << (std::ostream& os, SyncCout sc)
{
    static std::mutex m;
    if (sc == IO_LOCK) { m.lock(); }
    if (sc == IO_UNLOCK) { m.unlock(); }
    return os;
}

#define SYNC_COUT std::cout << IO_LOCK
#define SYNC_ENDL std::endl << IO_UNLOCK
