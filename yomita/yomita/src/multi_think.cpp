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

#include "multi_think.h"

#if defined LEARN || defined GENSFEN

#include "usi.h"

void MultiThink::think()
{
	// 評価関数の読み込み。
	USI::isReady();

	// ループの上限は自分でセット。
	this->loop_count = 0;
	std::vector<std::thread> threads;
	auto thread_num = (size_t)USI::Options["Threads"];

	thread_finished.reset(new volatile bool[thread_num]);

	// 関数をラムダで渡せば起動したことになるらしい。
	for (size_t i = 0; i < thread_num; i++)
	{
		thread_finished.get()[i] = false;
		threads.push_back(std::thread([i, this]
		{
			this->work(i);
			this->thread_finished.get()[i] = true;
		}));
	}

	// 何秒か置きにcallback_func()を呼び出す。
	// すべてのスレッドが終了しているかどうかもチェック。
	while (true)
	{
		const int check_interval = 5;

		for (int i = 0; i < callback_seconds / check_interval; i++)
		{
			// ここはメインスレッドなので眠ってもよし。
			// backgroundで動いているスレッドが動いてくれる。
			std::this_thread::sleep_for(std::chrono::seconds(check_interval));

			for (size_t s = 0; s < thread_num; s++)
			{
				// すべて終了していればFinishする。
				if (!thread_finished.get()[s])
					goto Next;
			}

			goto Finish;

		Next:;
		}

		// callback関数があれば呼び出す。
		if (callback_func)
			callback_func();
	}

Finish:

	if (callback_func)
	{
		std::cout << "\nfinalize..";
		callback_func();
	}

	for (size_t i = 0; i < thread_num; i++)
		threads[i].join();

	std::cout << "..all works done!" << std::endl;
}
#endif
