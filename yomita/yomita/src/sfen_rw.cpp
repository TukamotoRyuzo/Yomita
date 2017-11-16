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

#include "sfen_rw.h"

#ifdef LEARN

// Readerとしての初期化。
void Learn::AsyncSfenRW::initReader(int thread_num, const std::vector<std::string>& files)
{
    buffers.resize(thread_num);

    for (auto& v : buffers)
        v = nullptr;

    rw_count = 0;
    prng = PRNG(20160720);
    filenames = files;
    finished = false;
}

// データを一つ読みだす。
bool Learn::AsyncSfenRW::read(size_t thread_id, PackedSfenValue& ps)
{
    auto& p = buffers[thread_id];

    // データがない場合、プールから一つもらう。
    if (p == nullptr || p->size() == 0)
    {
        // 用済みの領域なので破棄。
        delete p;

        while (!finished)
        {
            {
                std::unique_lock<Mutex> lk(mutex);

                if (buffers_pool.size())
                {
                    p = buffers_pool.back();
                    buffers_pool.pop_back();
                    rw_count += THREAD_BUFFER_SIZE;
                    break;
                }
            }
            // プールにデータがたまるまで待つ。
            sleep(100);
        }

        if (finished)
            return false;
    }

    ps = p->back();
    p->pop_back();
    return true;
}

// ファイルを一つ開く。
bool Learn::AsyncSfenRW::open()
{
    if (fs.is_open())
        fs.close();

    do {
        if (filenames.size() == 0)
            return false;

        auto file = filenames.back();
        filenames.pop_back();
        fs.open(file, std::ios::in | std::ios::binary);

        if (!fs)
            std::cout << std::endl << "open error! filename = " << file << std::endl;
        else
            std::cout << std::endl << "open filename = " << file << std::endl;
    } while (!fs);

    return true;
}

void Learn::AsyncSfenRW::workReader()
{
    const size_t size = READ_SIZE / THREAD_BUFFER_SIZE;

    while (true)
    {
        while (buffers_pool.size() >= size)
            sleep(100);

        std::vector<PackedSfenValue> v;

        while (v.size() < READ_SIZE)
        {
            PackedSfenValue p;

            if (fs.read((char*)&p, sizeof(PackedSfenValue)))
                v.push_back(p);

            else if (!open())
            {
                std::cout << "end of files." << std::endl;
                finished = true;
                return;
            }
        }

        // random shuffle by Fisher-Yates algorithm
        for (size_t i = 0, size = v.size(); i < size; ++i)
            std::swap(v[i], v[prng.rand(size - i) + i]);

        std::vector<SfenVector*> ptrs;

        for (size_t i = 0; i < size; ++i)
        {
            SfenVector* ptr = new SfenVector();
            ptr->resize(THREAD_BUFFER_SIZE);
            memcpy(ptr->data(), &v[i * THREAD_BUFFER_SIZE], sizeof(PackedSfenValue) * THREAD_BUFFER_SIZE);
            ptrs.push_back(ptr);
        }

        std::unique_lock<Mutex> lk(mutex);

        for (size_t i = 0; i < size; ++i)
            buffers_pool.push_back(ptrs[i]);
    }
}

// Writerとしての初期化
void Learn::AsyncSfenRW::initWriter(int thread_num, std::string filename)
{
    rw_count = 0;
    time_stamp_count = 0;
    buffers.resize(thread_num);

    for (auto& v : buffers)
        v = nullptr;

    fs.open(filename, std::ios::out | std::ios::binary | std::ios::app);
    finished = false;
}

void Learn::AsyncSfenRW::finalizeWriter(size_t thread_id)
{
    auto& buf = buffers[thread_id];

    if (buf->size() != 0)
    {
        std::unique_lock<Mutex> lk(mutex);
        buffers_pool.push_back(buf);
    }
}

void Learn::AsyncSfenRW::terminate()
{
    finished = true;
    worker.join();
    fs.close();
}

void Learn::AsyncSfenRW::write(size_t thread_id, const PackedSfenValue& ps)
{
    auto& p = buffers[thread_id];

    if (p == nullptr)
        p = new SfenVector();

    p->push_back(ps);

    if (p->size() >= FILE_WRITE_INTERVAL)
    {
        {
            std::unique_lock<Mutex> lk(mutex);
            buffers_pool.push_back(p);
        }

        p = new SfenVector();
    }
}

void Learn::AsyncSfenRW::workWriter()
{
    auto output_status = [&]()
    {
        std::cout << std::endl << rw_count << " sfens " << localTime();
        fs.flush();
    };

    while (!finished || buffers_pool.size())
    {
        std::vector<SfenVector*> buf;

        {
            std::unique_lock<Mutex> lk(mutex);

            if (buffers_pool.size())
            {
                buf = buffers_pool;
                buffers_pool.clear();
            }
        }

        if (!buf.size())
            sleep(100);

        else
        {
            for (auto ptr : buf)
            {
                fs.write((const char*)ptr->data(), sizeof(PackedSfenValue) * ptr->size());
                rw_count += ptr->size();
                std::cout << ".";
                delete ptr;

                if ((++time_stamp_count % 40) == 0)
                    output_status();
            }
        }
    }

    output_status();
}

#endif