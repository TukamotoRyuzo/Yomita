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

#include <sstream>
#include "search.h"
#include "thread.h"
#include "usi.h"
#include "score.h"
#include "tt.h"
#include "genmove.h"
#include "evaluate.h"
#include "timeman.h"
#include "movepick.h"
#include "book.h"

// 置換表を用いるかどうか。
// 学習時は使わないほうがよいが、棋譜生成時には使ったほうが早く終わる。
#if !defined LEARN || defined GENSFEN
#define ENABLE_TT
#endif

// 評価関数の差分計算を行うかどうか。
#ifdef USE_EVAL
#define EVAL_DIFF
#endif

// 1手詰めルーチンを呼び出す手数。この手数以上であれば呼び出す。
#define MATE_PLY 70
#define MATE1PLY

using namespace Eval;
using namespace USI;
using namespace Search;
StateStackPtr Search::setup_status;

namespace
{ 
	// nullwindow探索をしていないときはPV
	enum NodeType { NO_PV, PV, };

	// d手で挽回可能な点数
	Score futilityMargin(Depth d, Board& b)
	{
#ifdef EVAL_KPPT
		return Score((int)(70 + 100 * b.state()->progress.rate()) * (int)d);
#else
		return Score(80 * d);
#endif
	}

	constexpr int QUIETS = 64;

	// Futility and reductions lookup tables, initialized at startup
	int FutilityMoveCounts[2][16];  // [improving][depth]
	Depth Reductions[2][2][64][QUIETS]; // [pv][improving][depth][moveNumber]

	// Razoring and futility margin based on depth
	const int razor_margin[4] = { 483, 570, 603, 721 };

	template <bool PvNode> Depth reduction(bool i, Depth d, int mn)
	{
		return Reductions[PvNode][i][std::min(d, 63 * ONE_PLY)][std::min(mn, QUIETS - 1)];
	}

	template <NodeType NT>
	Score search(Board& b, Stack* ss, Score alpha, Score beta, Depth depth, bool cut_node);

	template <NodeType NT, bool InCheck>
	Score qsearch(Board& b, Stack* ss, Score alpha, Score beta, Depth depth);

	Score scoreToTT(Score s, int ply);
	Score scoreFromTT(Score s, int ply);
	Score drawScore(Turn root_turn, Turn t);
	void updatePv(Move* pv, Move move, Move* childPv);
	void updateCmStats(Stack* ss, Move m, Score bonus);
	void checkTime();
	void updateStats(const Board& b, Stack* ss, Move move, Depth depth, Move* quiets, int quiet_cnt);

	// easy moveを検知するのに使われる。PVが深いiterationまで安定しているのなら、即座に指し手を返すことができる
	struct EasyMoveManager
	{
		void clear()
		{
			stableCnt = 0;
			expectedPosKey = 0;
			pv[0] = pv[1] = pv[2] = MOVE_NONE;
		}

		Move get(Key key) const { return expectedPosKey == key ? pv[2] : MOVE_NONE; }

		void update(Board& b, const std::vector<Move>& newPv)
		{
			assert(newPv.size() >= 3);

			// Keep track of how many times in a row the 3rd ply remains stable
			stableCnt = (newPv[2] == pv[2]) ? stableCnt + 1 : 0;

			if (!std::equal(newPv.begin(), newPv.begin() + 3, pv))
			{
				std::copy(newPv.begin(), newPv.begin() + 3, pv);

				StateInfo st[2];
				b.doMove(newPv[0], st[0]);
				b.doMove(newPv[1], st[1]);
				expectedPosKey = b.key();
				b.undoMove(newPv[1]);
				b.undoMove(newPv[0]);
			}
		}

		int stableCnt;
		Key expectedPosKey;
		Move pv[3];
	};

	// Set of rows with half bits set to 1 and half to 0. It is used to allocate
	// the search depths across the threads.
	typedef std::vector<int> Row;

	const Row HalfDensity[] =
	{
		{ 0, 1 },
		{ 1, 0 },
		{ 0, 0, 1, 1 },
		{ 0, 1, 1, 0 },
		{ 1, 1, 0, 0 },
		{ 1, 0, 0, 1 },
		{ 0, 0, 0, 1, 1, 1 },
		{ 0, 0, 1, 1, 1, 0 },
		{ 0, 1, 1, 1, 0, 0 },
		{ 1, 1, 1, 0, 0, 0 },
		{ 1, 1, 0, 0, 0, 1 },
		{ 1, 0, 0, 0, 1, 1 },
		{ 0, 0, 0, 0, 1, 1, 1, 1 },
		{ 0, 0, 0, 1, 1, 1, 1, 0 },
		{ 0, 0, 1, 1, 1, 1, 0 ,0 },
		{ 0, 1, 1, 1, 1, 0, 0 ,0 },
		{ 1, 1, 1, 1, 0, 0, 0 ,0 },
		{ 1, 1, 1, 0, 0, 0, 0 ,1 },
		{ 1, 1, 0, 0, 0, 0, 1 ,1 },
		{ 1, 0, 0, 0, 0, 1, 1 ,1 },
	};

	// std::extent<配列型>で配列の大きさを自動で読み取れる
	const size_t HalfDensitySize = std::extent<decltype(HalfDensity)>::value;
	Move ponder_candidate;
	EasyMoveManager EasyMove;
	Turn RootTurn;
	Score DrawScore;
} // namespace

Book::MemoryBook book;

void Search::init()
{
	Book::readBook(BOOK_STR, book);
	DrawScore = Score((int)Options["Draw_Score"]);

	for (int imp = 0; imp <= 1; ++imp)
		for (int d = 1; d < 64; ++d)
			for (int mc = 1; mc < QUIETS; ++mc)
			{
				double r = log(d) * log(mc) / 2;

				if (r < 0.80)
					continue;

				Reductions[NO_PV][imp][d][mc] = int(std::round(r)) * ONE_PLY;
				Reductions[PV][imp][d][mc] = std::max(Reductions[NO_PV][imp][d][mc] - ONE_PLY, DEPTH_ZERO);

				// Increase reduction for non-pv nodes when eval is not improving
				if (!imp && Reductions[NO_PV][imp][d][mc] >= 2 * ONE_PLY)
					Reductions[NO_PV][imp][d][mc] += ONE_PLY;
			}

	for (int d = 0; d < 16; ++d)
	{
		FutilityMoveCounts[0][d] = int(2.4 + 0.773 * pow(d + 0.00, 1.8));
		FutilityMoveCounts[1][d] = int(2.9 + 1.045 * pow(d + 0.49, 1.8));
	}
}

void Search::clear()
{
	TT.clear();

	for (Thread* th : Threads)
	{
		th->history.clear();
		th->counter_moves.clear();
		th->counter_move_history.clear();
		th->from_to_history.clear();
	}

	Threads.main()->previous_score = SCORE_INFINITE;
	DrawScore = Score((int)Options["Draw_Score"]);
}

void MainThread::search()
{
	bool nyugyoku_win = false;

	Turn t = RootTurn = root_board.turn();
	Time.init(Limits, t, root_board.ply());

	bool book_hit = false;

	if (root_moves.empty())
	{
		root_moves.push_back(RootMove(MOVE_NONE));

		SYNC_COUT << "info depth 0 score "
			<< USI::score(root_board.bbCheckers() ? -SCORE_MATE : SCORE_DRAW)
			<< SYNC_ENDL;
	}
	else if (root_board.isDeclareWin())
	{
		nyugyoku_win = true;
	}
	else
	{
		auto it = book.find(root_board.sfen());

		if (!Limits.infinite && it != book.end() && it->second.best_move != MOVE_NONE)
		{
			// 定跡にヒット
			Move m = it->second.best_move;

			// 指し手に欠落している情報があるかもしれないので補う
			if (!isDrop(m))
				m = makeMove(fromSq(m), toSq(m), root_board.piece(fromSq(m)), root_board.piece(toSq(m)), isPromote(m));
			else
				m = makeDrop(toPiece(movedPieceType(m), root_board.turn()), toSq(m));

			root_depth = DEPTH_MAX;

			auto it_move = std::find(root_moves.begin(), root_moves.end(), m);

			if (it_move != root_moves.end())
			{
				book_hit = true;
				std::swap(root_moves[0], *it_move);
			}
			else
				std::cout << "book move is illegal" << std::endl;
		}
		else
		{
			// slaveスレッドに探索開始局面を設定する
			for (auto th : Threads)
			{
				th->max_ply = 0;
				th->root_depth = DEPTH_ZERO;

				if (th != this)
				{
					// このとき探索ノード数(Board::nodes_)もリセットされる
					th->root_board = Board(root_board, th);
					th->root_moves = root_moves;
					th->startSearching();
				}
			}

			if (root_moves.size() == 1 && !Limits.move_time) // 一手しかないのですぐ指す
			{
				root_depth = DEPTH_MAX;
				root_moves[0].score = SCORE_INFINITE; // 絶対この手が選ばれるように
			}
			else
			{
				// 探索開始
				Thread::search();
			}
		}

	}

	if (!Signals.stop && (Limits.ponder || Limits.infinite))
	{
		Signals.stop_on_ponderhit = true;
		wait(Signals.stop);
	}

	Signals.stop = true;

	// slaveスレッドの探索がすべて終了するのを待つ
	for (auto th : Threads.slaves)
		th->join();

	Thread* best_thread = this;

	if (!this->easy_move_played && !book_hit)
	{
		for (Thread* th : Threads)
		{
			if (th->completed_depth > best_thread->completed_depth
				&& th->root_moves[0].score > best_thread->root_moves[0].score)
				best_thread = th;
		}
	}

	previous_score = best_thread->root_moves[0].score;

	// もし必要なら新たなpvを表示しておく
	if (best_thread != this)
		SYNC_COUT << USI::pv(best_thread->root_board, best_thread->completed_depth, -SCORE_INFINITE, SCORE_INFINITE) << SYNC_ENDL;

	if (nyugyoku_win)
	{
		SYNC_COUT << "bestmove win" << SYNC_ENDL;
	}
	else
	{
		// 手が何もないなら投了
		const Move best_move = best_thread->root_moves[0].pv[0];

		SYNC_COUT << "bestmove " << toUSI(best_move);

		if (Options["Ponder"]
			&& best_thread->root_moves[0].pv.size() > 1
			|| (!isNone(best_thread->root_moves[0].pv[0]) && best_thread->root_moves[0].extractPonderFromTT(root_board, ponder_candidate)))
			std::cout << " ponder " << toUSI(best_thread->root_moves[0].pv[1]);

		std::cout << SYNC_ENDL;
	}
}

void Thread::search()
{
	// (ss - 5) and (ss + 2)という参照を許すため
	Stack stack[MAX_PLY + 7], *ss = stack + 5;
	Score best_score, alpha, beta, delta;
	MainThread* main_thread = (this == Threads.main() ? Threads.main() : nullptr);
	Move easy_move = MOVE_NONE;

	std::memset(ss - 5, 0, 8 * sizeof(Stack));

	best_score = delta = alpha = -SCORE_INFINITE;
	beta = SCORE_INFINITE;
	completed_depth = DEPTH_ZERO;

	if (main_thread)
	{
		easy_move = EasyMove.get(root_board.key());
		EasyMove.clear();
		main_thread->easy_move_played = main_thread->failed_low = false;
		ponder_candidate = MOVE_NONE;
		main_thread->best_move_changes = 0;
		TT.newSearch();
	}

	while (++root_depth < DEPTH_MAX && !Signals.stop && (!Limits.depth || root_depth <= Limits.depth))
	{
		// ヘルパースレッドのための新しい深さを設定する。半密度行列というものを使う
		if (!main_thread)
		{
			const Row& row = HalfDensity[(idx - 1) % HalfDensitySize];

			if (row[(root_depth + root_board.ply()) % row.size()])
				continue;
		}

		// PV変動率を計算する
		if (main_thread)
			main_thread->best_move_changes *= 0.505, main_thread->failed_low = false;

		for (RootMove& rm : root_moves)
			rm.previous_score = rm.score;

		if (root_depth >= 5 * ONE_PLY)
		{
			delta = Score(60);
			alpha = std::max(root_moves[0].previous_score - delta, -SCORE_INFINITE);
			beta = std::min(root_moves[0].previous_score + delta, SCORE_INFINITE);
		}

		while (true)
		{
			best_score = ::search<PV>(root_board, ss, alpha, beta, root_depth, false);

			std::stable_sort(root_moves.begin(), root_moves.end());

			// 勝ちを見つけたら速攻指す
			if (best_score >= SCORE_MATE_IN_MAX_PLY || best_score >= SCORE_KNOWN_WIN && root_moves.size() == 1)
			{
				root_moves[0].score = SCORE_INFINITE;
				return;
			}

			if (Signals.stop)
				break;

			// 探索をする前にUIに出力する
			if (main_thread
				&& (best_score <= alpha || best_score >= beta)
				&& Time.elapsed() > 3000)
				SYNC_COUT << USI::pv(root_board, root_depth, alpha, beta) << SYNC_ENDL;

			// fail high/lowを起こしているならwindow幅を広げる
			if (best_score <= alpha)
			{
				beta = (alpha + beta) / 2;
				alpha = std::max(best_score - delta, -SCORE_INFINITE);

				if (main_thread)
				{
					// 安定していないので
					main_thread->failed_low = true;
					Signals.stop_on_ponderhit = false;
				}
			}
			else if (best_score >= beta)
			{
				alpha = (alpha + beta) / 2;
				beta = std::min(best_score + delta, SCORE_INFINITE);
			}
			else
				break;

			delta += delta / 4 + 5;

			assert(alpha >= -SCORE_INFINITE && beta <= SCORE_INFINITE);
		}

		if (main_thread)
		{
			if (Signals.stop)
				SYNC_COUT << "info nodes " << Threads.nodeSearched() << " time " << Time.elapsed() << SYNC_ENDL;
			else
				SYNC_COUT << USI::pv(root_board, root_depth, alpha, beta) << SYNC_ENDL;
		}

		if (!Signals.stop)
			completed_depth = root_depth;

		if (!main_thread)
			continue;

		if (main_thread->root_moves[0].pv.size() > 1)
			ponder_candidate = main_thread->root_moves[0].pv[1];

		// Have we found a "mate in x"?
		if (Limits.mate
			&& best_score >= SCORE_MATE_IN_MAX_PLY
			&& SCORE_MATE - best_score <= 2 * Limits.mate)
			Signals.stop = true;

		if (Limits.useTimeManagement())
		{
			if (!Signals.stop && !Signals.stop_on_ponderhit)
			{
				const int F[] = { main_thread->failed_low,
					best_score - main_thread->previous_score };

				int improving_factor = std::max(229, std::min(715, 357 + 119 * F[0] - 6 * F[1]));
				double unstable_pv_factor = 1 + main_thread->best_move_changes;

				bool do_easy_move = root_moves[0].pv[0] == easy_move
					&& main_thread->best_move_changes < 0.03
					&& Time.elapsed() > Time.optimum() * 5 / 42;

				if (root_moves.size() == 1
					|| Time.elapsed() > Time.optimum() * unstable_pv_factor * improving_factor / 628
					|| (main_thread->easy_move_played = do_easy_move))
				{
					if (Limits.ponder)
						Signals.stop_on_ponderhit = true;
					else
						Signals.stop = true;
				}
			}

			if (root_moves[0].pv.size() >= 3) // pvを保存し、安定性を見る
				EasyMove.update(root_board, root_moves[0].pv);
			else
				EasyMove.clear();
		}
	}

	if (!main_thread)
		return;

	if (EasyMove.stableCnt < 6 || main_thread->easy_move_played)
		EasyMove.clear();
}


namespace
{
	// depth : 残り深さ
	template <NodeType NT>
	Score search(Board& b, Stack* ss, Score alpha, Score beta, Depth depth, bool cut_node)
	{
		const bool PvNode = NT == PV;
		const bool rootNode = PvNode && (ss - 1)->ply == 0;

		assert(-SCORE_INFINITE <= alpha && alpha < beta && beta <= SCORE_INFINITE);
		assert(PvNode || (alpha == beta - 1));
		assert(DEPTH_ZERO < depth && depth < DEPTH_MAX);

		Move pv[MAX_PLY + 1], quiets_searched[QUIETS];
		Score eval, score;
		Move best_move, move;
		int move_count, quiet_count;
		StateInfo st;
		TTEntry* tte;

		// Step 1. ノード初期化
		Thread* this_thread = b.thisThread();
		const bool in_check = b.bbCheckers();
		Score best_score = -SCORE_INFINITE;
		ss->ply = (ss - 1)->ply + 1;
		move_count = quiet_count = ss->move_count = 0;
		ss->history = SCORE_ZERO;

		// checkTime()を呼ぶためのカウンタをリセットする
		if (this_thread->reset_calls.load(std::memory_order_relaxed))
		{
			this_thread->reset_calls = false;
			this_thread->calls_count = 0;
		}

		// 利用可能な残り時間のチェック
		if (++this_thread->calls_count > 4096)
		{
			for (Thread* th : Threads)
				th->reset_calls = true;

			checkTime();
		}

		// GUIへselDepth(現在、選択的に読んでいる手の探索深さ)情報を送信するために使用
		if (PvNode && this_thread->max_ply < ss->ply)
			this_thread->max_ply = ss->ply;

		if (!rootNode)
		{
			// Step 2. 引き分けか探索中断かをチェックする

			// 256手以上指していたら引き分け
			if (b.ply() >= 256)
				return SCORE_DRAW;

			// 宣言法により勝ち。重いので190手目以降に呼ぶことにする
			if (b.ply() >= 190 && b.isDeclareWin())
				return mateIn(ss->ply);

			switch (b.repetitionType(16))
			{
			case NO_REPETITION: if (!Signals.stop.load(std::memory_order_relaxed) && ss->ply < MAX_PLY) { break; }
			case REPETITION_DRAW: return drawScore(RootTurn, b.turn()); // ※↑のifに引っかからなかったらここに来る
			case REPETITION_WIN:  return mateIn(ss->ply);
			case REPETITION_SUPERIOR: return SCORE_MATE_IN_MAX_PLY;
			case REPETITION_LOSE:  return matedIn(ss->ply);
			case REPETITION_INFERIOR: return SCORE_MATED_IN_MAX_PLY;
			default: UNREACHABLE;
			}

			// Step 3. 詰み手数による枝刈り
			alpha = std::max(matedIn(ss->ply), alpha);
			beta = std::min(mateIn(ss->ply + 1), beta);

			if (alpha >= beta)
				return alpha;
		}

		assert(0 <= ss->ply && ss->ply < MAX_PLY);

		ss->current_move = (ss + 1)->excluded_move = best_move = MOVE_NONE;
		ss->counter_moves = nullptr;
		(ss + 1)->skip_early_pruning = false;
		(ss + 2)->killers[0] = (ss + 2)->killers[1] = MOVE_NONE;

		// Step 4. 置換表のlook up
		// singular extensionするべきノードかどうかを確かめるために、この局面の置換表の指し手を
		// 除外して探索するときはss->excluded_moveが存在し、その場合異なる局面のキーを用いる必要がある
		const Move excluded_move = ss->excluded_move;
#ifdef ENABLE_TT
		const Key key = b.key() ^ (Key(excluded_move) << 1);
		bool tt_hit = TT.probe(key, tte);
	
		// stockfishではdepth, bound, evalをローカルにコピーしていないが、それだとアクセス競合が怖いのでコピーする。
		// TODO:コピーをアトミックに行いたい。
		Move tt_move = rootNode ? this_thread->root_moves[0].pv[0] : tt_hit ? tte->move() : MOVE_NONE;
		Score tt_score = tt_hit ? scoreFromTT(tte->score(), ss->ply) : SCORE_NONE;
		Depth tt_depth = tt_hit ? tte->depth() : DEPTH_NONE;
		Bound tt_bound = tt_hit ? tte->bound() : BOUND_NONE;
		Score tt_eval = tt_hit ? tte->eval() : SCORE_NONE;

		// keyの0bit目は手番と同じになっていなければおかしい。
		assert(Turn(key & 1) == b.turn());
		
		// 読み込んでいる最中に別スレッドによって上書きされた。このとき、tteの内容は全く信用できない。
		if (tt_hit && (key >> 32) != tte->key())
		{
			tt_hit = false;
			tt_move = rootNode ? this_thread->root_moves[0].pv[0] : MOVE_NONE;
			tt_score = SCORE_NONE;
			tt_depth = DEPTH_NONE;
			tt_bound = BOUND_NONE;
			tt_eval = SCORE_NONE;
		}

#if 0 // シノニム
		else if (tt_hit && tt_move != MOVE_NONE && !b.pseudoLegal(tt_move))
		{
			tt_hit = false;
			tt_move = rootNode ? this_thread->root_moves[0].pv[0] : MOVE_NONE;
			tt_score = SCORE_NONE;
			tt_depth = DEPTH_NONE;
			tt_bound = BOUND_NONE;
			tt_eval = SCORE_NONE;
		}
#endif
#else
		const bool tt_hit = false;
		tte = nullptr;
		const Score tt_score = SCORE_NONE;
		const Move tt_move = MOVE_NONE;
		const Depth tt_depth = DEPTH_NONE;
		const Bound tt_bound = BOUND_NONE;
		const Score tt_eval = SCORE_NONE;
#endif
		// pvノードではないなら置換表を見て枝刈りする
		if (!PvNode
			&& tt_hit
			&& tt_depth >= depth
			&& tt_score != SCORE_NONE
			&& (tt_score >= beta ? (tt_bound & BOUND_LOWER) : (tt_bound & BOUND_UPPER)))
		{
			// historyのupdateはしないほうがいいようだ。
			if (tt_score >= beta 
				&& tt_move 
				&& !isCaptureOrPawnPromote(tt_move)
				&& tt_move != ss->killers[0])
			{
				ss->killers[1] = ss->killers[0];
				ss->killers[0] = tt_move;
			}

			return tt_score;
		}

		// Step 4a. Tablebase probe
		// Tablebaseはないので、代わりに詰みチェックを行う。
#ifdef MATE1PLY
		if (!rootNode 
			&& b.ply() >= MATE_PLY 
			&& !tt_hit 
			&& depth > ONE_PLY 
			&& !in_check)
		{
			best_move = b.mate1ply();

			if (best_move != MOVE_NONE)
			{
				// staticEvalはなんでもいい
				best_score = mateIn(ss->ply);
#ifdef ENABLE_TT
				tte->save(key, scoreToTT(best_score, ss->ply), BOUND_EXACT,
					DEPTH_MAX, best_move, SCORE_NONE, TT.generation());
#endif
				return best_score;
			}
		}	
#endif
		// 進行度を差分計算
		Prog::evaluateProgress(b);

		// Step 5. 王手ならすぐ指し手ループへ行く
		if (in_check)
		{
			ss->static_eval = eval = SCORE_NONE;
			goto moves_loop;
		}
		else if (tt_hit)
		{
			if ((ss->static_eval = eval = tt_eval) == SCORE_NONE)
				eval = ss->static_eval = evaluate(b);

			// tt_scoreが局面の評価値として使えるかどうか
			if ((tt_score != SCORE_NONE))
				if (tt_bound & (tt_score > eval ? BOUND_LOWER : BOUND_UPPER))
					eval = tt_score;
		}
		else
		{
#if defined USE_EVAL_TURN
			// 手番評価があるため、(ss-1)->static_evalは使えない。
			eval = ss->static_eval = evaluate(b);
#else
			eval = ss->static_eval = (ss - 1)->current_move != MOVE_NULL ? evaluate(b) : -(ss - 1)->static_eval;
#endif
#ifdef ENABLE_TT
			// 評価関数を呼び出したので置換表に登録しておく。この後すぐに枝刈りされるかもしれないのでこのタイミングがベスト
			tte->save(key, SCORE_NONE, BOUND_NONE, DEPTH_NONE, MOVE_NONE, eval, TT.generation());
#endif
		}

		// 枝刈りしない
		if (ss->skip_early_pruning)
			goto moves_loop;

		// Step 6. Razoring (skipped when in check)
		// 現局面がalpha値より下で、razor_marginを足しても上回りそうにないなら
		// 普通に探索してもalphaを上回らないと見て即座に評価値を返す
		if (!PvNode
			&& depth < 4 * ONE_PLY
			&& isNone(tt_move)
			&& eval + razor_margin[depth] <= alpha)
		{
			if (depth <= ONE_PLY)
				return qsearch<NO_PV, false>(b, ss, alpha, beta, DEPTH_ZERO);

			Score ralpha = alpha - razor_margin[depth];
			Score s = qsearch<NO_PV, false>(b, ss, ralpha, ralpha + 1, DEPTH_ZERO);

			if (s <= ralpha)
				return s;
		}

		// Step 7. Futility pruning : child node (skipped when in check)
		// 現局面がbetaを超えていて、相手がfutilityMargin(depth)分挽回してきたとしてもbetaを超えているのなら、
		// この局面は相当いいと思われるのでこれ以上探索しても意味がないと判断し、探索をスキップする
		if (!rootNode
			&& depth < 7 * ONE_PLY
			&& eval - futilityMargin(depth, b) >= beta
			&& eval < SCORE_KNOWN_WIN)
			return eval;

		// Step 8. Null move search with verification search (is omitted in PV nodes)
		// 現局面でbetaを超えているなら探索深さを減らしてパスをしてみて、
		// それでもbetaを超えているなら普通に指してもbetaを超えると推測できるので、探索をスキップする
		if (!PvNode
			&& eval >= beta
			&& (ss->static_eval >= beta - 35 * (depth / ONE_PLY - 6) || depth >= 13 * ONE_PLY))
		{
			ss->current_move = MOVE_NULL;
			ss->counter_moves = nullptr;
			assert(eval - beta >= 0);

#ifdef EVAL_DIFF
			if (b.state()->sum.isNotEvaluated())
				ss->static_eval = evaluate(b);
#endif
			// 深さとスコアに基づいて、動的に減らす探索深さを決める
			Depth R = ((823 + 67 * depth) / 256 + std::min((eval - beta) / PAWN_SCORE, Score(3))) * ONE_PLY;
			b.doNullMove(st);
			(ss + 1)->skip_early_pruning = true;
			Score null_score = depth - R < ONE_PLY ? -qsearch<NO_PV, false>(b, ss + 1, -beta, -beta + 1, DEPTH_ZERO)
												   :  -search<NO_PV       >(b, ss + 1, -beta, -beta + 1, depth - R, !cut_node);
			(ss + 1)->skip_early_pruning = false;
			b.undoNullMove();

			if (null_score >= beta)
			{
				if (null_score >= SCORE_MATE_IN_MAX_PLY)
					null_score = beta;

				if (depth < 12 * ONE_PLY && abs(beta) < SCORE_KNOWN_WIN)
					return null_score;

				// あまり早い段階での枝刈りは乱暴かもしれないので、Nullwindowで探索して、それでもbetaを超えているなら
				// 今度こそスキップする
				ss->skip_early_pruning = true;
				Score s = depth - R < ONE_PLY ? -qsearch<NO_PV, false>(b, ss, beta - 1, beta, DEPTH_ZERO)
											   : -search<NO_PV       >(b, ss, beta - 1, beta, depth - R, false);
				ss->skip_early_pruning = false;

				if (s >= beta)
					return null_score;
			}
		}

		// Step 9. ProbCut (skipped when in check)
		// もしとてもよい駒を取る手があり、かつ探索深さを減らした探索の戻り値がbetaを超えるなら、前の手は安全に枝刈りできる
		if (!PvNode
			&& depth >= 6 * ONE_PLY
			&& abs(beta) < SCORE_MATE_IN_MAX_PLY
			&& !isPawnPromote((ss - 1)->current_move)) // ただし直前の指し手が歩成りならカットせずに普通に探索したほうがいいかも。
		{
			Score rbeta = std::min(beta + 270, SCORE_INFINITE);
			Depth rdepth = depth - 4 * ONE_PLY;

			assert(rdepth >= ONE_PLY);
			assert(!isNone((ss - 1)->current_move));
			assert(!isNull((ss - 1)->current_move));

#ifdef EVAL_DIFF
			if (b.state()->sum.isNotEvaluated())
				ss->static_eval = evaluate(b);
#endif
			Score th = captureScore(capturePieceType((ss - 1)->current_move));

			if (isPromote((ss - 1)->current_move))
				th += promoteScore(movedPieceType((ss - 1)->current_move));

			// 一手前に取られた駒より価値の高い駒を取る手を優先して生成する
			MovePicker mp(b, tt_move, th);

			while (move = mp.nextMove())
				if (b.legal<true>(move)) // 駒を取る手なので駒打ちではないはず
				{
					assert(!isDrop(move));
					ss->current_move = move;
					ss->counter_moves = &this_thread->counter_move_history.refer(move);
					b.doMove(move, st, b.givesCheck(move));
					score = -search<NO_PV>(b, ss + 1, -rbeta, -rbeta + 1, rdepth, !cut_node);
					b.undoMove(move);

					if (score >= rbeta)
						return score;
				}
		}

#ifdef ENABLE_TT // 置換表がないのなら多重反復深化をする意味がない。
		// Step 10. Internal iteratiove deepening (skipped when in check)
		// 現局面から少しの得でbetaを超えそうなら、探索深さを減らして探索してみて、得られた手から探索する。
		if (depth >= (PvNode ? 5 * ONE_PLY : 8 * ONE_PLY)
			&& isNone(tt_move)
			&& (PvNode || (ss->static_eval + 256 >= beta)))
		{
			Depth d = depth - 2 * ONE_PLY - (PvNode ? DEPTH_ZERO : depth / 4);

			// TODO:IIDの結果得られたスコアを何かに利用できないだろうか。
			ss->skip_early_pruning = true;
			Score s = search<NT>(b, ss, alpha, beta, d, true);
			ss->skip_early_pruning = false;
			tt_hit = TT.probe(key, tte);
			tt_move = tt_hit ? tte->move() : MOVE_NONE;

			if (tt_hit && (key >> 32) != tte->key())
				tt_move = MOVE_NONE;
		}
#endif
	moves_loop: // 指し手生成ループ

		const CounterMoveStats* cmh = (ss - 1)->counter_moves;
		const CounterMoveStats* fmh = (ss - 2)->counter_moves;
		const CounterMoveStats* fm2 = (ss - 4)->counter_moves;

#ifdef EVAL_DIFF
		if (b.state()->sum.isNotEvaluated())
			evaluate(b);
#endif
		MovePicker mp(b, tt_move, depth, ss);

		score = best_score;

		// 2手前より評価値がよくなっているかどうか
		const bool improving =       ss->static_eval >= (ss - 2)->static_eval
							/*||       ss->static_eval == SCORE_NONE redundant condition*/
							|| (ss - 2)->static_eval == SCORE_NONE;

		const bool singular_extension_node = !rootNode
										&& depth >= 8 * ONE_PLY
										&& !isNone(tt_move)
										&& abs(tt_score) < SCORE_KNOWN_WIN
										&& !excluded_move
										&& (tt_bound & BOUND_LOWER)
										&& tt_depth >= depth - 3 * ONE_PLY;

		// Step 11. Loop through moves
		while (move = mp.nextMove())
		{
			assert(isOK(move));

			if (move == excluded_move)
				continue;

			// root nodeでは、rootMoves()の集合に含まれていない指し手は探索をスキップする
			if (rootNode && !std::count(this_thread->root_moves.begin(), this_thread->root_moves.end(), move))
				continue;
#if 0
			// 現在探索中の指し手、探索深さ、探索済みの手数を出力する
			if (rootNode && this_thread == Threads.main()/* && Time.elapsed() > 3000*/)
				SYNC_COUT << "info depth " << depth / ONE_PLY
				<< " currmove " << toUSI(move)
				<< " currmovenumber " << move_count
				<< " nodes " << b.nodeSearched() << SYNC_ENDL;
#endif
			if (PvNode)
				(ss + 1)->pv = nullptr;

			// この深さで探索し終わった手の数
			ss->move_count = ++move_count;

			const bool capture_or_pawn_promotion = isCaptureOrPawnPromote(move);
			const bool gives_check = b.givesCheck(move);
			const bool move_count_pruning = depth < 16 * ONE_PLY && move_count >= FutilityMoveCounts[improving][depth];
			Depth extension = DEPTH_ZERO;

			// Step 12. 王手延長
			// 王手でしかも駒得なら延長して探索する価値あり
			if (gives_check
				&& !move_count_pruning
				&& b.seeGe(move, SCORE_ZERO))
					extension = ONE_PLY;

			// singular extension search
			// ここでのsearchは純粋に延長する/しないだけを求めたいのであり、historyのupdateはしたくない。
			else if (singular_extension_node
				&& move == tt_move
				&& b.legal(move))
			{
				Score rbeta = tt_score - 16 * depth / ONE_PLY;
				ss->excluded_move = move;

				// このノード自体もskip_early_pruning中かもしれない。このフラグはupdateStatsで使うので大事。
				bool save = ss->skip_early_pruning;

				ss->skip_early_pruning = true;
				score = search<NO_PV>(b, ss, rbeta - 1, rbeta, depth / 2, cut_node);
				ss->skip_early_pruning = save;
				ss->excluded_move = MOVE_NONE;

				// 上の処理で壊れたので復活。
				ss->move_count = move_count;

				if (score < rbeta)
					extension = ONE_PLY;
			}

			Depth new_depth = depth - ONE_PLY + extension;

			// Step 13. Pruning at shallow depth
			if (!rootNode
				&& !in_check
				&& best_score > SCORE_MATED_IN_MAX_PLY)
			{
				if (!capture_or_pawn_promotion && !gives_check)
				{		
					if (move_count_pruning)
						continue;

					// History based pruning
					// 枝刈りの条件はかなり厳しくしている。
					if (depth < 4 * ONE_PLY
						&& move != ss->killers[0]
						&& this_thread->history.value(move) < SCORE_ZERO
						&& (cmh && cmh->value(move) < SCORE_ZERO)
						&& (fmh && fmh->value(move) < SCORE_ZERO)
						&& (fm2 && fm2->value(move) < SCORE_ZERO))
						continue;

					Depth predicted_depth = std::max(new_depth - reduction<PvNode>(improving, depth, move_count), DEPTH_ZERO);

					assert(predicted_depth >= 0);

					// GameResult 624 - 40 - 652
					if (predicted_depth < 7 * ONE_PLY && ss->static_eval + futilityMargin(predicted_depth, b) + 256 <= alpha)
						continue;

					if (predicted_depth < 4 * ONE_PLY && !b.seeGe(move, SCORE_ZERO))
						continue;
				}
				
				// とても浅い残り探索深さにおける王手や駒をとる手のSEE値が負なら枝刈してしまう。
				else if (depth < 3 * ONE_PLY && 
					(mp.seeSign() < 0 || (!mp.seeSign() && !b.seeGe(move, SCORE_ZERO))))
					continue;
			}

#ifdef ENABLE_TT
			prefetch(TT.firstEntry(b.afterKey(move)));
#endif
			// 指し手が本当に合法かどうかのチェック(ルートならチェック済みなので要らない)
			if (!rootNode && !b.legal(move))
			{
				ss->move_count = --move_count;
				continue;
			}

			// この深さでの(現在探索中の)指し手
			ss->current_move = move;
			ss->counter_moves = &this_thread->counter_move_history.refer(move);

			// Step 14. 指し手で局面を進める
			b.doMove(move, st, gives_check);

			bool do_full_depth_search;
		
			// Step 15. 探索深さを減らす(LMR) もしfail highならフル探索深さで探索する
			if (depth >= 3 * ONE_PLY
				&& move_count > 1
				&& (!capture_or_pawn_promotion || move_count_pruning))
			{
				Depth r = reduction<PvNode>(improving, depth, move_count);
				
				if (!capture_or_pawn_promotion)
				{
					if (!PvNode && cut_node)
						r += ONE_PLY;

					ss->history = this_thread->history.value(move)
						+ (cmh ? cmh->value(move) : SCORE_ZERO)
						+ (fmh ? fmh->value(move) : SCORE_ZERO)
						+ (fm2 ? fm2->value(move) : SCORE_ZERO)
						+ this_thread->from_to_history.get(~b.turn(), move)
						- 8000;

					if (ss->history > SCORE_ZERO && (ss - 1)->history < SCORE_ZERO)
						r -= ONE_PLY;

					else if (ss->history < SCORE_ZERO && (ss - 1)->history > SCORE_ZERO)
						r += ONE_PLY;

					r = std::max(DEPTH_ZERO, (r - ss->history / 20000) * ONE_PLY);
				}
				else if (r) // 価値が高い可能性が高いので深くする。
					r -= ONE_PLY;

				Depth d = std::max(new_depth - r, ONE_PLY);
				score = -search<NO_PV>(b, ss + 1, -(alpha + 1), -alpha, d, true);
				do_full_depth_search = (score > alpha && r != DEPTH_ZERO);
			}
			else			
				do_full_depth_search = !PvNode || move_count > 1;

			// Step 16. フル探索。when LMR skipped or fails high 
			// Nullwindow探索をしてみて、alpha < score < betaでなければ通常の探索をスキップしてもよい
			if (do_full_depth_search)
				score = new_depth < ONE_PLY ?
				gives_check ? -qsearch<NO_PV,  true>(b, ss + 1, -(alpha + 1), -alpha, DEPTH_ZERO)
							: -qsearch<NO_PV, false>(b, ss + 1, -(alpha + 1), -alpha, DEPTH_ZERO)
							: - search<NO_PV       >(b, ss + 1, -(alpha + 1), -alpha, new_depth, !cut_node);

			if (PvNode && (move_count == 1 || (score > alpha && (rootNode || score < beta))))
			{
				(ss + 1)->pv = pv;
				(ss + 1)->pv[0] = MOVE_NONE;

				// 残り深さがないならqsearchを呼び出す
				score = new_depth < ONE_PLY ?
					gives_check ? -qsearch<PV,  true>(b, ss + 1, -beta, -alpha, DEPTH_ZERO)
								: -qsearch<PV, false>(b, ss + 1, -beta, -alpha, DEPTH_ZERO)
								: - search<PV       >(b, ss + 1, -beta, -alpha, new_depth, false);
			}

			// Step 17. 局面を戻す
			b.undoMove(move);

			assert(score > -SCORE_INFINITE && score < SCORE_INFINITE);

			// 探索終了したけどstopの時はsearchの戻り値は信用できないので置換表もpvも更新せずに戻る
			if (Signals.stop.load(std::memory_order_relaxed))
				return SCORE_ZERO;

			// Step 18. bestmoveのチェック
			if (rootNode) 
			{
				RootMove& rm = *std::find(this_thread->root_moves.begin(), this_thread->root_moves.end(), move);

				if (move_count == 1 || score > alpha)
				{
					rm.score = score;
					rm.pv.resize(1);

					assert((ss + 1)->pv);

					// pv構築
					for (Move* m = (ss + 1)->pv; *m != MOVE_NONE; ++m)
						rm.pv.push_back(*m);

					// どれくらいの頻度で最善手が変わったかを記憶しておく
					// これは時間制御に使われ、最善手が変わる頻度が高いほど追加で思考時間が与えられる
					if (move_count > 1 && this_thread == Threads.main())
						++static_cast<MainThread*>(this_thread)->best_move_changes;
				}
				else
					rm.score = -SCORE_INFINITE;
			}

			if (score > best_score)
			{
				best_score = score;

				if (score > alpha)
				{
					if (PvNode
						&& this_thread == Threads.main()
						&& EasyMove.get(b.key())
						&& (move != EasyMove.get(b.key()) || move_count > 1))
						EasyMove.clear();

					best_move = move;

					if (PvNode && !rootNode) // Update pv even in fail-high case
						updatePv(ss->pv, move, (ss + 1)->pv);

					if (PvNode && score < beta)
						alpha = score;

					else
					{
						// scoreがbetaを超えてたら一手前のループでbreakしてるはず
						assert(score >= beta);

						// beta cut
						break;
					}
				}
			}

			// 成りでも駒取りでもない手は、おとなしいquietな手
			if (!capture_or_pawn_promotion && move != best_move && quiet_count < QUIETS)
				quiets_searched[quiet_count++] = move;
		}

		// 一手も指されなかったということは、引き分けか詰み
		if (!move_count)
			best_score = excluded_move ? alpha
									   : in_check ? matedIn(ss->ply) : drawScore(RootTurn, b.turn());

		// quietな手が最善だった。統計を更新する
		else if (best_move && !isCaptureOrPawnPromote(best_move)) 
			updateStats(b, ss, best_move, depth, quiets_searched, quiet_count);

		// 一手前の指し手がfail lowを引き起こしたことによるボーナス
		// PVノードではこのsearch()の呼び出し元でbeta cutが起きてupdateStatsが呼ばれるので、ここでhistoryのupdateを行ってしまうと
		// 2重にupdateすることになってしまう。なので!PvNodeが正しい。
		else if (!PvNode
			&& depth >= 3 * ONE_PLY
			&& best_move == MOVE_NONE
			&& !isCaptureOrPawnPromote((ss - 1)->current_move) // stockfishだと!capturePieceだけど歩のpromoteもだめなはず
			&& isOK((ss - 1)->current_move))
		{
			// bonusとしてmove_countを足してやるとちょっとだけいい感じ。多分、指し手の後ろのほうで試された手を前に持ってきやすくなるのが理由。
			Score bonus = Score((depth / ONE_PLY) * (depth / ONE_PLY) + depth / ONE_PLY - 1) + (ss - 1)->move_count;
			updateCmStats(ss - 1, (ss - 1)->current_move, bonus);
		}
#ifdef ENABLE_TT
		tte->save(key, scoreToTT(best_score, ss->ply),
			best_score >= beta ? BOUND_LOWER :
			PvNode && best_move ? BOUND_EXACT : BOUND_UPPER,
			depth, best_move, ss->static_eval, TT.generation());

		assert(best_score > -SCORE_INFINITE && best_score < SCORE_INFINITE);
#endif
		return best_score;
	}

	template<NodeType NT, bool InCheck>
	Score qsearch(Board& b, Stack* ss, Score alpha, Score beta, Depth depth)
	{
		const bool PvNode = NT == PV;

		assert(InCheck == b.inCheck());
		assert(alpha >= -SCORE_INFINITE && alpha < beta && beta <= SCORE_INFINITE);
		assert(PvNode || alpha == beta - 1);
		assert(depth <= DEPTH_ZERO);

		Move pv[MAX_PLY + 1], best_move;
		Score old_alpha;

		if (PvNode)
		{
			old_alpha = alpha; // To flag BOUND_EXACT when eval above alpha and no available moves
			(ss + 1)->pv = pv;
			ss->pv[0] = MOVE_NONE;
		}

		ss->current_move = best_move = MOVE_NONE;
		ss->ply = (ss - 1)->ply + 1;

		if (b.ply() >= 190 && b.isDeclareWin())
			return mateIn(ss->ply);

		switch (b.repetitionType(16))
		{
		case NO_REPETITION:		  if (ss->ply < MAX_PLY) { break; }
		case REPETITION_DRAW: return ss->ply >= MAX_PLY && !InCheck ? evaluate(b) : drawScore(RootTurn, b.turn()); // ※↑のifに引っかからなかったらここに来る
		case REPETITION_WIN:  return mateIn(ss->ply);
		case REPETITION_SUPERIOR: return SCORE_MATE_IN_MAX_PLY;
		case REPETITION_LOSE:  return matedIn(ss->ply);
		case REPETITION_INFERIOR: return SCORE_MATED_IN_MAX_PLY;
		default: UNREACHABLE;
		}

		assert(0 <= ss->ply && ss->ply < MAX_PLY);

		// 王手を含めるかどうかを決定する。また使用しようとしているTTエントリの深さのタイプを修正する
		Depth qdepth = (InCheck || depth >= DEPTH_QS_CHECKS) ? DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;

#ifdef ENABLE_TT
		TTEntry* tte;
		const Key key = b.key();
		bool tt_hit = TT.probe(key, tte);
		Move tt_move = tt_hit ? tte->move() : MOVE_NONE;
		Score tt_score = tt_hit ? scoreFromTT(tte->score(), ss->ply) : SCORE_NONE;
		Depth tt_depth = tt_hit ? tte->depth() : DEPTH_NONE;
		Bound tt_bound = tt_hit ? tte->bound() : BOUND_NONE;
		Score tt_eval = tt_hit ? tte->eval() : SCORE_NONE;

		// 読み込んでいる最中に別スレッドによって上書きされた。このとき、tteの内容は全く信用できない。
		if (tt_hit && (key >> 32) != tte->key())
		{
			tt_hit = false;
			tt_move = MOVE_NONE;
			tt_score = SCORE_NONE;
			tt_depth = DEPTH_NONE;
			tt_bound = BOUND_NONE;
			tt_eval = SCORE_NONE;
		}
#if 0 // シノニム対策
		else if (tt_hit && tt_move != MOVE_NONE && !b.pseudoLegal(tt_move))
		{
			tt_hit = false;
			tt_move = MOVE_NONE;
			tt_score = SCORE_NONE;
			tt_depth = DEPTH_NONE;
			tt_bound = BOUND_NONE;
			tt_eval = SCORE_NONE;
		}
#endif
#else
		const TTEntry* tte = nullptr;
		const bool tt_hit = false;
		const Move tt_move = MOVE_NONE;
		const Score tt_score = SCORE_NONE;
		const Depth tt_depth = DEPTH_NONE;
		const Bound tt_bound = BOUND_NONE;
		const Score tt_eval = SCORE_NONE;
#endif
		
		// 置換表のほうが読みが深いなら置換表の評価値を返す
		if (!PvNode
			&& tt_hit
			&& tt_depth >= qdepth
			&& tt_score != SCORE_NONE
			&& (tt_score >= beta ? (tt_bound & BOUND_LOWER)
				: (tt_bound & BOUND_UPPER)))
		{
			ss->current_move = tt_move;
			return tt_score;
		}

		// 進行度を差分計算
		Prog::evaluateProgress(b);

		Score score, best_score, futility_base;

		// 評価関数を呼び出して現局面の評価値を得ておく
		if (InCheck)
		{
			ss->static_eval = SCORE_NONE;
			best_score = futility_base = -SCORE_INFINITE;
		}
		else
		{
			if (tt_hit)
			{
				// 置換表に評価値が入っていない
				if ((ss->static_eval = best_score = tt_eval) == SCORE_NONE)
					ss->static_eval = best_score = evaluate(b);

				// 置換表のスコア（評価値ではない)がこの局面の評価値より使えるかどうか？
				if (tt_score != SCORE_NONE)
					if (tt_bound & (tt_score > best_score ? BOUND_LOWER : BOUND_UPPER))
						best_score = tt_score;
			}
			else
			{
#if defined USE_EVAL_TURN
				ss->static_eval = best_score = evaluate(b); 
#else
				ss->static_eval = best_score = (ss - 1)->current_move != MOVE_NULL ? evaluate(b) : -(ss - 1)->static_eval;
#endif
			}

			// stand pat。評価値が少なくともbetaよりも大きいのであればすぐに戻る
			if (best_score >= beta)
			{
#ifdef ENABLE_TT
				if (!tt_hit) // 置換表に値がないのなら登録
					tte->save(key, scoreToTT(best_score, ss->ply), BOUND_LOWER,
						DEPTH_NONE, MOVE_NONE, ss->static_eval, TT.generation());
#endif
				return best_score;
			}

			if (b.ply() >= MATE_PLY)
			{
				best_move = b.mate1ply();

				if (best_move != MOVE_NONE)
				{
					best_score = mateIn(ss->ply + 1);
#ifdef ENABLE_TT
					tte->save(key, scoreToTT(best_score, ss->ply), BOUND_EXACT,
						DEPTH_MAX, best_move, SCORE_NONE, TT.generation());
#endif
					return best_score;
				}
			}

			if (PvNode && best_score > alpha)
				alpha = best_score;

			futility_base = best_score + 128;
		}

#ifdef EVAL_DIFF
		if (b.state()->sum.isNotEvaluated())
			evaluate(b);
#endif

		// 探索深さが0以下なのでRecaptureか成り、depthがDEPTH_QS_CHECKより大きい場合は王手を生成する
		MovePicker mp(b, tt_move, depth, (ss - 1)->current_move);
		Move move;
		StateInfo si;

		while (move = mp.nextMove())
		{
			const bool gives_check = b.givesCheck(move);

			// Futility pruning
			if (!InCheck
				&& !gives_check
				&& futility_base > -SCORE_KNOWN_WIN)
			{
				// もしこの手で取れる駒がただ取りだとして、それでもalphaに満たないならば読む価値が薄い
				Score futility_score = futility_base + captureScore(capturePieceType(move));

				if (isPromote(move))
					futility_score += promoteScore(movedPieceType(move));

				if (futility_score <= alpha)
				{
					best_score = std::max(best_score, futility_score);
					continue;
				}

				if (futility_base <= alpha && !b.seeGe(move, Score(1)))
				{
					best_score = std::max(best_score, futility_base);
					continue;
				}
			}

			const bool evasionPrunable = InCheck
									  && best_score > SCORE_MATED_IN_MAX_PLY
									  && !isCapture(move);

			// 王手されていない時や詰まされる時は、QUIETな手やSEEが-の手は探索しない
			if ((!InCheck || evasionPrunable)
				&& !isPawnPromote(move)
				&& !b.seeGe(move, SCORE_ZERO))
				continue;

#ifdef ENABLE_TT
			prefetch(TT.firstEntry(b.afterKey(move)));
#endif
			ss->current_move = move;

			if (!b.legal(move))
				continue;

			b.doMove(move, si, gives_check);

			score = gives_check ? -qsearch<NT,  true>(b, ss + 1, -beta, -alpha, depth - ONE_PLY)
								: -qsearch<NT, false>(b, ss + 1, -beta, -alpha, depth - ONE_PLY);
			b.undoMove(move);

			assert(score > -SCORE_INFINITE && score < SCORE_INFINITE);

			// Check for a new best move
			if (score > best_score)
			{
				best_score = score;

				if (score > alpha)
				{
					if (PvNode) // Update pv even in fail-high case
						updatePv(ss->pv, move, (ss + 1)->pv);

					if (PvNode && score < beta) // Update alpha here!
					{
						alpha = score;
						best_move = move;
					}
					else // Fail high
					{
#ifdef ENABLE_TT
						tte->save(key, scoreToTT(score, ss->ply), BOUND_LOWER,
							qdepth, move, ss->static_eval, TT.generation());
#endif
						return score;
					}
				}
			}
		}

		if (InCheck && best_score == -SCORE_INFINITE)
			return matedIn(ss->ply);

#ifdef ENABLE_TT
		tte->save(key, scoreToTT(best_score, ss->ply),
			PvNode && best_score > old_alpha ? BOUND_EXACT : BOUND_UPPER,
			qdepth, best_move, ss->static_eval, TT.generation());
#endif
		assert(best_score > -SCORE_INFINITE && best_score < SCORE_INFINITE);

		return best_score;
	}

	// 利用可能時間を過ぎて探索している場合、Signals.stopをtrueにして探索を中止する
	void checkTime()
	{
		// checkTime()が一番最初に呼び出されたときの時間
		static TimePoint last_info_time = now();

		// "go"が送られてきてからの経過時間
		int elapsed = Time.elapsed();

		// 現在時刻
		TimePoint tick = Limits.start_time + elapsed;

		if (tick - last_info_time >= 1000)
			last_info_time = tick;

		// ponder中は停止しない
		if (Limits.ponder)
			return;

		if ((!Limits.move_time && Limits.useTimeManagement() && elapsed > Time.maximum() - 10)
			|| (Limits.move_time && elapsed >= Limits.move_time) // 一あたりの探索時間が決まっているなら探索時間を比較
			|| (Limits.nodes && Threads.nodeSearched() >= Limits.nodes)) // 探索局面数による打ち切りが有効なら探索局面数を比較
			Signals.stop = true;
	}

	// ルートからの手数による詰みのスコアを、現局面からの手数による詰みのスコアに調整する
	// 詰みのスコアでなければ何も変更しない。この関数は置換表に登録する前に呼ばれる
	Score scoreToTT(Score s, int ply)
	{
		assert(s != SCORE_NONE);

		return  s >= SCORE_MATE_IN_MAX_PLY  ? s + ply
			  : s <= SCORE_MATED_IN_MAX_PLY ? s - ply : s;
	}

	// ↑の関数と逆のことをする。現局面からの手数による詰みのスコアを、ルートからの手数による詰みのスコアに調整する
	Score scoreFromTT(Score s, int ply)
	{
		return  s == SCORE_NONE ? SCORE_NONE
			  : s >= SCORE_MATE_IN_MAX_PLY  ? s - ply
			  : s <= SCORE_MATED_IN_MAX_PLY ? s + ply : s;
	}

	// 現在の指し手をpvに加え、その指し手の後のpvを現在のpvに加える
	void updatePv(Move* pv, Move move, Move* childPv)
	{
		for (*pv++ = move; childPv && !isNone(*childPv); )
			*pv++ = *childPv++;

		*pv = MOVE_NONE;
	}

	void updateCmStats(Stack * ss, Move m, Score bonus)
	{
		CounterMoveStats* cmh = (ss - 1)->counter_moves;
		CounterMoveStats* fmh = (ss - 2)->counter_moves;
		CounterMoveStats* fm2 = (ss - 4)->counter_moves;

		if (cmh)
			cmh->update(m, bonus);

		if (fmh)
			fmh->update(m, bonus);

		if (fm2)
			fm2->update(m, bonus);
	}

	// 統計情報を更新する
	// 現局面に対してβカットが起こり、なおかつ最善手がquietな手だったときに呼び出される
	void updateStats(const Board& b, Stack* ss, Move move, Depth depth, Move* quiets, int quiet_cnt)
	{
		// skip_early_pruningなnodeは、一つ前の階層のsearchから局面をdoMoveせずに探索深さを浅くしただけのものであり、
		// そこでbeta cutを引き起こした手は呼び出し元のnodeで試すキラー手としての価値が薄いので、
		// もともと試すべきだったkiller手を追い出してまでkillerとして登録するべきではない。
		if (ss->skip_early_pruning)
		{
			// ただしkillerがないなら登録したほうがよい。
			// ss->killers[1]が空いているならそこに登録したほうがいいようにも思えるが、実験してみると少し弱かった。
			if (ss->killers[0] == MOVE_NONE)
				ss->killers[0] = move;

			// skip_early_pruningなnodeでhistoryを更新しても、このsearch()の呼び出し元のnodeの指し手オーダリングに
			// よい影響を与えないと思われるので、これ以上何もせずに戻る。
			return;
		}

		// killerがなければ、キラーを追加
		if (ss->killers[0] != move)
		{
			ss->killers[1] = ss->killers[0];
			ss->killers[0] = move;
		}

		// 与えるべきボーナスは、後のほうで試された手ほど大きいものとする。move_countがボーナス値として都合がいい。
		Score base = Score((depth / ONE_PLY) * (depth / ONE_PLY) + depth / ONE_PLY - 1);
		Score bonus = base + Score(ss->move_count);
		Turn t = b.turn();

		// ローカルコピー
		const Move prev_move = (ss - 1)->current_move;

		Thread* this_thread = b.thisThread();

		// この指し手がbeta cutを引き起こした（または最善手だった）ので、ボーナスを与える
		this_thread->history.update(move, bonus);
		this_thread->from_to_history.update(t, move, bonus);

		updateCmStats(ss, move, bonus);

		// 前回指された手がパスではないなら
		// 前回の手に対するカウンター手として今回の指し手と、そのボーナスを追加
		if ((ss - 1)->counter_moves)
			this_thread->counter_moves.update(prev_move, move);

		// 他の静かな手は減点
		for (int i = 0; i < quiet_cnt; ++i)
		{
			// history値の高い指し手ほど、ペナルティは大きくする
			Score penalty = -(base + Score(quiet_cnt - i));

			this_thread->history.update(quiets[i], penalty);
			this_thread->from_to_history.update(t, quiets[i], penalty);
			updateCmStats(ss, quiets[i], penalty);
		}

		// 前回の指し手がquietならペナルティを与える
		if (isOK(prev_move) && !isCaptureOrPawnPromote(prev_move))
		{
			Score penalty = Score(-base - 4 * (depth + 1) / ONE_PLY) + Score((ss - 1)->move_count);
			updateCmStats(ss - 1, prev_move, penalty);
		}
	}

	Score drawScore(Turn root_turn, Turn t)
	{
		return t == root_turn ? DrawScore : -DrawScore;
	}
} // namespace

// ponder moveを何も考えていないときに探索終了要求がきたらなんとかしてTTからponder moveを返すように努力する
bool Search::RootMove::extractPonderFromTT(Board & b, Move weak_ponder)
{
	StateInfo st;
	TTEntry* tte;
	assert(pv.size() == 1);

	b.doMove(pv[0], st);

	Move m = TT.probe(b.key(), tte) ? tte->move() : weak_ponder;
	const bool contains = MoveList<LEGAL>(b).contains(m);

	b.undoMove(pv[0]);

	if (contains)
		pv.push_back(m);

	return contains;
}

// GUIにpvやスコアを表示する
std::string USI::pv(const Board & b, Depth depth, Score alpha, Score beta)
{
	std::stringstream ss;
	int elapsed = Time.elapsed() + 1;
	const std::vector<RootMove>& root_moves = b.thisThread()->root_moves;

	uint64_t nodes_searched = Threads.nodeSearched();

	Depth d = depth;
	Score s = root_moves[0].score;

	if (ss.rdbuf()->in_avail()) // Not at first line
		ss << "\n";

	ss << "info"
		<< " depth " << d / ONE_PLY
		<< " seldepth " << b.thisThread()->max_ply
		<< " score " << USI::score(root_moves[0].score);

	ss << (s >= beta ? " lowerbound" : s <= alpha ? " upperbound" : "");

	ss << " nodes " << nodes_searched
		<< " nps " << nodes_searched * 1000 / elapsed;

	if (elapsed > 1000)
		ss << " hashfull " << TT.hashfull();

	ss << " time " << elapsed
		<< " pv";
	for (Move m : root_moves[0].pv)
		ss << " " << toUSI(m);

	return ss.str();
}

#if defined LEARN || defined GENSFEN
namespace Learn
{
	void initLearn(Board& b)
	{
		// クリアは本当は必要なんだろうけど、時間がかかる。
		//Search::clear();

		auto& limits = USI::Limits;
		limits.infinite = true;
		
		auto th = b.thisThread();

		if (!th)
			std::cout << "thがnull" << std::endl;

		th->max_ply = 0;

		auto& root_moves = th->root_moves;
		root_moves.clear();

		for (auto m : MoveList<LEGAL>(b))
			root_moves.push_back(Search::RootMove(m));

		assert(root_moves.size());

		th->root_depth = DEPTH_ZERO;
	}

	std::pair<Score, std::vector<Move>> search(Board& b, Score alpha, Score beta, int depth)
	{
		Stack stack[MAX_PLY + 7], *ss = stack + 5;
		memset(ss - 5, 0, 8 * sizeof(Stack));

		initLearn(b);
		Score best_score, delta;
		best_score = delta = -SCORE_INFINITE;

		auto th = b.thisThread();
		auto& root_depth = th->root_depth;
		auto& root_moves = th->root_moves;

		while (++root_depth <= depth)
		{
			if (root_depth >= 5)
			{
				delta = Score(18);
				alpha = std::max(root_moves[0].previous_score - delta, -SCORE_INFINITE);
				beta = std::min(root_moves[0].previous_score + delta, SCORE_INFINITE);
			}

			while (true)
			{
				best_score = ::search<PV>(b, ss, alpha, beta, root_depth, false);
				std::stable_sort(root_moves.begin(), root_moves.end());

				if (best_score <= alpha)
				{
					beta = (alpha + beta) / 2;
					alpha = std::max(best_score - delta, -SCORE_INFINITE);
				}
				else if (best_score >= beta)
				{
					alpha = (alpha + beta) / 2;
					beta = std::min(best_score + delta, SCORE_INFINITE);
				}
				else
				{
					break;
				}

				delta += delta / 4 + 5;

				assert(-SCORE_INFINITE <= alpha && beta <= SCORE_INFINITE);
			}
		}

		std::vector<Move> pvs;

		for (Move move : root_moves[0].pv)
		{
			if (!isOK(move))
				break;

			pvs.push_back(move);
		}

		return std::pair<Score, std::vector<Move>>(best_score, pvs);
	}

	std::pair<Score, std::vector<Move>> qsearch(Board& b, Score alpha, Score beta)
	{
		Stack stack[MAX_PLY + 7], *ss = stack + 5;
		memset(ss - 5, 0, 8 * sizeof(Stack));

		Move pv[MAX_PLY + 1];
		ss->pv = pv;

		initLearn(b);
	
		auto th = b.thisThread();
		const bool in_check = b.inCheck();
		Score best_score = in_check ? ::qsearch<PV, true>(b, ss, alpha, beta, DEPTH_ZERO)
									: ::qsearch<PV, false>(b, ss, alpha, beta, DEPTH_ZERO);
			
		std::vector<Move> pvs;

		for (Move* p = &ss->pv[0]; isOK(*p); ++p)
			pvs.push_back(*p);

		return std::pair<Score, std::vector<Move>>(best_score, pvs);
	}
	
} // namespace Learn

#endif