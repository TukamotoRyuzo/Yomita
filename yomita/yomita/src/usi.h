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

#include <map>
#include <string>
#include "platform.h"
#include "thread.h"

struct OptionsMap;

// USIプロトコルで定義されているオプションを実装するOptionクラス
class Option
{
    // この項目が変更されたときに呼び出されるハンドラ
    typedef void (Fn)(const Option&);

public:

    // デフォルトコンストラクタ
    Option(Fn* = nullptr);

    // bool型用  
    // v = デフォルト値かつ現在の値
    Option(bool v, Fn* = nullptr);

    // string型用  
    // v = デフォルト値かつ現在の値
    Option(const char* v, Fn* = nullptr);

    // int型用  
    // v = デフォルト値 , min_ = 最小値 , max_ = 最大値 , この項目が変更されたときに呼び出されるハンドラ
    Option(int v, int min_, int max_, Fn* = nullptr);

    // 代入用。UCIプロトコルで渡された文字列をvに入れるとコンストラクタで格納していた型に変換して格納される。  
    // また、このクラスのコンストラクタでFnとして渡されていたハンドラがnullptrでなければそれを呼び出す。  
    Option& operator = (const std::string& v);

    // int型への変換子。current_value_の文字列がint化されて返る。  
    operator int() const
    {
        assert(type_ == "check" || type_ == "spin");
        return (type_ == "spin" ? stoi(current_value_) : current_value_ == "true");
    }

    // string型への変換子。current_value_の文字列がそのまま返る。  
    operator std::string() const { assert(type_ == "string"); return current_value_; }

private:

    // coutにoperator << でオプション項目一式をUCIプロトコルで出力するためのもの。 
    friend std::ostream& operator << (std::ostream&, const OptionsMap&);

    // このOption項目(がint型のとき)のデフォルト値、現在の値  
    // type_は、UCIプロトコルのoptionコマンドで通知するこのOption項目の型。  
    // 例) int型ならspin(スピンコントロールを表示するためにこれを指定する)  
    std::string default_value_, current_value_, type_;

    // このOption項目(がint型のとき)の最小値、最大値 (spinコントロールでつかう)
    int min_, max_;

    // このOption項目がUCIプロトコルで変更されたときに呼び出されるハンドラ 
    Fn* on_change_;
};

// 大文字小文字を区別せず、アルファベット順の大小比較をする。これはstd::mapのために定義されている。
// これにより、OptionsMapはアルファベット順にオーダリングされることになる。
struct CaseInsensitiveLess
{
    bool operator() (const std::string& s1, const std::string& s2) const
    {
        // lexicographical_compare はコンテナを比較する
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(),
            [](char c1, char c2) { return tolower(c1) < tolower(c2); });
    }
};

// 我々のオプション設定を保持するコンテナは実際はstd::mapである
// mapは<キー値の型,格納されている値の型,mapオーダリングの基準>で指定する。
struct OptionsMap : public std::map<std::string, Option, CaseInsensitiveLess>
{
public:
    void init();
    bool isLegalOption(const std::string name) { return this->find(name) != std::end(*this); }
};

struct SignalsType
{
    std::atomic_bool stop, stop_on_ponderhit;
};

struct LimitsType
{
    // コンストラクタではこの構造体をゼロクリア
    LimitsType() { std::memset(this, 0, sizeof(LimitsType)); }
    bool useTimeManagement() const { return !(mate | move_time | depth | nodes | infinite); }

    std::vector<Move> search_moves;

    // time 残り時間(この1局について) [ms]
    int time[TURN_MAX];

    // 増加時間
    int inc[TURN_MAX];

    // 秒読み(ms換算で)
    int byoyomi;

    // 探索深さを固定するとき用(0以外を指定してあるなら)
    int depth;

    // 固定思考時間(0以外が指定してあるなら) : 単位は[ms]
    int move_time;
 
    //  詰み探索モードのときは、ここに思考時間が指定されている。
    //  この思考時間いっぱいまで考えて良い。
    int mate;

    // 思考時間無制限かどうかのフラグ。非0なら無制限。
    bool infinite;

    // ponder中であるかのフラグ
    bool ponder;

    // この探索ノード数だけ探索したら探索を打ち切る。
    int64_t nodes;

    // go開始時刻
    TimePoint start_time;
};

class Board;

namespace USI
{
    const std::string START_POS = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";
    const std::string BENCHMARK = "l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w GR5pnsg 1";
    const std::string MAX_MOVE_POS = "R8/2K1S1SSk/4B4/9/9/9/9/9/1L1L1L3 b RBGSNLP3g3n17p 1";
    extern OptionsMap Options;
    extern SignalsType Signals;
    extern LimitsType Limits;

    // 将棋所で将棋を指せるようにするためのメッセージループ。
    void loop(int argc, char** argv);

    // よく使うので。
    void isReady();

    std::string score(Score s);
    std::string pv(const Board& b, Depth depth, Score alpha, Score beta);
}