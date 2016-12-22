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

#include <fstream>
#include <sstream>
#include "book.h"
#include "common.h"
#include "usi.h"

// bookはグローバルに用意。
MemoryBook Book;

using namespace std;

// USIの指し手文字列などで筋を表す文字列をここで定義されたFileに変換する。
File fileOf(char c) { return File('9' - c); }

// USIの指し手文字列などで段を表す文字列をここで定義されたRankに変換する。
Rank rankOf(char c) { return Rank(c - 'a'); }

// USIの指し手文字列などに使われている盤上の升を表す文字列をSquare型に変換する
// 変換できなかった場合はSQ_MAXが返る。
Square sqOf(char f, char r)
{
    File file = fileOf(f);
    Rank rank = rankOf(r);

    if (isOK(file) && isOK(rank))
        return sqOf(file, rank);

    return SQ_MAX;
}

namespace USI
{
    // usi文字列の指し手をMoveインスタンスに変換
    extern Move toMove(const Board& b, std::string str);

    // usi形式から指し手への変換。movepiece, capturepieceの情報が欠落している。
    Move toMove(const string& str)
    {
        const std::string piece_to_char(".BRPLNSGK........brplnsgk");

        if (str.length() <= 3)
            return MOVE_NONE;

        Square to = sqOf(str[2], str[3]);

        if (!isOK(to))
            return MOVE_NONE;

        bool promote = str.length() == 5 && str[4] == '+';
        bool drop = str[1] == '*';

        Move move = MOVE_NONE;

        if (!drop)
        {
            Square from = sqOf(str[0], str[1]);

            if (isOK(from))
                move = makeMove(from, to, EMPTY, EMPTY, promote);
        }
        else
        {
            for (int i = 1; i <= 7; ++i)
                if (piece_to_char[i] == str[0])
                {
                    move = makeDrop((Piece)i, to);
                    break;
                }
        }

        return move;
    }
}

// 手動で定跡手を登録するモード
void MemoryBook::make(const Board& b, const std::string filename)
{
    std::string y;

    if (read(filename))
    {
        std::cout << "ファイルが開けません" << std::endl;
        return;
    }

    std::cout << b << "検索中...\n";

    auto it = book.find(b.sfen());

    if (it != book.end())
    {
        // 定跡にヒット
        Move m = it->second;

        // 指し手に欠落している情報があるかもしれないので補う
        if (!isDrop(m))
            m = makeMove(fromSq(m), toSq(m), b.piece(fromSq(m)), b.piece(toSq(m)), isPromote(m));

        std::cout << "今登録されてる手は" << pretty(m) << " " << toUSI(m) << "です。どうしますか？\n";
    }
    else
        std::cout << "登録されている手はありません。どうしますか？";

    do {
        std::cout << "<登録:y やめる:n>:";
        std::cin >> y;
    } while (y != "y" && y != "n");

    if (y == "y")
    {
        std::cout << "登録開始です\n手をUSI形式で入力:";

        std::string str;
        std::cin >> str;
        Move m = USI::toMove(b, str);

        if (m != MOVE_NONE)
        {
            std::cout << "登録していいですか?(y/n)";
            std::cin >> y;

            if (y == "y")
            {
                std::cout << pretty(m) << "を登録します" << std::endl;
                store(filename, b.sfen(), m);
            }
            else
                std::cout << "やめときます" << std::endl;
        }
        else
            std::cout << "そんな手はありません\n";
    }

    std::cout << "登録終わり" << std::endl;

    // std::cinの後に改行コードが残ってるみたいなので。
    char temp[100];
    cin.getline(temp, sizeof(temp));
}

// bookに指し手を加えてファイルに書き出す。
void MemoryBook::store(const std::string filename, const std::string sfen, const Move m)
{
    insert(sfen, m);
    write(filename);
}

// 定跡ファイルの読み込み(book.db)など。MemoryBookに読み出す
int MemoryBook::read(const std::string filename)
{
    vector<string> lines;

    if (readAllLines(filename, lines))
        return 1; // 読み込み失敗

    uint64_t num_sum = 0;
    string sfen;

    for (auto line : lines)
    {
        // バージョン識別文字列(とりあえず読み飛ばす)
        if (line.length() >= 1 && line[0] == '#')
            continue;

        // コメント行(とりあえず読み飛ばす)
        if (line.length() >= 2 && line.substr(0, 2) == "//")
            continue;

        // "sfen "で始まる行は局面のデータであり、sfen文字列が格納されている。
        if (line.length() >= 5 && line.substr(0, 5) == "sfen ")
        {
            sfen = line.substr(5, line.length() - 5); // 新しいsfen文字列を"sfen "を除去して格納
            continue;
        }

        Move best;

        istringstream is(line);
        string best_move;
        is >> best_move;

        // 起動時なので変換に要するオーバーヘッドは最小化したいので合法かのチェックはしない。
        if (best_move == "none" || best_move == "resign")
            best = MOVE_NONE;
        else
            best = USI::toMove(best_move);

        insert(sfen, best);
    }

    return 0;
}

// MemoryBookの内容をfilenameに書き出す
int MemoryBook::write(const std::string filename)
{
    fstream fs(filename, ios::out);

    // バージョン識別用文字列
    fs << "#YANEURAOU-DB2016 1.00" << endl;

    for (auto it = book.begin(); it != book.end(); ++it)
        fs << "sfen " << it->first << endl << it->second << endl; // sfenと指し手を出力

    fs.close();

    return 0;
}

// bookに指し手を加える。
void MemoryBook::insert(const std::string sfen, const Move m)
{
    auto it = book.find(sfen);

    if (it == book.end()) // 存在しないので追加。
        book[sfen] = m;
    else
        it->second = m;
}

Move MemoryBook::probe(const Board& b) const
{
    auto it = book.find(b.sfen());

    if (it != book.end() && it->second != MOVE_NONE)
    {
        // 定跡にヒット
        Move m = it->second;

        // 指し手に欠落している情報があるかもしれないので補う
        if (!isDrop(m))
            m = makeMove(fromSq(m), toSq(m), b.piece(fromSq(m)), b.piece(toSq(m)), isPromote(m));
        else
            m = makeDrop(toPiece(movedPieceType(m), b.turn()), toSq(m));

        return m;
    }

    return MOVE_NONE;
}
