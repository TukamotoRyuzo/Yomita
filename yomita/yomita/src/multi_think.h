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

#include "config.h"

#if defined LEARN || defined GENSFEN

#include <functional>
#include "thread.h"

// マルチスレッドで何か行いたいときに使える便利なクラス。
struct MultiThink
{
	MultiThink() : prng(20160102) {}

	void think();

	virtual void work(size_t thread_id) = 0;

	std::function<void()> callback_func;
	int callback_seconds = 600;

	void setLoopMax(uint64_t max) { loop_max = max; }

	uint64_t getNextLoopCount()
	{
		std::unique_lock<Mutex> lk(loop_mutex);

		if (loop_count >= loop_max)
			return UINT64_MAX;

		return loop_count++;
	}

	Mutex io_mutex;

protected:

	template <typename T> T rand()
	{
		std::unique_lock<Mutex> lk(rand_mutex);
		return T(prng.rand64());
	}

	uint64_t rand(size_t n)
	{
		std::unique_lock<Mutex> lk(rand_mutex);
		return prng.rand<uint64_t>() % n;
	}

	void setPrng(PRNG r)
	{
		prng = r;
	}

private:

	volatile uint64_t loop_max, loop_count = 0;
	std::shared_ptr<volatile bool> thread_finished;

	Mutex loop_mutex;
	PRNG prng;
	Mutex rand_mutex;
};
#endif