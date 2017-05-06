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

#include <algorithm>
#include <sstream>
#include "usi.h"
#include "tt.h"

using std::string;

// optionの値が変更されたことをトリガーとして呼び出されるハンドラ。
namespace
{
    void onThreads(const Option&) { Threads.readUsiOptions(); }
    void onHashSize(const Option& opt) { TT.resize(opt); }

#ifdef HELPER
    void onWriteDebugLog(const Option& opt) { startLogger(opt); }
#endif
}

 // init()は引数で渡されたUSI option設定をhard codeされたデフォルト値で初期化する
 // OptionNameにはスペースを入れてはいけない。入れてしまうと、オプションとして認識してくれなくなる。
 // on_が引数にあるものは値が変更されたときに実行したい関数があるとき
 // on_のみだとボタン
 // true/falseはチェックボックス
 // 数字はスピンコントロールで、第一引数がデフォルト値 第2、第3引数はmin_/max_
void OptionsMap::init()
{
#ifdef IS_64BIT
    const int MAX_MEMORY = 65536;
    const int DEFAULT_MEMORY = 1024;
#else
    const int DEFAULT_MEMORY = 1;
    const int MAX_MEMORY = 512;
#endif
    (*this)["Hash"]                  = Option(DEFAULT_MEMORY, 1, MAX_MEMORY, onHashSize);
    (*this)["USI_Ponder"]            = Option(true);
    (*this)["Threads"]               = Option(1, 1, 128, onThreads);
    (*this)["Minimum_Thinking_Time"] = Option(15, 0, 5000);
    (*this)["Move_Overhead"]         = Option(60, 0, 5000);
    (*this)["Slow_Mover"]            = Option(70, 10, 1000);
    (*this)["nodestime"]             = Option(0, 0, 10000);
    (*this)["byoyomi_margin"]        = Option(0, 0, 60000);
#ifdef HELPER
    (*this)["Write_Debug_Log"]       = Option(false, onWriteDebugLog);
#endif
    (*this)["Draw_Score"]            = Option(-50, -32000, 32000);
    (*this)["MultiPV"]               = Option(1, 1, 500);
    (*this)["UseBook"]               = Option(true);
    (*this)["BookDir"]               = Option("book.txt");
#ifdef USE_PROGRESS
    (*this)["ProgressDir"]           = Option("progress/1");
    (*this)["ProgressSaveDir"]       = Option("progress/save");
#endif
#ifdef USE_EVAL
    std::string eval = "eval/"     + std::string(EVAL_TYPE);
    std::string save = "evalsave/" + std::string(EVAL_TYPE);
#ifdef EVAL_KPPT
#ifdef USE_FILE_SQUARE_EVAL
    eval += "/WCSC27";
#else
    eval += "/1110";
#endif
#elif defined EVAL_PPT
    eval += "/25_2_282";
#elif defined EVAL_PPTP
    eval += "/8";
#elif defined EVAL_KPPTP
    eval += "/eval_zero";
#endif
#ifdef IS_64BIT
    (*this)["EvalShare"]             = Option(false);
#endif
#ifdef LEARN
    (*this)["EvalSaveDir"]           = Option(save.c_str());
#endif
    (*this)["EvalDir"]               = Option(eval.c_str());
#endif
}

// どんなオプション項目があるのかを表示する演算子。
std::ostream& operator << (std::ostream& os, const OptionsMap& om)
{
    for (auto it = om.begin(); it != om.end(); ++it)
    {
        const Option& o = it->second;
        os << "\noption name " << it->first << " type " << o.type_;

        if (o.type_ != "button")
            os << " default " << o.default_value_;

        if (o.type_ == "spin")
            os << " min " << o.min_ << " max " << o.max_;
    }
    return os;
}

// Optionクラスのコンストラクターと変換子。 
Option::Option(Fn* f) : type_("button"), min_(0), max_(0), on_change_(f) {}
Option::Option(const char* v, Fn* f) : type_("string"), min_(0), max_(0), on_change_(f)
{
    default_value_ = current_value_ = v;
}
Option::Option(bool v, Fn* f) : type_("check"), min_(0), max_(0), on_change_(f)
{
    default_value_ = current_value_ = (v ? "true" : "false");
}
Option::Option(int v, int minv, int maxv, Fn* f) : type_("spin"), min_(minv), max_(maxv), on_change_(f)
{
    default_value_ = current_value_ = std::to_string(v);
}

// オプションに値をセットする。その際、範囲チェックも行う
Option& Option::operator = (const string& v)
{ 
    assert(!type_.empty());

    if ((type_ != "button" && v.empty())
        || (type_ == "check" && v != "true" && v != "false")
        || (type_ == "spin" && (stoi(v) < min_ || stoi(v) > max_)))
        return *this;

    if (type_ != "button")
        current_value_ = v;

    if (on_change_ != nullptr)
        (*on_change_)(*this);

    return *this;
}

