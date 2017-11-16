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

#include <string>
#include <fstream>

#include "config.h"
#include "thread.h"

namespace Learn
{
    // packされたsfen
    struct PackedSfenValue 
    { 
        uint8_t data[32]; 
        int16_t deep;
        bool win; 
        Move m; 
    };

    typedef std::vector<PackedSfenValue> SfenVector;

#ifdef LEARN
    // 非同期にファイルからread/writeするためのクラス
    struct AsyncSfenRW 
    {
        AsyncSfenRW() {};
        void terminate();

        // Readerとして使う場合
        void initReader(int thread_num, const std::vector<std::string>& files);
        void finalizeReader() { worker.join(); }
        void startReader() { worker = std::thread([&] { workReader(); }); }
        bool read(size_t thread_id, PackedSfenValue& ps);
        uint64_t readCount() const { return rw_count; }

        // Writerとして使う場合
        void initWriter(int thread_num, std::string filename);
        void finalizeWriter(size_t thread_id);
        void startWriter() { worker = std::thread([&] { workWriter(); }); }
        void write(size_t thread_id, const PackedSfenValue& p);

    private: 
        bool open();
        void workReader();
        void workWriter();

        static const uint64_t FILE_WRITE_INTERVAL = 5000;
        static const size_t THREAD_BUFFER_SIZE = 10 * 1000;
        static const size_t READ_SIZE = 1000 * 100 * 5;

        // タイムスタンプの出力用のカウンター
        uint64_t time_stamp_count = 0;
        uint64_t rw_count = 0;

        // ファイルストリーム
        std::fstream fs;

        // 非同期実行するthread
        std::thread worker;

        // 終了したかのフラグ
        std::atomic_bool finished;

        // poolにアクセスするときに必要なmutex
        Mutex mutex;

        std::vector<SfenVector*> buffers;
        std::vector<SfenVector*> buffers_pool;

        // Readするファイル名
        std::vector<std::string> filenames;

        PRNG prng;
    };
#endif
} // namespace Learn