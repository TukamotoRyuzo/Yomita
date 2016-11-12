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

#include "sfen_rw.h"

#ifdef GENSFEN
Learn::SfenWriter::SfenWriter(std::string filename, int thread_num)
{
    sfen_buffers.resize(thread_num);
    fs.open(filename, std::ios::out | std::ios::binary | std::ios::app);
    finished = false;
}

Learn::SfenWriter::~SfenWriter()
{
    finished = true;

    // ワーカースレッドが終了するのを待つ。
    file_worker_thread.join();
}

void Learn::SfenWriter::write(size_t thread_id, uint8_t data[32], int16_t value)
{
    // このバッファはスレッドごとに用意されている。
    auto& buf = sfen_buffers[thread_id];
    auto buf_reserve = [&]()
    {
        buf = SfensPointer(new std::vector<PackedSfenValue>());
        buf->reserve(FILE_WRITE_INTERVAL);
    };

    // 初回はbufがないのでreserve()する。
    if (!buf)
        buf_reserve();

    PackedSfenValue ps;
    memcpy(ps.data, data, 32);
    memcpy(ps.data + 32, &value, 2);

    // スレッドごとに用意されており、一つのスレッドが同時にこのwrite()関数を呼び出さないので
    // この時点では排他する必要はない。
    buf->push_back(ps);

    if (buf->size() >= FILE_WRITE_INTERVAL)
    {
        // sfen_buffers_poolに積んでおけばあとはworkerがよきに計らってくれる。
        {
            std::unique_lock<Mutex> lk(mutex);
            sfen_buffers_pool.push_back(buf);
        }
        buf_reserve();
        //std::cout << '[' << thread_id << ']';
    }
}

void Learn::SfenWriter::finalize(size_t thread_id)
{
    auto& buf = sfen_buffers[thread_id];
    if (buf->size() != 0)
    {
        std::unique_lock<Mutex> lk(mutex);
        sfen_buffers_pool.push_back(buf);
    }
}

void Learn::SfenWriter::startFileWriteWorker()
{
    file_worker_thread = std::thread([&] { this->fileWriteWorker(); });
}

void Learn::SfenWriter::fileWriteWorker()
{
    auto output_status = [&]()
    {
        // 現在時刻も出力
        std::cout << std::endl << sfen_write_count << " sfens , at " << localTime();

        // flush()はこのタイミングで十分。
        fs.flush();
    };

    while (!finished || sfen_buffers_pool.size())
    {
        std::vector<SfensPointer> buffers;

        {
            std::unique_lock<Mutex> lk(mutex);

            // poolに積まれていたら、それを処理する。
            if (sfen_buffers_pool.size() != 0)
            {
                buffers = sfen_buffers_pool;
                sfen_buffers_pool.clear();
            }
        }

        // 何も取得しなかったならsleep()
        // ひとつ取得したということはまだバッファにある可能性があるのでファイルに書きだしたあとsleep()せずに続行。
        if (!buffers.size())
        {
            sleep(100);
        }
        else
        {
            // バッファがどれだけ積まれているのかデバッグのために出力
            //std::cout << "[" << buffers.size() << "]";

            for (auto ptr : buffers)
            {
                fs.write((const char*)&((*ptr)[0].data), sizeof(PackedSfenValue) * ptr->size());

                //std::cout << "[" << ptr->size() << "]";
                sfen_write_count += ptr->size();

                // 棋譜を書き出すごとに'.'を出力。
                std::cout << ".";

                // 40回×GEN_SFENS_TIMESTAMP_OUTPUT_INTERVALごとに処理した局面数を出力
                if ((++time_stamp_count % uint64_t(40)) == 0)
                    output_status();
            }
        }
    }

    output_status();
}

#endif

#ifdef LEARN

#include <iomanip>

Learn::SfenReader::SfenReader(int thread_num)
{
    packed_sfens.resize(thread_num);
    total_read = 0;
    next_update_weights = 0;
    save_count = 0;
    files_end = false;

    // 比較実験がしたいので乱数を固定化しておく。
    prng = PRNG(20160720);
}

Learn::SfenReader::~SfenReader()
{
    file_worker_thread.join();
}

bool Learn::SfenReader::getSfen(size_t thread_id, PackedSfenValue& ps)
{
    // スレッドバッファに局面が残っているなら、それを1つ取り出して返す。
    auto& thread_ps = packed_sfens[thread_id];

    // バッファに残りがなかったらread bufferから充填するが、それすらなかったらもう終了。
    if ((thread_ps == nullptr || thread_ps->size() == 0) // バッファが空なら重点する。
        && !readToThreadBuffer(thread_id))
        return false;

    ps = *(thread_ps->rbegin());
    thread_ps->pop_back();
    return true;
}

bool Learn::SfenReader::readToThreadBuffer(size_t thread_id)
{
    auto read_from_buffer = [&]()
    {
        // read bufferから充填可能か？
        if (packed_sfens_pool.size() == 0)
            return false;

        packed_sfens[thread_id] = *packed_sfens_pool.rbegin();
        packed_sfens_pool.pop_back();
        total_read += THREAD_BUFFER_SIZE;
        return true;
    };

    while (true)
    {
        // ファイルバッファから充填できたなら、それで良し。
        {
            std::unique_lock<Mutex> lk(mutex);

            if (read_from_buffer())
                return true;
        }

        // もうすでに読み込むファイルは無くなっている。もうダメぽ。
        if (files_end)
            return false;

        // file workerがread_buffer1に充填してくれるのを待っている。
        // mutexはlockしていないのでいずれ充填してくれるはずだ。
        sleep(1);
    }

}

void Learn::SfenReader::startFileReadWorker()
{
    file_worker_thread = std::thread([&] { this->fileReadWorker(); });
}

void Learn::SfenReader::fileReadWorker()
{
    // 読み込むファイルがなくなったらfalse。
    auto open_next_file = [&]()
    {
        if (fs.is_open())
            fs.close();

        // もう無い
        if (filenames.size() == 0)
            return false;

        std::string filename = *filenames.rbegin();
        filenames.pop_back();

        // 生成した棋譜をテスト的に読み込むためのコード
        fs.open(filename, std::ios::in | std::ios::binary);

        if (!fs)
            std::cout << std::endl << "open error! filename = " << filename << std::endl;
        else
            std::cout << std::endl << "open filename = " << filename << std::endl;

        return true;
    };

    while (true)
    {
        // バッファが減ってくるのを待つ。
        while (packed_sfens_pool.size() >= SFEN_READ_SIZE / THREAD_BUFFER_SIZE)
            sleep(100);

        std::vector<PackedSfenValue> sfens;

        // ファイルバッファにSFEN_READ_SIZEだけファイルから読み込む。
        while (sfens.size() < SFEN_READ_SIZE)
        {
            PackedSfenValue p;

            // packed sfenをファイルから読み込めるだけ読み込んでsfensに格納。
            if (fs.read((char*)&p, sizeof(PackedSfenValue)))
            {
                if (_mm256_testz_si256(_mm256_set1_epi8(static_cast<char>(0xffu)), _mm256_loadu_si256((__m256i*)(p.data))))
                {
                    std::cout << "0" << std::endl;
                }
                else
                    sfens.push_back(p);
            }
            else
            {
                // 読み込み失敗
                if (!open_next_file())
                {
                    // 次のファイルもなかった。あぼーん。
                    std::cout << "..end of files.\n";
                    files_end = true;
                    return;
                }
            }
        }

        // この読み込んだ局面データをshuffleする。
        // random shuffle by Fisher-Yates algorithm
        for (size_t i = 0, size = sfens.size(); i < size; ++i)
            std::swap(sfens[i], sfens[prng.rand(size - i) + i]);

        // これをTHREAD_BUFFER_SIZEごとの細切れにする。それがsize個あるはず。
        const size_t size = SFEN_READ_SIZE / THREAD_BUFFER_SIZE;
        std::vector<SfensPointer> ptrs;
        ptrs.resize(size);

        for (size_t i = 0; i < size; ++i)
        {
            SfensPointer ptr(new std::vector<PackedSfenValue>());
            ptr->resize(THREAD_BUFFER_SIZE);
            memcpy(&((*ptr)[0]), &sfens[i * THREAD_BUFFER_SIZE], sizeof(PackedSfenValue) * THREAD_BUFFER_SIZE);
            ptrs[i] = ptr;
        }

        // sfensの用意が出来たので、折を見てコピー
        {
            std::unique_lock<Mutex> lk(mutex);

            // 300個ぐらいなのでこの時間は無視できるはず…。
            auto size2 = packed_sfens_pool.size();

            packed_sfens_pool.resize(size2 + size);

            for (size_t i = 0; i < size; ++i)
                packed_sfens_pool[size2 + i] = ptrs[i];
        }
    }
}

#endif