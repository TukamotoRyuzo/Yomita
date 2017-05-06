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

// 汎用的に使いそうな関数を定義する。

#include <chrono>
#include <vector>
#include <thread>
#include "platform.h"

#ifdef IS_64BIT
#define ALIGNAS(a) alignas(a)
#else
#define ALIGNAS(a)
#endif
// 64bit変数に対するforeach
#define FOR64(mask, sq, id, xxx) \
do{\
while(mask){\
sq = firstOne<id, true>(mask);\
xxx;\
}\
}while(false)

// bitboardに対するforeach
#define FORBB(bb, sq, xxx) \
do{\
uint64_t hi = bb.b(HIGH);\
while(hi){\
sq = firstOne<HIGH>(hi);\
xxx;\
}\
uint64_t lo = bb.b(LOW) & EXCEPT_MASK[LOW];\
while(lo){\
sq = firstOne<LOW>(lo);\
xxx;\
}\
}while(false)

// bitboardのb[0]とb[1]のかぶっていないビットを取り出すマスク
const uint64_t EXCEPT_MASK[2] = { 0x3ffffULL, 0x7fffe00000000000ULL };

// --- N回ループを展開するためのマクロ
// AperyのUROLのtemplateによる実装は模範的なコードなのだが、lambdaで書くと最適化されないケースがあったのでマクロで書く。
#define UROL1(Statement_) { const int i = 0; Statement_; }
#define UROL2(Statement_) { UROL1(Statement_); const int i = 1; Statement_; }
#define UROL3(Statement_) { UROL2(Statement_); const int i = 2; Statement_; }
#define UROL4(Statement_) { UROL3(Statement_); const int i = 3; Statement_; }
#define UROL5(Statement_) { UROL4(Statement_); const int i = 4; Statement_; }
#define UROL6(Statement_) { UROL5(Statement_); const int i = 5; Statement_; }
#define UROL7(Statement_) { UROL6(Statement_); const int i = 6; Statement_; }

// --- switchにおいてdefaultに到達しないことを明示して高速化させる
#ifdef _DEBUG
#define UNREACHABLE assert(false);
#elif defined SPEED_DEBUG
#define UNREACHABLE __assume(0);
#else
#define UNREACHABLE assert(false); __assume(0);
#endif

// ms単位での時間計測しか必要ないのでこれをTimePoint型のように扱う。
typedef std::chrono::milliseconds::rep TimePoint;

// ms単位で現在時刻を返す
inline TimePoint now() 
{
    return std::chrono::duration_cast<std::chrono::milliseconds>
        (std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 指定されたミリ秒だけsleepする。
inline void sleep(int ms)
{
    std::this_thread::sleep_for(std::chrono::microseconds(ms));
}

typedef uint64_t Key;

// 通常探索時の最大探索深さ
const int MAX_PLY = 128;

// 最大の合法手数
const int MAX_MOVES = 593 + 1;

// L1 / L2キャッシュ内の指定されたアドレスをプリロードする。
#if defined HAVE_SSE4 || defined HAVE_SSE2
inline void prefetch(void* addr) {
#if defined(__INTEL_COMPILER)
    // This hack prevents prefetches from being optimized away by
    // Intel compiler. Both MSVC and gcc seem not be affected by this.
    __asm__("");
#endif

#if defined(__INTEL_COMPILER) || defined(_MSC_VER)
    _mm_prefetch((char*)addr, _MM_HINT_T0);
#else
    __builtin_prefetch(addr);
#endif
}
#else
inline void prefetch(void*) {}
#endif

#ifdef HELPER
// cin/coutへの入出力をファイルにリダイレクトを開始/終了する。
extern void startLogger(bool b);
#endif
// ファイルを丸読みする。ファイルが存在しなくともエラーにはならない。空行はスキップする。
extern int readAllLines(std::string filename, std::vector<std::string>& lines);

// path名とファイル名を結合して、それを返す。
// folder名のほうは空文字列でないときに、末尾に'/'か'\\'がなければそれを付与する。
std::string path(const std::string& folder, const std::string& filename);

std::string localTime();

// 擬似乱数生成器
struct PRNG 
{
    PRNG(uint64_t seed) : s(seed) { assert(seed); }

    // thisアドレスを加味することでプロセスごとに異なった乱数を用いることができる。
    PRNG() : s(now() ^ uint64_t(this)) {}

    // T型の乱数を一つ生成
    template<typename T> T rand() { return T(rand64()); }

    uint64_t rand(size_t n) { return rand<uint64_t>() % n; }

private:
    uint64_t s;

    uint64_t rand64() 
    {
        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return s * 2685821657736338717LL;
    }
};

void mkdir(std::string dir);

namespace WinProcGroup
{
    void bindThisThread(size_t idx);
}