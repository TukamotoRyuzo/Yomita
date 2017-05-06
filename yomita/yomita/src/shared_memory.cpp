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

#include "evaluate.h"

#ifdef USE_EVAL

#include <fstream>
#include <iostream>
#include <windows.h>
#include <codecvt>
#include "usi.h"
#include "eval_kppt.h"
#include "eval_ppt.h"
#include "eval_pptp.h"
#include "eval_kpptp.h"

// memory mapped fileに必要。
namespace Eval
{
    extern void loadSub();

    void load()
    {
#ifdef IS_64BIT
        if (!(bool)USI::Options["EvalShare"])
        {
#endif
#ifdef EVAL_KPPTP
            SharedEval s, *shared = &s;
            shared->malloc();
#else
#ifndef IS_64BIT
            SharedEval* shared = new SharedEval;
#else
            SharedEval* shared = (SharedEval*)_aligned_malloc(sizeof(SharedEval), 32);
#endif
#endif
            et.set(shared);
            loadSub();
            SYNC_COUT << "info string use non-shared eval memory" << SYNC_ENDL;
            return;
#ifdef IS_64BIT
        }
#endif
#ifdef IS_64BIT
        std::string dir_name = USI::Options["EvalDir"];
        replace(dir_name.begin(), dir_name.end(), '\\', '_');
        replace(dir_name.begin(), dir_name.end(), '/', '_');

        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cv;

        auto mapped = TEXT("YOMITA_KPPT_MMF") + cv.from_bytes(dir_name);
        auto mutex = TEXT("YOMITA_KPPT_MUTEX") + cv.from_bytes(dir_name);
        auto h_mutex = CreateMutex(NULL, FALSE, mutex.c_str());

#ifdef EVAL_KPPTP
            const auto sizekk = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * sizeof(Value);
            const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end) * sizeof(Value);
            const auto sizekkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end) * sizeof(Value);
            const size_t size = sizekk + sizekpp + sizekkp;
#else
            const size_t size = sizeof(SharedEval);
#endif

        WaitForSingleObject(h_mutex, INFINITE);
        {
            auto h_map = CreateFileMapping(INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE,
                DWORD(size >> 32), DWORD(size & 0xffffffffULL),
                mapped.c_str());

            bool exists = GetLastError() == ERROR_ALREADY_EXISTS;
#ifdef EVAL_KPPTP
            auto shared = (char*)MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, size);

            et.kk_ = (Value(*)[SQ_MAX][SQ_MAX])shared;
            et.kpp_ = (Value(*)[SQ_MAX][fe_end][fe_end])(shared + sizekk);
            et.kkp_ = (Value(*)[SQ_MAX][SQ_MAX][fe_end])(shared + sizekk + sizekpp);
#else
            auto shared = (SharedEval*)MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, size);
            et.set(shared);
#endif
            if (!exists)
            {
                loadSub();
                SYNC_COUT << "info string created shared eval memory" << SYNC_ENDL;
            }
            else
                SYNC_COUT << "info string use shared eval memory" << SYNC_ENDL;

            ReleaseMutex(h_mutex);
        }
#endif
    }
} // namespace Eval;

#endif