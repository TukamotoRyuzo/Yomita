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

#define _CRT_SECURE_NO_WARNINGS 1

#ifdef _WIN32
#if _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifdef _MSC_VER
#include <windows.h>
#endif

extern "C"
{
    typedef bool(*fun1_t)(LOGICAL_PROCESSOR_RELATIONSHIP,
                          PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD);
    typedef bool(*fun2_t)(USHORT, PGROUP_AFFINITY);
    typedef bool(*fun3_t)(HANDLE, CONST GROUP_AFFINITY*, PGROUP_AFFINITY);
}
#endif

#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <codecvt>
#ifndef _MSC_VER
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "common.h"
#include "thread.h"

using namespace std;

namespace WinProcGroup {
#ifndef _MSC_VER
    void bindThisThread(size_t) {};
#else
    int getGroup(size_t idx)
    {
        int threads = 0;
        int nodes = 0;
        int cores = 0;
        DWORD return_length = 0;
        DWORD byte_offset = 0;

        HMODULE k32 = GetModuleHandle(TEXT("Kernel32.dll"));
        auto fun1 = (fun1_t)GetProcAddress(k32, "GetLogicalProcessorInformationEx");

        if (!fun1)
            return -1;

        if (fun1(RelationAll, nullptr, &return_length))
            return -1;

        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *buffer, *ptr;
        ptr = buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)malloc(return_length);

        if (!fun1(RelationAll, buffer, &return_length))
        {
            free(buffer);
            return -1;
        }

        while (ptr->Size > 0 && byte_offset + ptr->Size <= return_length)
        {
            if (ptr->Relationship == RelationNumaNode)
                nodes++;

            else if (ptr->Relationship == RelationProcessorCore)
            {
                cores++;
                threads += (ptr->Processor.Flags == LTP_PC_SMT) ? 2 : 1;
            }

            byte_offset += ptr->Size;
            ptr = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(((char*)ptr) + ptr->Size);
        }

        free(buffer);

        std::vector<int> groups;

        for (int n = 0; n < nodes; n++)
            for (int i = 0; i < cores / nodes; i++)
                groups.push_back(n);

        for (int t = 0; t < threads - cores; t++)
            groups.push_back(t % nodes);

        return idx < groups.size() ? groups[idx] : -1;
    }

    void bindThisThread(size_t idx)
    {
        if (Threads.size() < 8)
            return;

        int group = getGroup(idx);

        if (group == -1)
            return;
        
        HMODULE k32 = GetModuleHandle(TEXT("Kernel32.dll"));
        auto fun2 = (fun2_t)GetProcAddress(k32, "GetNumaNodeProcessorMaskEx");
        auto fun3 = (fun3_t)GetProcAddress(k32, "SetThreadGroupAffinity");

        if (!fun2 || !fun3)
            return;

        GROUP_AFFINITY affinity;

        if (fun2(group, &affinity))
            fun3(GetCurrentThread(), &affinity, nullptr);
    }
#endif
} // namespace WinProcGroup

// logging用のhack。streambufをこれでhookしてしまえば追加コードなしで普通に
// cinからの入力とcoutへの出力をファイルにリダイレクトできる。
// cf. http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
struct Tie : public streambuf
{
    Tie(streambuf* buf_, streambuf* log_) : buf(buf_), log(log_) {}

    int sync() { return log->pubsync(), buf->pubsync(); }
    int overflow(int c) { return write(buf->sputc((char)c), "<< "); }
    int underflow() { return buf->sgetc(); }
    int uflow() { return write(buf->sbumpc(), ">> "); }

    int write(int c, const char* prefix) {
        static int last = '\n';
        if (last == '\n')
            log->sputn(prefix, 3);
        return last = log->sputc((char)c);
    }

    streambuf *buf, *log; // 標準入出力 , ログファイル
};

struct Logger 
{
    static void start(bool b)
    {
        static Logger log;

        if (b && !log.file.is_open())
        {
            log.file.open("io_log.txt", ifstream::out);
            cin.rdbuf(&log.in);
            cout.rdbuf(&log.out);
            cout << "start logger" << endl;
        }
        else if (!b && log.file.is_open())
        {
            cout << "end logger" << endl;
            cout.rdbuf(log.out.buf);
            cin.rdbuf(log.in.buf);
            log.file.close();
        }
    }

private:
    Tie in, out;   // 標準入力とファイル、標準出力とファイルのひも付け
    ofstream file; // ログを書き出すファイル

    Logger() : in(cin.rdbuf(), file.rdbuf()), out(cout.rdbuf(), file.rdbuf()) {}
    ~Logger() { start(false); }

};
void startLogger(bool b) { Logger::start(b); }

// ファイルを丸読みする。ファイルが存在しなくともエラーにはならない。空行はスキップする。
int readAllLines(std::string filename, std::vector<std::string>& lines)
{
    fstream fs(filename, ios::in);
    if (fs.fail())
        return 1; // 読み込み失敗

    while (!fs.fail() && !fs.eof())
    {
        std::string line;
        getline(fs, line);
        if (line.length())
            lines.push_back(line);
    }
    fs.close();
    return 0;
}

// 現在の日にち、曜日、時刻を表す文字列を返す。
std::string localTime()
{
    auto now = std::chrono::system_clock::now();
    auto tp = std::chrono::system_clock::to_time_t(now);
    return std::ctime(&tp);
}

// YYYYMMDD形式で現在時刻を秒まで。
std::string timeStamp()
{
    char buff[20] = "";
    time_t now = time(NULL);
    struct tm *pnow = localtime(&now);
    sprintf(buff, "%04d%02d%02d%02d%02d%02d", pnow->tm_year + 1900, pnow->tm_mon + 1, pnow->tm_mday,
        pnow->tm_hour, pnow->tm_min, pnow->tm_sec);
    return std::string(buff);
}

std::string path(const std::string& folder, const std::string& filename)
{
    if (folder.length() >= 1 && *folder.rbegin() != '/' && *folder.rbegin() != '\\')
        return folder + "/" + filename;

    return folder + filename;
}

void _mkdir(std::string dir)
{
#ifdef _MSC_VER
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cv;

    if (_wmkdir(cv.from_bytes(dir).c_str()) == -1)
#else
#ifdef _WIN32
    if (mkdir(dir.c_str()) == -1)
#else
    if (mkdir(dir.c_str(),
                S_IRUSR | S_IWUSR | S_IXUSR |
                S_IRGRP | S_IWGRP | S_IXGRP |
                S_IROTH | S_IXOTH | S_IXOTH ) == -1)
#endif
#endif
    {
        if (errno == EEXIST)
            std::cout << "ディレクトリは dirname が既存のファイル、ディレクトリ、またはデバイスの名前であるため生成されませんでした。" << std::endl;
        else if (errno == ENOENT)
            std::cout << "パスが見つかりませんでした。" << std::endl;
    }
}
