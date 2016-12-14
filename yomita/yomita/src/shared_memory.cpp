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

// memory mapped fileに必要。
namespace Eval
{
    extern void loadSub();

    void load()
    {
        if (!(bool)USI::Options["EvalShare"])
        {
            auto shared = new SharedEval();
            et.set(shared);
            loadSub();
            SYNC_COUT << "info string use non-shared eval memory" << SYNC_ENDL;
            return;
        }

        std::string dir_name = USI::Options["EvalDir"];
        replace(dir_name.begin(), dir_name.end(), '\\', '_');
        replace(dir_name.begin(), dir_name.end(), '/', '_');

        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cv;

        auto mapped = TEXT("YOMITA_KPPT_MMF") + cv.from_bytes(dir_name);
        auto mutex = TEXT("YOMITA_KPPT_MUTEX") + cv.from_bytes(dir_name);
        auto h_mutex = CreateMutex(NULL, FALSE, mutex.c_str());

        WaitForSingleObject(h_mutex, INFINITE);
        {
            auto h_map = CreateFileMapping(INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE,
                0, sizeof(SharedEval),
                mapped.c_str());

            bool exists = GetLastError() == ERROR_ALREADY_EXISTS;
            auto shared = (SharedEval*)MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedEval));
            et.set(shared);

            if (!exists)
            {
                loadSub();
                SYNC_COUT << "info string created shared eval memory" << SYNC_ENDL;
            }
            else
                SYNC_COUT << "info string use shared eval memory" << SYNC_ENDL;

            ReleaseMutex(h_mutex);
        }
    }
} // namespace Eval;

#endif