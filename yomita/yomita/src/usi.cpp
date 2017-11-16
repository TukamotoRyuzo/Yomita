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

#include <string>
#include <vector>
#include <sstream>
#include <iostream>

#include "usi.h"
#include "book.h"
#include "board.h"
#include "timeman.h" // for ponderhit 

const std::string engine_name = "Yomita_" + std::string(EVAL_TYPE);
const std::string version = "4.61";

// USIプロトコル対応のGUIとのやりとりを受け持つクラス
namespace USI
{
    OptionsMap Options;
    LimitsType Limits;

    // 各コマンドの意味については
    // http://www.geocities.jp/shogidokoro/usi.html
    // を参照のこと

    // "isready"が送られたときに行う処理
    void isready();

    // USIの"position"コマンドに対して呼び出される
    void position(Board& b, std::istringstream& up);

    // 思考時間等を受け取り、探索を開始する。GUIから"go"コマンドを受け取ったときに呼び出される。  
    void go(const Board& b, std::istringstream& ss_cmd);

    // "setoption"が送られたときに行う処理
    void setoption(std::istringstream& ss_cmd);

    // 指し手生成の速度を計測
    void measureGenerateMoves(const Board& b, bool check);

    // usi文字列の指し手をMoveインスタンスに変換
    Move toMove(const Board& b, std::string str)
    {
        for (auto m : MoveList<LEGAL_ALL>(b))
            if (str == toUSI(m))
                return m;

        return MOVE_NONE;
    }
} // namespace USI

namespace Learn
{
#ifdef LEARN
    void genSfen(Board& b, std::istringstream& is);
    void learn(Board& b, std::istringstream& is);
    void learnProgress(Board& b, std::istringstream& is);
    void onlineLearning(Board& b, std::istringstream& is);
    void blend(Board& b, std::istringstream& is);
#endif
} // namespace Learn


void USI::isready()
{
    static bool first = true;

    // 評価関数の読み込みなど時間のかかるであろう処理はこのタイミングで行なう。
    if (first)
    {
#ifdef USE_EVAL
        Eval::load();
#endif
#ifdef USE_PROGRESS
        Progress::load();
#endif
        first = false;
    }

    Search::clear();
    SYNC_COUT << "readyok" << SYNC_ENDL;
}

 // "position"が送られたときに行う処理
void USI::position(Board &b, std::istringstream &is)
{
    std::string token, sfen;

    // ストリームから空白文字が出るまで読み込む
    is >> token;

    // 読み込んだ文字列でスイッチ
    if (token == "startpos")
    {
        // 初期局面として初期局面のSFEN形式の入力が与えられたとみなして処理する。        
        sfen = START_POS;

        // もしあるなら"moves"トークンを消費する。
        is >> token;
    }
    // 局面がsfen形式で指定されているなら、その局面を読み込む。
    // 平手初期局面以外での（例えば駒落ち）対局時に用いられるようだ
    else if (token == "sfen")
        while (is >> token && token != "moves")
            sfen += token + " ";
    else
        return;

    // ここで平手開始局面、または駒落ちの開始局面がsfenに代入されているので、その情報を使用してBoardのインスタンスを初期化。
    b.init(sfen, b.thisThread());
    Search::setup_status = StateStackPtr(new aligned_stack<StateInfo>);
    int current_ply = b.ply();
    Move m;

    // 指し手のリストをパースする(あるなら)
    while ((is >> token) && (m = toMove(b, token)))
    {
        Search::setup_status->push(StateInfo());
        b.doMove(m, Search::setup_status->top());
        assert(b.verify());
#ifdef USE_EVAL
        Eval::evaluate(b);
#endif
        ++current_ply;
    }

    b.setPly(current_ply);
}

// 思考開始の合図"go"が送られた時に行う処理。
void USI::go(const Board &b, std::istringstream& ss_cmd)
{
    std::string token;
    LimitsType limits; // コンストラクタで0クリアされる。
    limits.start_time = now();

    while (ss_cmd >> token) 
    {
        if (token == "searchmoves")
            while (ss_cmd >> token)
                limits.search_moves.push_back(toMove(b, token));

        else if (token == "btime")    ss_cmd >> limits.time[BLACK];
        else if (token == "wtime")    ss_cmd >> limits.time[WHITE];
        else if (token == "binc")     ss_cmd >> limits.inc[BLACK];
        else if (token == "winc")     ss_cmd >> limits.inc[WHITE];
        else if (token == "movetime") ss_cmd >> limits.move_time;
        else if (token == "byoyomi")  ss_cmd >> limits.byoyomi;
        else if (token == "depth")    ss_cmd >> limits.depth;
        else if (token == "nodes")    ss_cmd >> limits.nodes;
        else if (token == "ponder")   limits.ponder   = true;
        else if (token == "infinite") limits.infinite = true;
    }

    Threads.startThinking(b, limits);
}

// "setoption"が送られたときに行う処理
void USI::setoption(std::istringstream& ss_cmd)
{
    std::string token, name, value;

    // "name" が入力されるはず。
    ss_cmd >> token;
    assert(token == "name");

    // オプション名
    ss_cmd >> name;

    // " " が含まれた名前も扱う。
    while (ss_cmd >> token && token != "value")
        name += " " + token;

    // オプションの詳細な値
    ss_cmd >> value;

    // " " が含まれた値も扱う。
    while (ss_cmd >> token)
        value += " " + token;

    if (!Options.isLegalOption(name))
        std::cout << "No such option: " << name << std::endl;

    if (value.empty())
        Options[name] = true;
    else
        Options[name] = value;
}
#ifdef USE_BITBOARD
// 指し手生成の速度を計測
void USI::measureGenerateMoves(const Board& b, bool check = false)
{
    std::cout << b << std::endl;

    MoveStack legal_moves[MAX_MOVES];

    for (int i = 0; i < MAX_MOVES; ++i) 
        legal_moves[i].move = MOVE_NONE;

    MoveStack* pms = &legal_moves[0];
    const uint64_t num = check ? 20000000 : 5000000;
    TimePoint start = now();

    if (b.inCheck())
    {
        for (uint64_t i = 0; i < num; i++)
        {
            pms = &legal_moves[0];
            pms = generate<EVASIONS>(pms, b);
        }
    }
    else
    {
        if (check)
        {
            for (uint64_t i = 0; i < num; i++)
            {
                pms = &legal_moves[0];
                pms = generate<QUIET_CHECKS>(pms, b);
                //pms = generate<CHECK>(pms, b);
            }
        }
        else 
        {
            for (uint64_t i = 0; i < num; i++)
            {
                pms = &legal_moves[0];
                pms = generate<CAPTURE_PLUS_PAWN_PROMOTE>(pms, b);
                pms = generate<QUIETS>(pms, b);
            }
        }
    }

    TimePoint end = now();

    std::cout << "elapsed = " << end - start << " [msec]" << std::endl;

    if (end - start != 0) 
        std::cout << "times/s = " << num * 1000 / (end - start) << " [times/sec]" << std::endl;

    const ptrdiff_t count = pms - &legal_moves[0];
    std::cout << "num of moves = " << count << std::endl;

    for (int i = 0; i < count; ++i) 
        std::cout << legal_moves[i].move << " ";

    std::cout << std::endl;
}
#endif
extern void measureBBGenerateMoves(const Board& b);
extern void measure_module(const Board& b);

// 細切れになったsfenファイルを一纏めにするコマンド。
namespace Learn {
    void cleanSfen(Board& b, std::istringstream& is);
}
// 将棋所で将棋を指せるようにするためのメッセージループ。
void USI::loop(int argc, char** argv)
{
    // デフォルトでログを取る。
    startLogger(true);

    Board board(Threads.main());

    // USIから送られてくるコマンドを受け取るバッファ
    std::string cmd, token;

    for (int i = 1; i < argc; i++)
        cmd += std::string(argv[i]) + " ";

    do {
        if (argc == 1 && !std::getline(std::cin, cmd))
            cmd = "quit";

        // 入力されたコマンドを入力ストリームとする
        std::istringstream ss_cmd(cmd);

        token.clear();

        // 空白は読み飛ばしながら、tokenに代入
        ss_cmd >> std::skipws >> token;

        if (   token == "quit"
            || token == "stop"
            || token == "ponderhit" && Threads.stop_on_ponderhit
            || token == "gameover")
        {
            Threads.stop = true;
            Threads.main()->startSearching(true);
        }

        // USIエンジンとして認識されるために必要なコマンド
        else if (token == "usi")
        {
            SYNC_COUT << "id name " << engine_name << version
                << "\nid author Ryuzo Tukamoto"
                << "\n" << Options
                << "\nusiok" << SYNC_ENDL;
        }

        // ponderhitしたときのコマンド
        else if (token == "ponderhit") 
        { 
            Limits.ponder = false;	

            if (Limits.byoyomi)
            {
                Limits.start_time = now();
                Time.reset();
            }
        }

        // 新規対局開始のコマンド
        else if (token == "usinewgame") { /*Search::clear(); 遅いのでisreadyでやる*/ }

        // 時間のかかる前処理はここで。
        else if (token == "isready") { isready(); }

        // エンジンに設定するパラメータが送られてくるのでそれを設定
        else if (token == "setoption") { setoption(ss_cmd); }

        // 思考開始の合図。エンジンはこれを受信すると思考を開始。
        else if (token == "go") { go(board, ss_cmd); }

        // 思考開始する局面を作る。
        else if (token == "position") { position(board, ss_cmd); }

        // 以下、拡張コマンド

        else if (token == "maturi") { isready(); board.init("l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w GR5pnsg 1"); std::cout << "maturi set." << std::endl; }
        else if (token == "ryu") { isready(); board.init("8l/1l+R2P3/p2pBG1pp/kps1p4/Nn1P2G2/P1P1P2PP/1PS6/1KSG3+r1/LN2+p3L w Sbgn3p 124"); std::cout << "max set." << std::endl; }
        else if (token == "max") { isready(); board.init("R8/2K1S1SSk/4B4/9/9/9/9/9/1L1L1L3 b RBGSNLP3g3n17p 1"); std::cout << "max set." << std::endl; }

        // ベンチマーク。
        else if (token == "b") { benchmark(board); }

        // 現局面を表示させる。内部状態を見たいときに使う。
        else if (token == "p") { std::cout << board << std::endl; }
#ifdef USE_BITBOARD
        // 現局面で 5M回指し手生成して速度を量る。
        else if (token == "s") { measureGenerateMoves(board); }
#if defined USE_BYTEBOARD
        else if (token == "f") { measureBBGenerateMoves(board); }
        else if (token == "m") { measure_module(board); }
#endif
        // この局面での王手生成速度チェック
        else if (token == "cs") { measureGenerateMoves(board, true); }
#endif
        // この局面の合法手をすべて表示する。
        else if (token == "legal") { std::cout << MoveList<LEGAL_ALL>(board) << std::endl; }

        // 現局面で1手詰めルーチンを呼ぶ。
        else if (token == "mate") { std::cout << board.mate1ply() << std::endl; }

        // userTest()を呼び出す。
        else if (token == "d") { userTest(); }

        // sfenを出力
        else if (token == "sfen") { std::cout << board.sfen() << std::endl; }

        // ログファイルの書き出し
        else if (token == "log") { startLogger(true); }

        // perftを呼び出す
        else if (token == "perft") 
        { 
            int depth = 5;
            ss_cmd >> depth;
            perft(board, depth);
        }

        // 即指させたいとき。
        else if (token == "harry")
        {
            if (!Limits.ponder)
                Threads.stop = true;
        }

        else if (token == "resign")
        {
            SYNC_COUT << "bestmove resign" << SYNC_ENDL;

            if (!Limits.ponder)
                Threads.stop = true;
        }

        else if (token == "h") {
            Move m = Book.probe(board);
            std::cout << board << pretty(m) << std::endl;
        }
        // 評価関数を呼び出す
        else if (token == "eval") { std::cout << "eval = " << Eval::evaluate(board) << std::endl; }

#ifdef USE_PROGRESS
        // 進行度を表示
        else if (token == "prog") { std::cout << Progress::evaluate(board) * 100 << "%" << std::endl; }
#endif

#ifdef LEARN
        // 棋譜の自動生成
        else if (token == "gensfen") { Learn::genSfen(board, ss_cmd); }

        // 棋譜からの学習
        else if (token == "learn") { Learn::learn(board, ss_cmd); }

        else if (token == "testsfen") { Learn::cleanSfen(board, ss_cmd); }
#ifdef EVAL_KPPT
        else if (token == "blend") { Learn::blend(board, ss_cmd); }
#endif
        // 評価関数を今すぐ保存する。
        else if (token == "save_eval") 
        {
            std::string filename = timeStamp();
            ss_cmd >> filename;
            Eval::GlobalEvaluater->save(filename, true); 
        }
#ifdef USE_FILE_SQUARE_EVAL
        // 評価関数を今すぐ保存する。
        else if (token == "save_eval_no_file_convert")
        {
            std::string filename = "eval_zero";
            ss_cmd >> filename;
            Eval::GlobalEvaluater->saveNoFileConvert(filename);
        }
#endif
#ifdef USE_PROGRESS
        // 棋譜からの進行度学習
        else if (token == "learn_progress") { Learn::learnProgress(board, ss_cmd); }
#endif
        // 自己対戦&学習
        else if (token == "online") { Learn::onlineLearning(board, ss_cmd); }
#endif
        // 有効なコマンドではない。
        else { SYNC_COUT << "unknown command: " << cmd << SYNC_ENDL; }

        if (argc > 1)
            argc = 1;
    } while (cmd != "quit");

    Threads.main()->join();
}

// USIプロトコルに合うようににScoreをstd::stringに変換する
std::string USI::score(Score s)
{
    assert(-SCORE_INFINITE < s && s < SCORE_INFINITE);

    std::stringstream ss;

    if (abs(s) < SCORE_MATE - MAX_PLY)
        ss << "cp " << s * 100 / PAWN_SCORE;
    else
        ss << "mate " << (s > 0 ? SCORE_MATE - s - 1 : -SCORE_MATE - s + 1);

    return ss.str();
}
