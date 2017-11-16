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

#include "usi.h"
#include "thread.h"

ThreadPool Threads;

Thread::Thread()
{
    exit = false;
    max_ply = 0;
    idx = Threads.size();
    root_board.setThread(this);
#ifdef USE_EVAL
    // デフォルトではグローバルテーブルを参照するようにしておく。
    evaluater = &Eval::GlobalEvaluater;
#endif
    tt = &GlobalTT;

    // スレッドがidleLoop内でsleepするまでを正常に実行させる処理
    std::unique_lock<Mutex> lk(mutex);
    searching = true;

    // この操作を終えるとスレッドがidleLoopを実行し出す
    native_thread = std::thread(&Thread::idleLoop, this);

    // Thread::idleLoopでスタンバイOKになるまで待つ
    sleep_condition.wait(lk, [&] { return !searching; });
}

Thread::~Thread()
{
    mutex.lock();
    exit = true;
    sleep_condition.notify_one();
    mutex.unlock();
    native_thread.join();
}

void Thread::clear()
{
    if (tt != &GlobalTT)
        tt->clear();

    counter_moves.clear();
    history.clear();
    counter_move_history.clear();
    CounterMoveStats& cm = *counter_move_history.refer();
    auto t = cm.refer();
    std::fill(t, t + sizeof(cm) / sizeof(*t), -1);
}

void Thread::wait(std::atomic_bool& b)
{
    std::unique_lock<Mutex> lk(mutex);
    sleep_condition.wait(lk, [&] { return bool(b); });
}

void Thread::waitWhile(std::atomic_bool& b)
{
    std::unique_lock<Mutex> lk(mutex);
    sleep_condition.wait(lk, [&] { return !b; });
}

// このスレッドに大して探索を開始させるときはこれを呼び出す
void Thread::startSearching(bool resume) 
{ 
    std::unique_lock<Mutex> lk(mutex); 

    if (!resume)
        searching = true; 

    sleep_condition.notify_one();
}

void Thread::idleLoop()
{
    WinProcGroup::bindThisThread(idx);

    while (!exit)
    {
        std::unique_lock<Mutex> lk(mutex);

        searching = false;

        while (!searching && !exit)
        {
            // 準備ができたことを通知する。Thread()と、MainThread::search()で待機しているスレッドに対して行う
            sleep_condition.notify_one();
            sleep_condition.wait(lk);
        }

        lk.unlock();

        if (!exit)
            search();
    }
}

std::vector<Thread*>::iterator Slaves::begin() const { return Threads.begin() + 1; }
std::vector<Thread*>::iterator Slaves::end() const { return Threads.end(); }

void ThreadPool::init()
{
    push_back(new MainThread());
    readUsiOptions();
}

void ThreadPool::exit()
{
    while (size())
        delete back(), pop_back();
}

// ThreadPoolの中のmainスレッド、slaveスレッドが探索を開始するための初期化をする
void ThreadPool::startThinking(const Board& b, const LimitsType& limits)
{
    // メインスレッドの参加を待つ
    main()->join();

    stop_on_ponderhit = stop = false;
    USI::Limits = limits;
    Search::RootMoves root_moves;

    for (const auto& m : MoveList<LEGAL>(b))
        if (limits.search_moves.empty() || count(limits.search_moves.begin(), limits.search_moves.end(), m))
            root_moves.push_back(Search::RootMove(m));

    // slaveスレッドに探索開始局面を設定する
    for (auto th : Threads)
    {
        th->setPosition(Board(b, th));
        th->max_ply = 0;
        th->nodes = 0;
        th->root_depth = th->completed_depth = DEPTH_ZERO;
        th->root_moves = root_moves;
    }

    main()->startSearching();
}

uint64_t ThreadPool::nodeSearched() const
{
    uint64_t nodes = 0;

    for (auto* th : *this)
        nodes += th->nodes.load(std::memory_order_relaxed);

    return nodes;
}

void ThreadPool::readUsiOptions()
{
    size_t requested = USI::Options["Threads"];

    while (size() < requested)
        push_back(new Thread());

    while (size() > requested)
        delete back(), pop_back();
}

PRNG WorkerThread::prng(20161010);
Mutex WorkerThread::rand_mutex;