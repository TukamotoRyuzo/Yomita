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

#define _CRT_SECURE_NO_WARNINGS 1

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <codecvt>
#include "common.h"

using namespace std;

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

std::string path(const std::string& folder, const std::string& filename)
{
	if (folder.length() >= 1 && *folder.rbegin() != '/' && *folder.rbegin() != '\\')
		return folder + "/" + filename;

	return folder + filename;
}

void mkdir(std::string dir)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> cv;

	if (_wmkdir(cv.from_bytes(dir).c_str()) == -1)
	{
		if (errno == EEXIST)
			std::cout << "ディレクトリは dirname が既存のファイル、ディレクトリ、またはデバイスの名前であるため生成されませんでした。" << std::endl;
		else if (errno == ENOENT)
			std::cout << "パスが見つかりませんでした。" << std::endl;
	}
}
