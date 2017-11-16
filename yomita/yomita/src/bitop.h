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

#include <vector>

#include "platform.h"

// C++11では、std::stack<StateInfo>がalignasを無視するために、代わりにstack相当のものを自作。
template <typename T> struct aligned_stack 
{
    void push(const T& t) 
    { 
#if defined HAVE_SSE2 || defined HAVE_SSE4
        auto ptr = (T*)_mm_malloc(sizeof(T), alignof(T)); 
#else
        auto ptr = new T;
#endif
        *ptr = t; 
        container.push_back(ptr); 
    }

    T& top() const { return **container.rbegin(); }
    void clear() 
    { 
        for (auto ptr : container)
#if defined HAVE_SSE2 || defined HAVE_SSE4
            _mm_free(ptr);
#else
            delete ptr;
#endif
        container.clear(); 
    }
    ~aligned_stack() { clear(); }
private:
    std::vector<T*> container;
};

// 64bitのうち、LSBの位置を返す。
inline int bsf64(const uint64_t mask)
{
    assert(mask != 0);
#if defined USE_BSF
    unsigned long index;
    _BitScanForward64(&index, mask);
    return index;
#else
    static const int BitTable[64] =
    {
        63, 30, 3, 32, 25, 41, 22, 33, 15, 50, 42, 13, 11, 53, 19, 34, 61, 29, 2,
        51, 21, 43, 45, 10, 18, 47, 1, 54, 9, 57, 0, 35, 62, 31, 40, 4, 49, 5, 52,
        26, 60, 6, 23, 44, 46, 27, 56, 16, 7, 39, 48, 24, 59, 14, 12, 55, 38, 28,
        58, 20, 37, 17, 36, 8
    };
    const uint64_t tmp = mask ^ (mask - 1);
    const uint32_t old = static_cast<uint32_t>((tmp & 0xffffffff) ^ (tmp >> 32));
    return BitTable[(old * 0x783a9b23) >> 26];
#endif
}

inline int bsr64(const uint64_t mask)
{
    assert(mask != 0);
#if defined USE_BSF
    unsigned long index;
    _BitScanReverse64(&index, mask);
    return index;
#else
    for (int i = 63; 0 <= i; --i)
    {
        if (mask >> i)
            return i;
    }
    return 0;
#endif
}

#if defined USE_POPCNT
#define popCount(v) (int)_mm_popcnt_u64(v)
#define popCount32(v) (int)_mm_popcnt_u32(v)
#else
    //64bitのうち、たっているビットの数を返す。(popcntの代わり)
    inline int popCount(uint64_t v) 
    {
        uint64_t count = (v & 0x5555555555555555ULL) + ((v >> 1) & 0x5555555555555555ULL);
        count = (count & 0x3333333333333333ULL) + ((count >> 2) & 0x3333333333333333ULL);
        count = (count & 0x0f0f0f0f0f0f0f0fULL) + ((count >> 4) & 0x0f0f0f0f0f0f0f0fULL);
        count = (count & 0x00ff00ff00ff00ffULL) + ((count >> 8) & 0x00ff00ff00ff00ffULL);
        count = (count & 0x0000ffff0000ffffULL) + ((count >> 16) & 0x0000ffff0000ffffULL);
        return (int)((count & 0x00000000ffffffffULL) + ((count >> 32) & 0x00000000ffffffffULL));
    }
#endif

#if defined HAVE_BMI2
#define pext(a, b) _pext_u64(a, b)
#define pext32(a, b) _pext_u32(a, b)
#else
    inline uint64_t pext(uint64_t src, uint64_t mask)
    {
        // 自前のpextで代用
        uint64_t dst = 0;

        for (int i = 1; mask; i += i, mask &= mask - 1)
            if (mask & -(int64_t)mask & src)
                dst |= i;

        return dst;
    }
#endif


