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

#define ENABLE_SAFE_OPERATORS_ON(T)\
    inline constexpr T  operator + (const T d1, const T d2) { return T(int(d1) + int(d2)); }\
    inline constexpr T  operator + (const T d1, const int d2) { return T(int(d1) + int(d2)); }\
    inline constexpr T  operator | (const T d1, const T d2) { return T(int(d1) | int(d2)); }\
    inline constexpr T  operator ^ (const T d1, const T d2) { return T(int(d1) ^ int(d2)); }\
    inline constexpr T  operator - (const T d1, const T d2) { return T(int(d1) - int(d2)); }\
    inline constexpr T  operator - (const T d1, const int d2) { return T(int(d1) - int(d2)); }\
    inline constexpr T  operator * (int i, const T d) { return T(i * int(d)); }\
    inline constexpr T  operator * (const T d, const T d2) { return T(int(d) * int(d2)); }\
    inline constexpr T  operator & (int i, const T d) { return T(i & int(d)); }\
    inline constexpr T  operator & (const T d1, const T d2) { return T(int(d1) & int(d2)); }\
    inline constexpr T  operator * (const T d, int i) { return T(int(d) * i); }\
    inline constexpr T  operator - (const T d) { return T(-int(d)); }\
    inline T& operator += (T& d1, const T d2) { return d1 = d1 + d2; }\
    inline T& operator -= (T& d1, const T d2) { return d1 = d1 - d2; }\
    inline T& operator += (T& d1, const int d2) { return d1 = d1 + d2; }\
    inline T& operator -= (T& d1, const int d2) { return d1 = d1 - d2; }\
    inline T& operator *= (T& d, int i) { return d = T(int(d) * i); }\
    inline T& operator ^= (T& d, int i) { return d = T(int(d) ^ i); }

#define ENABLE_OPERATORS_ON(T) ENABLE_SAFE_OPERATORS_ON(T)\
    inline T& operator ++ (T& d) { return d = T(int(d) + 1); }\
    inline T& operator -- (T& d) { return d = T(int(d) - 1); }\
    inline T  operator ++ (T& d,int) { T prev = d; d = T(int(d) + 1); return prev; } \
    inline T  operator -- (T& d,int) { T prev = d; d = T(int(d) - 1); return prev; } \
    inline constexpr T  operator / (const T d, int i) { return T(int(d) / i); }\
    inline T& operator /= (T& d, int i) { return d = T(int(d) / i); }
