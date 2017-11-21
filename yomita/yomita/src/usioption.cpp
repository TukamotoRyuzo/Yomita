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

#include <sstream>
#include <fstream>
#include <algorithm>

#ifdef _MSC_VER
#include <filesystem>

#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem ;

#endif


#include "tt.h"
#include "usi.h"

std::string evalDir() { return path("eval", std::string(EVAL_TYPE)); }
std::string evalSaveDir() { return path("evalsave", std::string(EVAL_TYPE)); }

// EvalDirの中の評価関数バイナリをすべて書き並べて返す。
std::vector<std::string> evalFiles()
{
    std::vector<std::string> filenames;

#ifdef _MSC_VER
    for (std::tr2::sys::directory_iterator it(evalDir());
        it != std::tr2::sys::directory_iterator(); ++it)
#else
    for (fs::directory_iterator it(evalDir());
        it != fs::directory_iterator(); ++it)
#endif
    {
#ifdef _MSC_VER
        std::tr2::sys::path target = *it;
#else
        fs::path target = *it;
#endif

#ifdef _MSC_VER
        if (std::tr2::sys::is_directory(target))
#else
        if (fs::is_directory(target))
#endif
            filenames.push_back(path(evalDir(), target.filename().string()));
    }
    
    if (filenames.empty())
        filenames.push_back("none");

    return filenames;
}

// evalフォルダの中にconfig.txtがあり、そこにはどの評価関数を読み込むかが書かれているのでそれを読み込む。
// もし存在しない評価関数バイナリだった場合は適当にディレクトリ内の評価関数バイナリを返す。
std::string evalConfig(std::vector<std::string> eval_files)
{
    std::string conf = path(evalDir(), "config.txt");
    std::ifstream ifs(conf);

    if (!ifs)
    {
        std::cout << "info string " << conf << " not found." << std::endl;
        eval_files.front();
    }

    std::string ss;
    std::getline(ifs, ss);
    auto eval = path(evalDir(), ss);

    if (std::find(eval_files.begin(), eval_files.end(), eval) != eval_files.end())
        return eval;
    else
        return eval_files.front();
}

// init()は引数で渡されたUSI option設定をhard codeされたデフォルト値で初期化する。
// OptionNameにはスペースを入れてはいけない。入れてしまうと、オプションとして認識してくれなくなる。
void OptionsMap::init()
{
    const int MAX_MEMORY = Is64bit ? 65536 : 512;
    const int DEF_MEMORY = Is64bit ? 64 : 1;
    const int MAX_THREAD = Is64bit ? 128 : 4;

    (*this)["Hash"]                  = Option(DEF_MEMORY, 1, MAX_MEMORY, [](const Option& opt) { GlobalTT.resize(opt); });
    (*this)["USI_Ponder"]            = Option(true);
    (*this)["Threads"]               = Option(12, 1, MAX_THREAD, [](const Option&){ Threads.readUsiOptions(); });
    (*this)["NetworkDelay"]          = Option(0, 0, 60000);
    (*this)["DrawScore"]             = Option(-50, -32000, 32000);
    (*this)["MultiPV"]               = Option(1, 1, 500);
    (*this)["UseBook"]               = Option(true);
    (*this)["BookName"]              = Option("book.txt");
    (*this)["ResignScore"]           = Option(-32000, -32000, 32000);
#ifdef USE_PROGRESS
    (*this)["ProgressDir"]           = Option("progress/0.104809");
#endif
#ifdef USE_EVAL
    auto eval_files = evalFiles();
    (*this)["EvalDir"]               = Option(eval_files, evalConfig(eval_files));
    (*this)["EvalShare"]             = Option(false);
#endif
}

// どんなオプション項目があるのかを表示する演算子。
std::ostream& operator << (std::ostream& os, const OptionsMap& om)
{
    for (auto it = om.begin(); it != om.end(); ++it)
    {
        if (it->first == "USI_Ponder")
            continue;
        
        const Option& o = it->second;

        os << "\noption name " << it->first << " type " << o.type_;

        if (o.type_ != "button")
            os << " default " << o.default_value_;

        if (o.type_ == "combo")
            for (auto c : o.combo_)
                os << " var " << c;

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
Option::Option(const std::vector<std::string> combo, const std::string& v, Fn* f) : type_("combo"), min_(0), max_(0), on_change_(f), combo_(combo)
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
Option& Option::operator = (const std::string& v)
{ 
    assert(!type_.empty());

    if (   (type_ != "button" && v.empty())
        || (type_ == "check" && v != "true" && v != "false")
        || (type_ == "spin" && (stoi(v) < min_ || stoi(v) > max_)))
        return *this;

    if (type_ != "button")
        current_value_ = v;

    if (on_change_ != nullptr)
        (*on_change_)(*this);

    return *this;
}
