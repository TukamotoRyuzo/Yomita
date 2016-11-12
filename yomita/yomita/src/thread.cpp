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

#include "thread.h"
#include "usi.h"
#include "genmove.h"

ThreadPool Threads;

namespace 
{
    template<typename T> T* newThread() 
    {
        T* th = new (_mm_malloc(sizeof(T), alignof(T))) T();
        return (T*)th;
    }

    void deleteThread(Thread *th) 
    {
        th->terminate();
        _mm_free(th);
    }
}

Thread::Thread()
{
    // メンバ初期化
    reset_calls = exit = false;
    max_ply = calls_count = 0;
    history.clear();
    counter_moves.clear();
    idx = Threads.size();

    // スレッドがidleLoop内でsleepするまでを正常に実行させる処理
    std::unique_lock<Mutex> lk(mutex);
    searching = true;

    // この操作を終えるとスレッドがidleLoopを実行し出す
    native_thread = std::thread(&Thread::idleLoop, this);

    // Thread::idleLoopでスタンバイOKになるまで待つ
    sleep_condition.wait(lk, [&] { return !searching; });
}

// デストラクタの代わりを担う
void Thread::terminate()
{
    mutex.lock();
    exit = true;

    // exitをtrueにしたことを通知し、idleLoopを終了を待つ
    sleep_condition.notify_one();
    mutex.unlock();

    // 探索終了を待つ
    native_thread.join();
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
    push_back(newThread<MainThread>());
    readUsiOptions();
}

void ThreadPool::exit()
{
    while (size())
    {
        deleteThread(back());
        pop_back();
    }
}

// ThreadPoolの中のmainスレッド、slaveスレッドが探索を開始するための初期化をする
void ThreadPool::startThinking(const Board& b, const LimitsType& limits)
{
    // メインスレッドの参加を待つ
    main()->join();

    USI::Signals.stop_on_ponderhit = USI::Signals.stop = false;

    main()->root_moves.clear();
    main()->root_board = b;
    USI::Limits = limits;

    for (const auto& m : MoveList<LEGAL>(b))
        if (limits.search_moves.empty() || count(limits.search_moves.begin(), limits.search_moves.end(), m))
            main()->root_moves.push_back(Search::RootMove(m));

    main()->startSearching();
}

void ThreadPool::readUsiOptions()
{
    size_t requested = USI::Options["Threads"];

    while (size() < requested)
        push_back(newThread<Thread>());

    while (size() > requested)
    {
        deleteThread(back());
        pop_back();
    }
}