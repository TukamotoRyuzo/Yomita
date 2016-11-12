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
#include "thread.h"
#include <fstream>

namespace Learn
{
    // packされたsfen
    struct PackedSfenValue { uint8_t data[34]; };
    typedef std::shared_ptr<std::vector<PackedSfenValue>> SfensPointer;

#ifdef GENSFEN

    // Sfenを書き出して行くためのヘルパクラス
    struct SfenWriter
    {
        // 書き出すファイル名と生成するスレッドの数
        SfenWriter(std::string filename, int thread_num);
        ~SfenWriter();

        // この数ごとにファイルにflushする。
        const uint64_t FILE_WRITE_INTERVAL = 5000;

        // 局面と評価値をペアにして1つ書き出す(packされたsfen形式で)
        void write(size_t thread_id, uint8_t data[32], int16_t value);	

        // バッファに残っている分をファイルに書き出す。
        void finalize(size_t thread_id);

        // write_workerスレッドを開始する。
        void startFileWriteWorker();
        
        // ファイルに書き出すの専用スレッド
        void fileWriteWorker();

    private:
        // 書き込むファイルストリーム
        std::fstream fs;

        // ファイルに書き込む用のthread
        std::thread file_worker_thread;

        // 終了したかのフラグ。ワーカースレッドに伝える。
        volatile bool finished;

        // タイムスタンプの出力用のカウンター
        uint64_t time_stamp_count = 0;

        // sfen_buffers_poolにアクセスするときに必要なmutex
        Mutex mutex;

        // 書きだした局面の数
        uint64_t sfen_write_count = 0;

        // ファイルに書き出す前のバッファ
        std::vector<SfensPointer> sfen_buffers;
        std::vector<SfensPointer> sfen_buffers_pool;
    };
#endif
#ifdef LEARN
    // Sfenの読み込み機
    struct SfenReader
    {
        SfenReader(int thread_num);
        
        ~SfenReader();
        
        // [ASYNC] スレッドが局面を一つ返す。なければfalseが返る。
        bool getSfen(size_t thread_id, PackedSfenValue& ps);
        
        // [ASYNC] スレッドバッファに局面を10000局面ほど読み込む。
        bool readToThreadBuffer(size_t thread_id);
        
        // 局面ファイルをバックグラウンドで読み込むスレッドを起動する。
        void startFileReadWorker();
        
        // ファイルの読み込み専用スレッド用
        void fileReadWorker();

        // 各スレッドがバッファリングしている局面数 0.1M局面。40HTで4M局面
        static const size_t THREAD_BUFFER_SIZE = 10 * 1000;

        // ファイル読み込み用のバッファ(これ大きくしたほうが局面がshuffleが大きくなるので局面がバラけていいと思うが
        // あまり大きいとメモリ消費量も上がる。
        static const size_t SFEN_READ_SIZE = 1000 * 100 * 5;

        static_assert(SFEN_READ_SIZE % THREAD_BUFFER_SIZE == 0, "");

        // sfenファイル群
        std::vector<std::string> filenames;

        // 読み込んだ局面数
        volatile uint64_t total_read;

        // total_readがこの値を超えたらupdate_weights()してmseの計算をする。
        uint64_t next_update_weights;

        int save_count;

    protected:

        // fileをバックグラウンドで読み込みしているworker thread
        std::thread file_worker_thread;

        PRNG prng;

        // ファイル群を読み込んでいき、最後まで到達したか。
        volatile bool files_end;

        // sfenファイルのハンドル
        std::fstream fs;

        // 各スレッド用のsfen
        std::vector<SfensPointer> packed_sfens;

        // packed_sfens_poolにアクセスするときのmutex
        Mutex mutex;

        // sfenのpool。fileから読み込むworker threadはここに補充する。
        // 各worker threadはここから自分のpacked_sfens[thread_id]に充填する。
        // ※　mutexをlockしてアクセスすること。
        std::vector<SfensPointer> packed_sfens_pool;
    };
#endif
} // namespace Learn