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

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "usi.h"
#include "board.h"
#include "genmove.h"
#include "test.h"
#include "benchmark.h"
#include "book.h"
#include "eval_kppt.h"
#include "timeman.h" // for ponderhit 

const std::string engine_name = "Yomita_";
const std::string version = "1.52";

// USIプロトコル対応のGUIとのやりとりを受け持つクラス
namespace USI
{
	OptionsMap Options;
	SignalsType Signals;
	LimitsType Limits;

	// 各コマンドの意味については
	// http://www.geocities.jp/shogidokoro/usi.html
	// を参照のこと

	// "isready"が送られたときに行う処理
	void isReady();

	// USIの"position"コマンドに対して呼び出される  
	void position(Board& b, std::istringstream& up);

	// 思考時間等を受け取り、探索を開始する。GUIから"go"コマンドを受け取ったときに呼び出される。  
	void go(const Board& b, std::istringstream& ss_cmd);

	// "setoption"が送られたときに行う処理
	void setOption(std::istringstream& ss_cmd);

	// 指し手生成の速度を計測
	void measureGenerateMoves(const Board& b, bool check);

	// usi文字列の指し手をMoveインスタンスに変換
	Move toMove(const Board& b, std::string str);
} // namespace USI

namespace Learn
{
#ifdef GENSFEN
	void genSfen(Board& b, std::istringstream& is);
	void mergeSfen(std::istringstream& is);
	void testPackedSfenFile(std::istringstream& is);
#endif
#ifdef LEARN
	void learn(Board& b, std::istringstream& is);
	void learnProgress(Board& b, std::istringstream& is);
#endif
	
} // namespace Learn


void USI::isReady()
{
	static bool first = true;

	// 評価関数の読み込みなど時間のかかるであろう処理はこのタイミングで行なう。
	if (first)
	{
#ifdef USE_EVAL
		// 評価関数の読み込み
		Eval::load();
#endif
		first = false;
	}

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

	// setup_statusはunique_ptrなので以前にstackに詰まれているStateInfoは勝手にdeleteされる。
	Search::setup_status = StateStackPtr(new aligned_stack<StateInfo>);

	// 指し手のリストをパースする(あるなら)
	// moveをusi定義の記述に変換する
	int current_ply = b.ply();
	Move m;

	while ((is >> token) && (m = toMove(b, token)))
	{
		Search::setup_status->push(StateInfo());
		b.doMove(m, Search::setup_status->top());
		assert(b.verify());
		Eval::evaluate(b);
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

		else if (token == "btime")    { ss_cmd >> limits.time[BLACK]; }
		else if (token == "wtime")    { ss_cmd >> limits.time[WHITE]; }
		else if (token == "binc")     { ss_cmd >> limits.inc[BLACK];  }
		else if (token == "winc")     { ss_cmd >> limits.inc[WHITE];  }
		else if (token == "movetime") { ss_cmd >> limits.move_time;   }
		else if (token == "byoyomi")  { ss_cmd >> limits.byoyomi;     }
		else if (token == "depth")    { ss_cmd >> limits.depth;		  }
		else if (token == "nodes")    { ss_cmd >> limits.nodes;		  }
		else if (token == "ponder")   { limits.ponder   = true;		  }
		else if (token == "infinite") { limits.infinite = true;		  }
	}

	if (limits.byoyomi && limits.byoyomi * 6 > limits.time[b.turn()])
		limits.move_time = limits.byoyomi - Options["byoyomi_margin"];

	Threads.startThinking(b, limits);
}

// "setoption"が送られたときに行う処理
void USI::setOption(std::istringstream& ss_cmd)
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
				pms = generate<SPEED_CHECK>(pms, b);
				//pms = generate<CHECK>(pms, b);
			}
		}
		else 
		{
			for (uint64_t i = 0; i < num; i++)
			{
				pms = &legal_moves[0];
				pms = generate<CAPTURE_PLUS_PROMOTE>(pms, b);
				pms = generate<NO_CAPTURE_MINUS_PROMOTE>(pms, b);
				pms = generate<DROP>(pms, b);
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

// usi文字列の指し手をMoveインスタンスに変換
Move USI::toMove(const Board& b, std::string str)
{
	for (auto m : MoveList<LEGAL_ALL>(b))
		if (str == toUSI(m))
			return m;

	return MOVE_NONE;
}

// 将棋所で将棋を指せるようにするためのメッセージループ。
void USI::loop(int argc, char** argv)
{
	// デフォルトでログを取る。
	startLogger(true);

	// 評価関数の読み込みが行われてからでないと局面のセットはできない。
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
			|| token == "ponderhit" && Signals.stop_on_ponderhit
			|| token == "gameover")
		{
			Signals.stop = true;
			Threads.main()->startSearching(true);
		}

		// USIエンジンとして認識されるために必要なコマンド
		else if (token == "usi")
		{
			SYNC_COUT << "id name " << engine_name << EVAL_TYPE << version
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
		else if (token == "usinewgame") { Search::clear(); }

		// 時間のかかる前処理はここで。
		else if (token == "isready") { isReady(); }

		// エンジンに設定するパラメータが送られてくるのでそれを設定
		else if (token == "setoption") { setOption(ss_cmd); }

		// 思考開始の合図。エンジンはこれを受信すると思考を開始。
		else if (token == "go") { go(board, ss_cmd); }

		// 思考開始する局面を作る。
		else if (token == "position") { position(board, ss_cmd); }

		// 以下、拡張コマンド
		// 指し手生成祭りの局面をセット
		else if (token == "maturi") { board.init(USI::BENCHMARK); std::cout << "maturi set." << std::endl; }

		// 合法手が一番多い局面をセット
		else if (token == "max") { board.init(USI::MAX_MOVE_POS); std::cout << "max set." << std::endl; }

		// ベンチマーク。
		else if (token == "b") { benchmark(board); }

		// 現局面を表示させる。内部状態を見たいときに使う。
		else if (token == "p") { std::cout << board << std::endl; }

		// 現局面で 5M回指し手生成して速度を量る。
		else if (token == "s") { measureGenerateMoves(board); }

		// この局面の合法手をすべて表示する。
		else if (token == "legal")
		{ 
			MoveList<LEGAL_ALL> mlist(board);
			std::cout << "size = " << mlist.size() << std::endl;
			for(auto m : mlist) 
				std::cout << pretty(m); 
			std::cout << std::endl; 
		}

		else if (token == "drop")
		{
			MoveList<DROP> mlist(board);
			std::cout << "size = " << mlist.size() << std::endl;
			for (auto m : mlist)
				std::cout << pretty(m);
			std::cout << std::endl;
		}

		// 現局面で1手詰めルーチンを呼ぶ。
		else if (token == "mate") { std::cout << board.mate1ply() << std::endl; }

		// この局面の王手
		else if (token == "c") { for (auto m : MoveList<SPEED_CHECK>(board)) std::cout << pretty(m); }

		// この局面での王手生成速度チェック
		else if (token == "cs") { measureGenerateMoves(board, true); }

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
				Signals.stop = true;
		}

		// 手動で定跡手を追加。これで一手ずつ定跡を登録するのはつらすぎるが、定跡を読み込めてるかチェックするときには便利。
		else if (token == "mb") { Book::makeBook(board, BOOK_STR); }

		// 評価関数を呼び出す
		else if (token == "eval") { std::cout << "eval = " << Eval::evaluate(board) << std::endl; }

		// 入玉宣言勝ちかどうか
		else if (token == "nyu") { std::cout << board.isDeclareWin() << std::endl; }

#if defined CONVERT_EVAL
		// 縦型Square用に作られたevalファイルを横型用に変換する。
		else if (token == "convert_eval_f2r") { Eval::convertEvalFileToRank(); }

		// 横型Square用に作られたevalファイルを縦型用に変換する。
		else if (token == "convert_eval_r2f") { Eval::convertEvalRankToFile(); }
#endif

#ifdef GENSFEN
		// 棋譜の自動生成
		else if (token == "gensfen") { Learn::genSfen(board, ss_cmd); }

		// 細切れになったpacked sfenファイルの結合
		else if (token == "merge") { Learn::mergeSfen(ss_cmd); }

#ifdef LEARN
		// packed sfenファイルが正しいフォーマットになっているかのテスト
		else if (token == "packtest") { Learn::testPackedSfenFile(ss_cmd); }
#endif
#endif
#ifdef LEARN
		else if (token == "l") 
		{ 
			isReady();
			board.init(USI::START_POS);
		
			TimePoint start = now();
			auto c = Learn::search(board, -SCORE_INFINITE, SCORE_INFINITE, 6);
			TimePoint elapsed = now() - start;
			SYNC_COUT << "score = " << c.first << "pv = ";

			for (auto m : c.second)
				std::cout << pretty(m);

			std::cout << "\ntime = " << elapsed << "ms";
			std::cout << SYNC_ENDL;
		}

		// 棋譜からの学習
		else if (token == "learn") { Learn::learn(board, ss_cmd); }

		else if (token == "save_eval") { Eval::saveEval("eval_zero"); }

		// 棋譜からの進行度学習
		else if (token == "learn_progress") { Learn::learnProgress(board, ss_cmd); }
#endif
		// 有効なコマンドではない。
		else { SYNC_COUT << "unknown command: " << cmd << SYNC_ENDL; }

	} while (cmd != "quit" && argc == 1); // コマンドライン引数がある場合はループしない。

	Threads.main()->join();
}

// USIプロトコルに合うようににScoreをstd::stringに変換する
std::string USI::score(Score s)
{
	std::stringstream ss;

	if (abs(s) < SCORE_MATE - MAX_PLY)
		ss << "cp " << s * 100 / PAWN_SCORE;
	else
		ss << "mate " << (s > 0 ? SCORE_MATE - s + 1 : -SCORE_MATE - s + 1);

	return ss.str();
}
