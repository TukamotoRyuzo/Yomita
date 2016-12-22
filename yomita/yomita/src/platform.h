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

#include <cassert>

// MSVCコンパイラから出るうっとうしい警告を無効にする。
#pragma warning(disable: 4800) // 値の強制的なbool型への変換
#pragma warning(disable: 4267) // unsigned と signedの比較
#pragma warning(disable: 4838) // 'int' から 'const char' への変換
#pragma warning(disable: 4805) // '|': 演算中の 'int64_t' 型と 'const bool' 型の混用は安全ではありません

// MicroSoft Visual C++はinttypes.hをサポートしていない。サポートしているならここに書かれる内容はinttypes.hに任せればいい。
typedef   signed __int8    int8_t;
typedef unsigned __int8   uint8_t;
typedef   signed __int16  int16_t;
typedef unsigned __int16 uint16_t;
typedef   signed __int32  int32_t;
typedef unsigned __int32 uint32_t;
typedef   signed __int64  int64_t;
typedef unsigned __int64 uint64_t;

// Debug時に定義するとassertが無効になり、Release時に定義するとassertが有効になる。
//#define SPEED_DEBUG

#ifdef SPEED_DEBUG
#undef assert
#ifdef _DEBUG
#define assert(EXPRESSION)
#else
#define assert(EXPRESSION) if (!(EXPRESSION)) __debugbreak();
#endif
#endif

#define STATIC_ASSERT(x) static_assert(x, "static assertion error!")

// bsfq命令(_Bitscanforword)を使うか
#if defined(_WIN64) && !defined(IS_64BIT)
#include <intrin.h> // MSVC popcnt and bsfq instrinsics
#define IS_64BIT
#define USE_BSF
#endif

// uint64_tに対するpopcnt命令を使うか
#if defined(_MSC_VER) && defined(IS_64BIT)
#define USE_POPCNT
#define HAVE_SSE42
#define HAVE_SSE4
//#define HAVE_BMI2
#endif

#if !defined(IS_64BIT)
//#define HAVE_SSE2
#endif

#if defined (HAVE_BMI2)
#include <immintrin.h>
#endif

#if defined (HAVE_SSE42)
#include <nmmintrin.h>
#endif

#if defined (HAVE_SSE4)
#include <smmintrin.h>
#elif defined (HAVE_SSE2)
#include <emmintrin.h>
#endif

#define FORCE_INLINE __forceinline
