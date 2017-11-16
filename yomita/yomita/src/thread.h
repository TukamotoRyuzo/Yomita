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

#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <condition_variable>

#include "tt.h"
#include "move.h"
#include "board.h"
#include "search.h"
#include "movepick.h"

const int MAX_THREADS = 64;

typedef std::mutex Mutex;
typedef std::condition_variable ConditionVariable;
struct LimitsType;

struct Thread 
{
    Thread();
    ~Thread();

    virtual void search();
    void idleLoop();

    static void* operator new (size_t s) { return Is64bit ? _mm_malloc(s, 32) : malloc(s); }
    static void operator delete (void* th) { Is64bit ? _mm_free(th) : free(th); }

    void clear();

    // bがtrueになるまで待つ
    void wait(std::atomic_bool& b);
    
    // bがtrueの間待つ
    void waitWhile(std::atomic_bool& b);

    // このスレッドに大して探索を開始させるときはこれを呼び出す
    void startSearching(bool resume = false);

    // メインスレッドか否か
    bool isMain() const { return idx == 0; }

    // このスレッドのsearchingフラグがfalseになるのを待つ(MainThreadがslaveの探索が終了するのを待機するのに使う)
    void join() { waitWhile(searching); }

    // 局面をセットする。root_boardを外部に公開したくない。(root_boardとthreadが相互参照になっていて気持ち悪いので。）
    void setPosition(const Board& b) { root_board = b; }

#ifdef USE_EVAL
    Eval::Evaluater** evaluater;
#endif
    TranspositionTable* tt;
    Search::RootMoves root_moves;
    Depth root_depth, completed_depth;
    size_t pv_idx;

    // 現在探索しているノードのうち、最大の探索深さを表す
    int max_ply;
    std::atomic<uint64_t> nodes;

    // ある指し手に対する指し手を保存しておく配列
    MoveStats counter_moves;

    // ある指し手に対するすべての指し手の点数を保存しておく配列
    CounterMoveHistoryStats counter_move_history;

    // fromからtoへの移動と手番に対する点数を保存しておく配列
    HistoryStats history;

    // pv
    Move pv[MAX_PLY][MAX_PLY + 1];

protected:
    Board root_board;
    std::thread native_thread;
    ConditionVariable sleep_condition;
    Mutex mutex;
    std::atomic_bool exit, searching;
    size_t idx;
};

struct MainThread : public Thread
{
    virtual void search();
    void checkTime();
    bool easy_move_played, failed_low;
    double best_move_changes;
    Score previous_score;
    int calls_cnt = 0;
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
    uint64_t nodeSearched() const;
    Slaves slaves;
    void readUsiOptions();
    template <typename T> void startWorkers(size_t thread_num);
    std::atomic_bool stop, stop_on_ponderhit;
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

// 学習や教師生成など、Threadクラスを用いて探索以外をさせたいときに用いるスレッド。
// search()をオーバーライドすればsearch内の処理をマルチスレッドで行える。
class WorkerThread : public Thread
{
    static PRNG prng;
    static Mutex rand_mutex;

public:
    // 乱数シードを時刻で初期化する。
    static void reseed() { prng = PRNG(); }

    static uint64_t rand(size_t n)
    {
        std::unique_lock<Mutex> lk(rand_mutex);
        return prng.rand<uint64_t>() % n;
    }
};

// WorkerThreadを派生させたスレッドを開始させる。
template <typename T>
inline void ThreadPool::startWorkers(size_t thread_num)
{
    stop = false;
    exit();

    for (int i = 0; i < thread_num; i++)
    {
        push_back(new T());
        back()->startSearching();
    }
}
