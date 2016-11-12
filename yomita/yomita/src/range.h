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

// range based forのためのheader
// constexpr Range<Square, ZERO, SQ_MAX> Squares{};
// のように書けば、for(auto sq : Squares)とかけるようになる
template<typename T, int, int> class Range {};

template<typename T>
class Iter
{
    T value;
public:
    constexpr Iter(T val) : value(val) {};
    constexpr T operator *() const { return value; }
    Iter operator ++ (int) { value = static_cast<T>(value + 1); return Iter(T(value - 1)); }
    Iter& operator ++ () { value = static_cast<T>(value + 1); return *this; }
    constexpr bool operator == (const Iter& itr) const { return value == itr.value; }
    constexpr bool operator != (const Iter& itr) const { return value != itr.value; }
};

template<typename T, int b, int e>
constexpr Iter<T> begin(const Range<T, b, e>&) { return Iter<T>(T(b)); }

template<typename T, int b, int e>
constexpr Iter<T> end(const Range<T, b, e>&) { return Iter<T>(T(e)); }

