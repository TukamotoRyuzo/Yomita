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
#if 1
#define FRONT_PRUNE
#define EXTENSION
#define REDUCTION
#endif
using namespace Eval;
using namespace USI;
using namespace Search;
StateStackPtr Search::setup_status;

namespace
{
    // nullwindow探索をしていないときはPV
    enum NodeType { NO_PV, PV, };

    // d手で挽回可能な点数
    Score futilityMargin(Depth d, double p)
    {
#ifdef GENSFEN
        // 棋譜生成時は大きめにしとく。
        return Score((int)(150 + 250 * p) * (d / ONE_PLY));
#else
#ifdef USE_PROGRESS
        return Score((int)(50 + 150 * p) * (d / ONE_PLY));
#else
        return Score((70 + b.ply() * 2) * (d / ONE_PLY));
#endif
#endif
    }

    constexpr int QT = 64;

    // Futility and reductions lookup tables, initialized at startup
    int FutilityMoveCounts[2][16];  // [improving][depth]
    int Reductions[2][2][64][QT]; // [pv][improving][depth][moveNumber]

    // Threshold used for countermoves based pruning
    const int CounterMovePruneThreshold = 0;

    // Sizes and phases of the skip-blocks, used for distributing search depths across the threads.
    static int skipsize[20] = { 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };
    static int phase[20] = { 0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 7 };

    // Razoring and futility margin based on depth
    const int razor_margin[4] = { 0, 570, 603, 721 };

    template <bool PvNode> Depth reduction(bool i, Depth d, int mn)
    {
        return Reductions[PvNode][i][std::min(d / ONE_PLY, 63)][std::min(mn, QT - 1)] * ONE_PLY;
    }

    int statBonus(Depth depth)
    {
        int d = depth / ONE_PLY;
        return d > 17 ? 0 : d * d + 2 * d - 2;
    }

    template <NodeType NT>
    Score search(Board& b, Stack* ss, Score alpha, Score beta, Depth depth, bool cut_node, bool skip_early_pruning = false);

    template <NodeType NT, bool InCheck>
    Score qsearch(Board& b, Stack* ss, Score alpha, Score beta, Depth depth = DEPTH_ZERO);

    Score scoreToTT(Score s, int ply);
    Score scoreFromTT(Score s, int ply);
    Score drawScore(Turn root_turn, Turn t);
    void updatePv(Move* pv, Move move, Move* childPv);
    void updateCmStats(Stack* ss, Move m, int bonus);
    void updateStats(const Board& b, Stack* ss, Move move, Depth depth, Move* quiets, int quiet_cnt, bool skip_early_pruning);
    void checkTime();
    
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

    Move WeakPonder;
    EasyMoveManager EasyMove;
    Turn RootTurn;
    Score DrawScore;
} // namespace

void Search::init()
{
    DrawScore = Score((int)Options["Draw_Score"]);

    for (int imp = 0; imp <= 1; ++imp)
        for (int d = 1; d < 64; ++d)
            for (int mc = 1; mc < QT; ++mc)
            {
                double r = log(d) * log(mc) / 1.95;

                Reductions[NO_PV][imp][d][mc] = int(std::round(r));
                Reductions[PV][imp][d][mc] = std::max(Reductions[NO_PV][imp][d][mc] - 1, 0);

                // Increase reduction for non-pv nodes when eval is not improving
                if (!imp && Reductions[NO_PV][imp][d][mc] >= 2)
                    Reductions[NO_PV][imp][d][mc]++;
            }

    for (int d = 0; d < 16; ++d)
    {
        FutilityMoveCounts[0][d] = int(7.4 + 0.773 * pow(d + 0.00, 2.5));
        FutilityMoveCounts[1][d] = int(7.9 + 1.045 * pow(d + 0.49, 2.5));
    }
}

void Search::clear()
{
    if (Options["UseBook"])
        Book.read(Options["BookDir"]);

    TT.clear();

    for (Thread* th : Threads)
    {
        th->history.clear();
        th->counter_moves.clear();
        th->counter_move_history.clear();
        CounterMoveStats& cm = *th->counter_move_history.refer();
        int *t = cm.refer();
        std::fill(t, t + sizeof(cm) / sizeof(int), CounterMovePruneThreshold - 1);
    }

    Threads.main()->previous_score = SCORE_INFINITE;
    DrawScore = Score((int)Options["Draw_Score"]);
}

void MainThread::search()
{
    bool declare_win = false, book_hit = false;
    Turn t = RootTurn = root_board.turn();
    Time.init(Limits, t, root_board.ply());

    // 合法手がない == 詰んでいる局面もしくはステイルメイト。将棋は両方負けである。
    if (root_moves.empty())
    {
        root_moves.push_back(RootMove(MOVE_NONE));
        SYNC_COUT << "info depth 0 score " << USI::score(-SCORE_MATE) << SYNC_ENDL;
    }

    // 宣言勝ち
    else if (root_board.isDeclareWin())
    {
        declare_win = true;
        SYNC_COUT << "info depth 0 score " << USI::score(SCORE_MATE) << SYNC_ENDL;
    }

    else 
    {
        // 定跡手
        if (Options["UseBook"] && !Limits.infinite)
        {
            // 定跡手を取得
            const Move m = Book.probe(root_board);

            // 定跡にヒット
            if (m != MOVE_NONE)
            {
                auto it_move = std::find(root_moves.begin(), root_moves.end(), m);

                if (it_move != root_moves.end())
                {
                    std::swap(root_moves[0], *it_move);
                    book_hit = true;
                }
            }
        }

        if (!book_hit)
        {
            // 検討モード時や秒読み時は合法手が1手しかなくても思考する
            if (!Limits.infinite
                && root_moves.size() == 1
                && !Limits.move_time)
            {
                completed_depth = DEPTH_MAX;
                root_moves[0].score = SCORE_INFINITE; // 絶対この手が選ばれるように
            }
            else
            {
                // slaveスレッドの探索を開始させる
                for (auto th : Threads.slaves)
                    th->startSearching();

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

    if (!easy_move_played
        && !book_hit
        && Options["MultiPV"] == 1
        && root_moves[0].pv[0] != MOVE_NONE)
    {
        for (Thread* th : Threads)
        {
            Depth depth_diff = th->completed_depth - best_thread->completed_depth;
            Score score_diff = th->root_moves[0].score - best_thread->root_moves[0].score;

            if (score_diff > 0 && depth_diff >= 0)
                best_thread = th;
        }
    }

    previous_score = best_thread->root_moves[0].score;

    // もし必要なら新たなpvを表示しておく
    if (best_thread != this)
        SYNC_COUT << USI::pv(best_thread->root_board, best_thread->completed_depth, -SCORE_INFINITE, SCORE_INFINITE) << SYNC_ENDL;

    // 宣言勝ちできるなら勝ち
    if (declare_win)
        SYNC_COUT << "bestmove win" << SYNC_ENDL;

    else
    {
        const Move best_move = best_thread->root_moves[0].pv[0];

        SYNC_COUT << "bestmove " << toUSI(best_move);

        if (Options["USI_Ponder"]
            && best_thread->root_moves[0].pv.size() > 1
            || (best_thread->root_moves[0].pv[0] != MOVE_NONE
                && best_thread->root_moves[0].extractPonderFromTT(root_board, WeakPonder)))
            std::cout << " ponder " << toUSI(best_thread->root_moves[0].pv[1]);

        std::cout << SYNC_ENDL;
    }
}

void Thread::search()
{
    // (ss - 4) and (ss + 2)という参照を許すため
    Stack stack[MAX_PLY + 7], *ss = stack + 4;
    Score best_score, alpha, beta, delta;
    MainThread* main_thread = (this == Threads.main() ? Threads.main() : nullptr);
    Move easy_move = MOVE_NONE;

    std::memset(ss - 4, 0, 7 * sizeof(Stack));

    for (int i = 4; i > 0; i--)
        (ss - i)->counter_moves = counter_move_history.refer(); // Use as sentinel

    best_score = delta = alpha = -SCORE_INFINITE;
    beta = SCORE_INFINITE;

    // やねうら王を参考に、将棋所のコンソールが詰まるのを防ぐ。
    int last_info_time = 0;
    const int pv_interval = 0;

    if (main_thread)
    {
        easy_move = EasyMove.get(root_board.key());
        EasyMove.clear();
        WeakPonder = MOVE_NONE;
        main_thread->easy_move_played = main_thread->failed_low = false;
        main_thread->best_move_changes = 0;
        TT.newSearch();
    }

    const size_t multi_pv = std::min((size_t)Options["MultiPV"], root_moves.size());

    while ((root_depth += ONE_PLY) < DEPTH_MAX 
        && !Signals.stop 
        && (!Limits.depth || Threads.main()->root_depth / ONE_PLY <= Limits.depth))
    {
        // Distribute search depths across the threads
        if (idx)
        {
            int i = (idx - 1) % 20;
            if (((root_depth / ONE_PLY + root_board.ply() + phase[i]) / skipsize[i]) % 2)
                continue;
        }

        // PV変動率を計算する
        if (main_thread)
            main_thread->best_move_changes *= 0.505, main_thread->failed_low = false;

        for (RootMove& rm : root_moves)
            rm.previous_score = rm.score;

        for (pv_idx = 0; pv_idx < multi_pv && !Signals.stop; pv_idx++)
        {
            if (root_depth >= 5 * ONE_PLY)
            {
                delta = Score(18);
                alpha = std::max(root_moves[pv_idx].previous_score - delta, -SCORE_INFINITE);
                beta  = std::min(root_moves[pv_idx].previous_score + delta,  SCORE_INFINITE);
            }

            while (true)
            {
                best_score = ::search<PV>(root_board, ss, alpha, beta, root_depth, false);

                std::stable_sort(root_moves.begin() + pv_idx, root_moves.end());

                // 勝ちを見つけたら速攻指す
                if (!Limits.infinite
                    && !Limits.ponder
                    && (best_score >= SCORE_MATE_IN_MAX_PLY 
                    || (best_score >= SCORE_KNOWN_WIN && root_moves.size() == 1)))
                {
                    SYNC_COUT << USI::pv(root_board, root_depth, alpha, beta) << SYNC_ENDL;
                    completed_depth = DEPTH_MAX;
                    Signals.stop = true;
                }

                if (Signals.stop)
                    break;

                // 探索をする前にUIに出力する
                if (main_thread
                    && multi_pv == 1
                    && (best_score <= alpha || best_score >= beta)
                    && Time.elapsed() > 3000
                    && (root_depth < 3 * ONE_PLY || last_info_time + pv_interval < Time.elapsed()))
                {
                    last_info_time = Time.elapsed();
                    SYNC_COUT << USI::pv(root_board, root_depth, alpha, beta) << SYNC_ENDL;
                }

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

            std::stable_sort(root_moves.begin(), root_moves.begin() + pv_idx + 1);

            if (!main_thread)
                continue;

            if (Signals.stop || (pv_idx + 1 == multi_pv || Time.elapsed() > 3000)
                && (root_depth < 3 * ONE_PLY || last_info_time + pv_interval < Time.elapsed()))
            {
                last_info_time = Time.elapsed();
                SYNC_COUT << USI::pv(root_board, root_depth, alpha, beta) << SYNC_ENDL;
            }
        }

        if (!Signals.stop)
            completed_depth = root_depth;

        if (!main_thread)
            continue;

        if (main_thread->root_moves[0].pv.size() > 1)
            WeakPonder = main_thread->root_moves[0].pv[1];

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
                                 && Time.elapsed() > Time.optimum() * 5 / 44;

                if (root_moves.size() == 1
                    || Time.elapsed() > Time.optimum() * unstable_pv_factor * improving_factor / 628
                    || (main_thread->easy_move_played = do_easy_move, do_easy_move))
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
    Score search(Board& b, Stack* ss, Score alpha, Score beta, Depth depth, bool cut_node, bool skip_early_pruning)
    {
        const bool PvNode = NT == PV;
        const bool rootNode = PvNode && (ss - 1)->ply == 0;

        assert(-SCORE_INFINITE <= alpha && alpha < beta && beta <= SCORE_INFINITE);
        assert(PvNode || (alpha == beta - 1));
        assert(DEPTH_ZERO < depth && depth < DEPTH_MAX);
        assert(!(PvNode && cut_node));

        Move quiets_searched[QT];
        Score eval, score;
        Move best_move, move;
        int move_count, quiet_count;
        StateInfo st;

        // Step 1. ノード初期化
        Thread* this_thread = b.thisThread();
        const bool in_check = b.bbCheckers();
        Score best_score = -SCORE_INFINITE;
        ss->ply = (ss - 1)->ply + 1;
        move_count = quiet_count = ss->move_count = 0;
        ss->history = 0;

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
            case NO_REPETITION:       if (!Signals.stop.load(std::memory_order_relaxed) && ss->ply < MAX_PLY) { break; }
            case REPETITION_DRAW:     return drawScore(RootTurn, b.turn()); // ※↑のifに引っかからなかったらここに来る
            case REPETITION_WIN:      return mateIn(ss->ply);
            case REPETITION_LOSE:     return matedIn(ss->ply);
            case REPETITION_SUPERIOR: return SCORE_MATE_IN_MAX_PLY;
            case REPETITION_INFERIOR: return SCORE_MATED_IN_MAX_PLY;
            default: UNREACHABLE;
            }

            // Step 3. 詰み手数による枝刈り
            alpha = std::max(matedIn(ss->ply), alpha);
            beta  = std::min(mateIn(ss->ply + 1), beta);

            if (alpha >= beta)
                return alpha;
        }

        assert(0 <= ss->ply && ss->ply < MAX_PLY);

        ss->current_move = (ss + 1)->excluded_move = best_move = MOVE_NONE;
        ss->counter_moves = this_thread->counter_move_history.refer();
        (ss + 2)->killers[0] = (ss + 2)->killers[1] = MOVE_NONE;

        // Step 4. 置換表のlook up
        // singular extensionするべきノードかどうかを確かめるために、この局面の置換表の指し手を
        // 除外して探索するときはss->excluded_moveが存在し、その場合異なる局面のキーを用いる必要がある
        const Move excluded_move = ss->excluded_move;

#ifdef ENABLE_TT
        TTEntry *tte;
        Move tt_move;
        Score tt_score;
        Depth tt_depth;
        Bound tt_bound;
        Score tt_eval;

        const Key key = b.key() ^ Key(excluded_move << 1);
        const bool tt_hit = TT.probe(key, tte);

        if (tt_hit)
        {
            tt_move = rootNode ? this_thread->root_moves[this_thread->pv_idx].pv[0] : tte->move();
            tt_score = scoreFromTT(tte->score(), ss->ply);
            tt_depth = tte->depth();
            tt_bound = tte->bound();
            tt_eval = tte->eval();
        }
        else
        {
            tt_move = rootNode ? this_thread->root_moves[this_thread->pv_idx].pv[0] : MOVE_NONE;
            tt_score = SCORE_NONE;
            tt_depth = DEPTH_NONE;
            tt_bound = BOUND_NONE;
            tt_eval = SCORE_NONE;
        }
#else
        const bool tt_hit = false;
        const TTEntry* tte = nullptr;
        const Move tt_move = MOVE_NONE;
        const Score tt_score = SCORE_NONE;
        const Depth tt_depth = DEPTH_NONE;
        const Bound tt_bound = BOUND_NONE;
        const Score tt_eval  = SCORE_NONE;
#endif

        // pvノードではないなら置換表を見て枝刈りする
        if (!PvNode
            && tt_hit
            && tt_depth >= depth
            && tt_score != SCORE_NONE
            && (tt_score >= beta ? (tt_bound & BOUND_LOWER) 
                                 : (tt_bound & BOUND_UPPER)))
        {
            if (tt_move)
            {
                if (tt_score >= beta)
                {
                    if (!isCaptureOrPawnPromote(tt_move))
                        updateStats(b, ss, tt_move, depth, nullptr, 0, skip_early_pruning);

                    if ((ss - 1)->move_count == 1
                        && !isCaptureOrPawnPromote((ss - 1)->current_move)
                        && !skip_early_pruning)
                        updateCmStats(ss - 1, (ss - 1)->current_move, -statBonus(depth + ONE_PLY));
                }

                else if (!isCaptureOrPawnPromote(tt_move)
                    && !skip_early_pruning)
                {
                    int penalty = -statBonus(depth);
                    this_thread->history.update(b.turn(), tt_move, penalty);
                    updateCmStats(ss, tt_move, penalty);
                }
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
                best_score = mateIn(ss->ply + 1);
#ifdef ENABLE_TT
                // staticEvalはなんでもいい
                tte->save(key, scoreToTT(best_score, ss->ply), BOUND_EXACT,
                    DEPTH_MAX, best_move, best_score, TT.generation());
#endif
                return best_score;
            }
        }
#endif

#ifdef USE_PROGRESS
        // 進行度を差分計算
        const double progress = Prog::evaluateProgress(b);
#endif
        // Step 5. 王手ならすぐ指し手ループへ行く
        if (in_check)
        {
            ss->static_eval = eval = SCORE_NONE;
            goto moves_loop;
        }
        else if (tt_hit)
        {
            if ((eval = ss->static_eval = tt_eval) == SCORE_NONE)
                eval = ss->static_eval = evaluate(b);

            // tt_scoreが局面の評価値として使えるかどうか
            if (tt_score != SCORE_NONE && tt_depth >= DEPTH_ZERO)
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
        if (skip_early_pruning)
            goto moves_loop;

#ifdef FRONT_PRUNE
        // Step 6. Razoring (skipped when in check)
        // 現局面がalpha値より下で、razor_marginを足しても上回りそうにないなら
        // 普通に探索してもalphaを上回らないと見て即座に評価値を返す
        if (!PvNode
            && depth < 4 * ONE_PLY
            && eval + razor_margin[depth / ONE_PLY] <= alpha)
        {
            if (depth <= ONE_PLY)
                return qsearch<NO_PV, false>(b, ss, alpha, beta);

            Score ralpha = alpha - razor_margin[depth / ONE_PLY];
            Score s = qsearch<NO_PV, false>(b, ss, ralpha, ralpha + 1);

            if (s <= ralpha)
                return s;
        }

        // Step 7. Futility pruning : child node (skipped when in check)
        // 現局面がbetaを超えていて、相手がfutilityMargin(depth)分挽回してきたとしてもbetaを超えているのなら、
        // この局面は相当いいと思われるのでこれ以上探索しても意味がないと判断し、探索をスキップする
        if (!rootNode
            && depth < 7 * ONE_PLY
            && eval - futilityMargin(depth, progress) >= beta
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
            ss->counter_moves = this_thread->counter_move_history.refer();
            assert(eval - beta >= 0);

#ifdef EVAL_DIFF
            if (b.state()->sum.isNotEvaluated())
                ss->static_eval = evaluate(b);
#endif
            // 深さとスコアに基づいて、動的に減らす探索深さを決める
            // 16bitをオーバーフローするのでONE_PLYで割るのは先にやる必要がある。
            Depth R = ((823 + 67 * (depth / ONE_PLY)) / 256 + std::min((eval - beta) / PAWN_SCORE, 3)) * ONE_PLY;
            b.doNullMove(st);
            Score null_score = depth - R < ONE_PLY ? -qsearch<NO_PV, false>(b, ss + 1, -beta, -beta + 1)
                                                   : - search<NO_PV       >(b, ss + 1, -beta, -beta + 1, depth - R, !cut_node, true);
            b.undoNullMove();

            if (null_score >= beta)
            {
                if (null_score >= SCORE_MATE_IN_MAX_PLY)
                    null_score = beta;

                if (depth < 12 * ONE_PLY && abs(beta) < SCORE_KNOWN_WIN)
                    return null_score;

                // あまり早い段階での枝刈りは乱暴かもしれないので、Nullwindowで探索して、それでもbetaを超えているなら
                // 今度こそスキップする
                Score s = depth - R < ONE_PLY ? qsearch<NO_PV, false>(b, ss, beta - 1, beta)
                                              :  search<NO_PV       >(b, ss, beta - 1, beta, depth - R, false, true);

                if (s >= beta)
                    return null_score;
            }
        }

        // Step 9. ProbCut (skipped when in check)
        // もしとてもよい駒を取る手があり、かつ探索深さを減らした探索の戻り値がbetaを超えるなら、前の手は安全に枝刈りできる
        if (!PvNode
            && depth >= 5 * ONE_PLY
            && abs(beta) < SCORE_MATE_IN_MAX_PLY
            && !isPawnPromote((ss - 1)->current_move)) // ただし直前の指し手が歩成りならカットせずに普通に探索したほうがいいかも。
        {
            Score rbeta = std::min(beta + 200, SCORE_INFINITE);
            Depth rdepth = depth - 4 * ONE_PLY;

            assert(rdepth >= ONE_PLY);
            assert(isOK((ss - 1)->current_move));

#ifdef EVAL_DIFF
            if (b.state()->sum.isNotEvaluated())
                ss->static_eval = evaluate(b);
#endif
            // 一手前に取られた駒より価値の高い駒を取る手を優先して生成する
            MovePicker mp(b, tt_move, rbeta - ss->static_eval);

            while ((move = mp.nextMove()) != MOVE_NONE)
                if (b.legal<true>(move)) // 駒を取る手なので駒打ちではないはず
                {
                    assert(!isDrop(move));
                    ss->current_move = move;
                    ss->counter_moves = this_thread->counter_move_history.refer(move);
                    b.doMove(move, st, b.givesCheck(move));
                    score = -search<NO_PV>(b, ss + 1, -rbeta, -rbeta + 1, rdepth, !cut_node);
                    b.undoMove(move);

                    if (score >= rbeta)
                        return score;
                }
        }
#endif
#ifdef ENABLE_TT // 置換表がないのなら多重反復深化をする意味がない。
        // Step 10. Internal iteratiove deepening (skipped when in check)
        // 現局面から少しの得でbetaを超えそうなら、探索深さを減らして探索してみて、得られた手から探索する。
        if (depth >= 6 * ONE_PLY
            && tt_move == MOVE_NONE
            && (PvNode || (ss->static_eval + 256 >= beta)))
        {
            Depth d = (3 * depth / (4 * ONE_PLY) - 2) * ONE_PLY;
            Score s = search<NT>(b, ss, alpha, beta, d, cut_node, true);
            tt_move = TT.probe(key, tte) ? tte->move() : MOVE_NONE;
        }
#endif
    moves_loop: // 指し手生成ループ

        const CounterMoveStats* cmh = (ss - 1)->counter_moves;
        const CounterMoveStats* fmh = (ss - 2)->counter_moves;
        const CounterMoveStats* fm2 = (ss - 4)->counter_moves;

#ifdef EVAL_DIFF
        if (b.state()->sum.isNotEvaluated())
            evaluate(b);

        assert(!in_check || ss->static_eval == SCORE_NONE);
#endif
        MovePicker mp(b, tt_move, depth, ss);

        score = best_score;

        // 2手前より評価値がよくなっているかどうか
        const bool improving = ss->static_eval >= (ss - 2)->static_eval
                          /*|| ss->static_eval == SCORE_NONE redundant condition*/
                            ||(ss - 2)->static_eval == SCORE_NONE;

        const bool singular_extension_node = !rootNode
                                          && depth >= 8 * ONE_PLY
                                          && tt_move != MOVE_NONE
                                          && tt_score != SCORE_NONE
                                          && !excluded_move
                                          && (tt_bound & BOUND_LOWER)
                                          && tt_depth >= depth - 3 * ONE_PLY;

        // Step 11. Loop through moves
        while ((move = mp.nextMove()) != MOVE_NONE)
        {
            assert(isOK(move));

            if (move == excluded_move)
                continue;

            // root nodeでは、rootMoves()の集合に含まれていない指し手は探索をスキップする
            if (rootNode && !std::count(this_thread->root_moves.begin() + this_thread->pv_idx, this_thread->root_moves.end(), move))
                continue;
#if 0
            // 現在探索中の指し手、探索深さ、探索済みの手数を出力する
            if (rootNode && this_thread == Threads.main()/* && Time.elapsed() > 3000*/)
                SYNC_COUT << "info depth " << depth / ONE_PLY
                << " currmove " << toUSI(move)
                << " currmovenumber " << move_count + this_thread->pv_idx
                << " nodes " << b.nodeSearched() << SYNC_ENDL;
#endif
            if (PvNode)
                (ss + 1)->pv = nullptr;

            // この深さで探索し終わった手の数
            ++move_count;

            const bool capture_or_pawn_promotion = isCaptureOrPawnPromote(move);
            const bool gives_check = b.givesCheck(move);
            const bool move_count_pruning = depth < 16 * ONE_PLY && move_count >= FutilityMoveCounts[improving][depth / ONE_PLY];
            Depth extension = DEPTH_ZERO;
#ifdef EXTENSION
            // Step 12. 王手延長
            // 駒損しない王手と有効そうな王手は延長する。
            if (gives_check && !move_count_pruning)
                extension = (movedPieceType(move) == PAWN 
                    || (fromSq(move) & b.state()->blockers_for_king[~b.turn()])
                    || (isDrop(move) && toSq(move) == b.kingSquare(~b.turn()) + (b.turn() == BLACK ? DELTA_S : DELTA_N))
                    || b.seeGe(move, SCORE_ZERO)) ? ONE_PLY : ONE_PLY / 2;

            // singular extension search
            else if (singular_extension_node
                && move == tt_move
                && b.legal(move))
            {
                Score rbeta = tt_score - 16 * (depth / ONE_PLY);
                Depth d = depth / 2;
                ss->excluded_move = move;
                score = search<NO_PV>(b, ss, rbeta - 1, rbeta, d, cut_node, true);
                ss->excluded_move = MOVE_NONE;

                if (score < rbeta)
                    extension = ONE_PLY;
            }
#endif
            ss->move_count = move_count;

            Depth new_depth = depth - ONE_PLY + extension;
#ifdef FRONT_PRUNE
            // Step 13. Pruning at shallow depth
            if (!rootNode
                && best_score > SCORE_MATED_IN_MAX_PLY)
            {
                if (!capture_or_pawn_promotion && !gives_check)
                {
                    if (move_count_pruning)
                        continue;

                    int lmr_depth = std::max(new_depth - reduction<PvNode>(improving, depth, move_count), DEPTH_ZERO) / ONE_PLY;

                    // History based pruning
                    // 枝刈りの条件はかなり厳しくしている。
                    if (lmr_depth < 3
                        && (cmh->value(move) < CounterMovePruneThreshold)
                        && (fmh->value(move) < CounterMovePruneThreshold))
                        continue;

                    if (lmr_depth < 7
                        && !in_check
                        && ss->static_eval + 256 + 200 * lmr_depth <= alpha)
                        continue;

                    if  (lmr_depth < 8
                        && !b.seeGe(move, Score(-35 * lmr_depth * lmr_depth)))
                        continue;
                }

                // とても浅い残り探索深さにおける王手や駒をとる手のSEE値が負なら枝刈してしまう。
                else if (depth < 3 * ONE_PLY &&
                    (mp.seeSign() < 0 || (!mp.seeSign() && !b.seeGe(move, SCORE_ZERO))))
                    continue;
            }
#endif
            // 指し手が本当に合法かどうかのチェック(ルートならチェック済みなので要らない)
            if (!rootNode && !b.legal(move))
            {
                ss->move_count = --move_count;
                continue;
            }

            // この深さでの(現在探索中の)指し手
            ss->current_move = move;
            ss->counter_moves = this_thread->counter_move_history.refer(move);

            // Step 14. 指し手で局面を進める
            b.doMove(move, st, gives_check);

            bool do_full_depth_search;
#ifdef REDUCTION
            // Step 15. 探索深さを減らす(LMR) もしfail highならフル探索深さで探索する
            if (depth >= 3 * ONE_PLY
                && move_count > 1
                && (!capture_or_pawn_promotion || move_count_pruning))
            {
                Depth r = reduction<PvNode>(improving, depth, move_count);

                if (capture_or_pawn_promotion)
                    r -= r ? ONE_PLY : DEPTH_ZERO;
                else
                {
                    if (cut_node)
                        r += 2 * ONE_PLY;

                    ss->history = cmh->value(move)
                                + fmh->value(move)
                                + fm2->value(move)
                                + this_thread->history.get(~b.turn(), move)
                                - 4000;

                    if (ss->history > 0 && (ss - 1)->history < 0)
                        r -= ONE_PLY;

                    else if (ss->history < 0 && (ss - 1)->history > 0)
                        r += ONE_PLY;

                    r = std::max(DEPTH_ZERO, (r / ONE_PLY - ss->history / 20000) * ONE_PLY);
                }

                Depth d = std::max(new_depth - r, ONE_PLY);
                score = -search<NO_PV>(b, ss + 1, -(alpha + 1), -alpha, d, true);
                do_full_depth_search = (score > alpha && d != new_depth);
            }
            else
#endif
                do_full_depth_search = !PvNode || move_count > 1;

            // Step 16. フル探索。when LMR skipped or fails high 
            // Nullwindow探索をしてみて、alpha < score < betaでなければ通常の探索をスキップしてもよい
            if (do_full_depth_search)
                score = new_depth < ONE_PLY ?
                gives_check ? -qsearch<NO_PV,  true>(b, ss + 1, -(alpha + 1), -alpha)
                            : -qsearch<NO_PV, false>(b, ss + 1, -(alpha + 1), -alpha)
                            : - search<NO_PV       >(b, ss + 1, -(alpha + 1), -alpha, new_depth, !cut_node);

            if (PvNode && (move_count == 1 || (score > alpha && (rootNode || score < beta))))
            {
                (ss + 1)->pv = this_thread->pv[ss->ply];
                (ss + 1)->pv[0] = MOVE_NONE;

                // 残り深さがないならqsearchを呼び出す
                score = new_depth < ONE_PLY ?
                    gives_check ? -qsearch<PV,  true>(b, ss + 1, -beta, -alpha)
                                : -qsearch<PV, false>(b, ss + 1, -beta, -alpha)
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
                        assert(score >= beta); // Fail high
                        break;
                    }
                }
            }

            // 成りでも駒取りでもない手は、おとなしいquietな手
            if (!capture_or_pawn_promotion && move != best_move && quiet_count < QT)
                quiets_searched[quiet_count++] = move;
        }

        assert(move_count || !in_check || excluded_move || !MoveList<LEGAL>(b).size());

        // 一手も指されなかったということは、引き分けか詰み
        if (!move_count)
            best_score = excluded_move ? alpha
                            : in_check ? matedIn(ss->ply) : drawScore(RootTurn, b.turn());

        // quietな手が最善だった。統計を更新する
        else if (best_move)
        {
            if (!isCaptureOrPawnPromote(best_move))
                updateStats(b, ss, best_move, depth, quiets_searched, quiet_count, skip_early_pruning);

            if ((ss - 1)->move_count == 1
                && !isCaptureOrPawnPromote((ss - 1)->current_move)
                && !skip_early_pruning)
                updateCmStats(ss - 1, (ss - 1)->current_move, -statBonus(depth + ONE_PLY));
        }

        // 一手前の指し手がfail lowを引き起こしたことによるボーナス
        // PVノードではこのsearch()の呼び出し元でbeta cutが起きてupdateStatsが呼ばれるので、ここでhistoryのupdateを行ってしまうと
        // 2重にupdateすることになってしまう。なので!PvNodeが正しい。
        else if (!PvNode
            && !skip_early_pruning
            && depth >= 3 * ONE_PLY
            && !isCaptureOrPawnPromote((ss - 1)->current_move) // stockfishだと!capturePieceだけど歩のpromoteもだめなはず
            && isOK((ss - 1)->current_move))
            updateCmStats(ss - 1, (ss - 1)->current_move, statBonus(depth + ONE_PLY));

#ifdef ENABLE_TT
        if (!excluded_move)
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

        Move best_move;

        ss->current_move = best_move = MOVE_NONE;
        ss->ply = (ss - 1)->ply + 1;

        if (PvNode)
        {
            // GUIへselDepth(現在、選択的に読んでいる手の探索深さ)情報を送信するために使用
            if (b.thisThread()->max_ply < ss->ply)
                b.thisThread()->max_ply = ss->ply;

            (ss + 1)->pv = b.thisThread()->pv[ss->ply];
            ss->pv[0] = MOVE_NONE;
        }

        if (b.ply() >= 190 && b.isDeclareWin())
            return mateIn(ss->ply);

        switch (b.repetitionType(16))
        {
        case NO_REPETITION:       if (ss->ply < MAX_PLY) { break; }
        case REPETITION_DRAW:     return ss->ply >= MAX_PLY && !InCheck ? evaluate(b) : drawScore(RootTurn, b.turn()); // ※↑のifに引っかからなかったらここに来る
        case REPETITION_WIN:      return mateIn(ss->ply);
        case REPETITION_LOSE:     return matedIn(ss->ply);
        case REPETITION_SUPERIOR: return SCORE_MATE_IN_MAX_PLY;
        case REPETITION_INFERIOR: return SCORE_MATED_IN_MAX_PLY;
        default: UNREACHABLE;
        }

        assert(0 <= ss->ply && ss->ply < MAX_PLY);

        // 王手を含めるかどうかを決定する。また使用しようとしているTTエントリの深さのタイプを修正する
        Depth qdepth = (InCheck || depth >= DEPTH_QS_CHECKS) ? DEPTH_QS_CHECKS : DEPTH_QS_NO_CHECKS;

#ifdef ENABLE_TT
        TTEntry* tte;
        Move tt_move;
        Score tt_score;
        Depth tt_depth;
        Bound tt_bound;
        Score tt_eval;

        const Key key = b.key();
        bool tt_hit = TT.probe(key, tte);

        if (tt_hit)
        {
            tt_move = tte->move();
            tt_score = scoreFromTT(tte->score(), ss->ply);
            tt_depth = tte->depth();
            tt_bound = tte->bound();
            tt_eval = tte->eval();
        }
        else
        {
            tt_move = MOVE_NONE;
            tt_score = SCORE_NONE;
            tt_depth = DEPTH_NONE;
            tt_bound = BOUND_NONE;
            tt_eval = SCORE_NONE;
        }
#else
        const TTEntry* tte = nullptr;
        const bool tt_hit = false;
        const Move tt_move = MOVE_NONE;
        const Score tt_score = SCORE_NONE;
        const Depth tt_depth = DEPTH_NONE;
        const Bound tt_bound = BOUND_NONE;
        const Score tt_eval = SCORE_NONE;
#endif
#if 1
        // 置換表のほうが読みが深いなら置換表の評価値を返す
        if (!PvNode
            && tt_hit
            && tt_depth >= depth
            && tt_score != SCORE_NONE
            && (tt_score >= beta ? (tt_bound & BOUND_LOWER)
                                 : (tt_bound & BOUND_UPPER)))
            return tt_score;
#endif
#ifdef USE_PROGRESS
        // 進行度を差分計算
        //Prog::evaluateProgress(b);
#endif
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
#ifdef MATE1PLY
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
#endif
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

        while ((move = mp.nextMove()) != MOVE_NONE)
        {
            const bool gives_check = b.givesCheck(move);
#ifdef FRONT_PRUNE
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
                                      && depth != DEPTH_ZERO
                                      && best_score > SCORE_MATED_IN_MAX_PLY
                                      && !isCapture(move);

            // 王手されていない時や詰まされる時は、QUIETな手やSEEが-の手は探索しない
            if ((!InCheck || evasionPrunable)
                && !isPawnPromote(move)
                && !b.seeGe(move, SCORE_ZERO))
                continue;
#endif
            if (!b.legal(move))
                continue;

            ss->current_move = move;

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
                            depth, move, ss->static_eval, TT.generation());
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
            PvNode && best_move ? BOUND_EXACT : BOUND_UPPER,
            depth, best_move, ss->static_eval, TT.generation());
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

        return  s >= SCORE_MATE_IN_MAX_PLY ? s + ply
            : s <= SCORE_MATED_IN_MAX_PLY ? s - ply : s;
    }

    // ↑の関数と逆のことをする。現局面からの手数による詰みのスコアを、ルートからの手数による詰みのスコアに調整する
    Score scoreFromTT(Score s, int ply)
    {
        return  s == SCORE_NONE ? SCORE_NONE
            : s >= SCORE_MATE_IN_MAX_PLY ? s - ply
            : s <= SCORE_MATED_IN_MAX_PLY ? s + ply : s;
    }

    // 現在の指し手をpvに加え、その指し手の後のpvを現在のpvに加える
    void updatePv(Move* pv, Move move, Move* childPv)
    {
        for (*pv++ = move; childPv && *childPv != MOVE_NONE; )
            *pv++ = *childPv++;

        *pv = MOVE_NONE;
    }

    void updateCmStats(Stack * ss, Move m, int bonus)
    {
        for (int i : {1, 2, 4})
            if (isOK((ss - i)->current_move))
                (ss - i)->counter_moves->update(m, bonus);
    }

    // 統計情報を更新する
    // 現局面に対してβカットが起こり、なおかつ最善手がquietな手だったときに呼び出される
    void updateStats(const Board& b, Stack* ss, Move move, Depth depth, Move* quiets, int quiet_cnt, bool skip_early_pruning)
    {
        // skip_early_pruningなnodeは、一つ前の階層のsearchから局面をdoMoveせずに探索深さを浅くしただけのものであり、
        // そこでbeta cutを引き起こした手は呼び出し元のnodeで試すキラー手としての価値が薄いので、
        // もともと試すべきだったkiller手を追い出してまでkillerとして登録するべきではない。
        if (skip_early_pruning)
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
        int bonus = statBonus(depth);
        Thread* this_thread = b.thisThread();

        // この指し手がbeta cutを引き起こした（または最善手だった）ので、ボーナスを与える
        this_thread->history.update(b.turn(), move, bonus);
        updateCmStats(ss, move, bonus);

        // 前回指された手がパスではないなら
        // 前回の手に対するカウンター手として今回の指し手と、そのボーナスを追加
        if (isOK((ss - 1)->current_move))
            this_thread->counter_moves.update((ss - 1)->current_move, move);
 
        // 他の静かな手は減点
        for (int i = 0; i < quiet_cnt; ++i)
        {
            this_thread->history.update(b.turn(), quiets[i], -bonus);
            updateCmStats(ss, quiets[i], -bonus);
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
    size_t pv_idx = b.thisThread()->pv_idx;
    size_t multi_pv = std::min((size_t)Options["MultiPV"], root_moves.size());
    uint64_t nodes_searched = Threads.nodeSearched();

    for (size_t i = 0; i < multi_pv; i++)
    {
        bool updated = (i <= pv_idx);

        if (depth == ONE_PLY && !updated)
            continue;

        Depth d = updated ? depth : depth - ONE_PLY;
        Score s = updated ? root_moves[i].score : root_moves[i].previous_score;

        if (ss.rdbuf()->in_avail()) // Not at first line
            ss << "\n";

        ss << "info"
            << " depth " << d / ONE_PLY
            << " seldepth " << b.thisThread()->max_ply
            << " multipv " << i + 1
            << " score " << USI::score(s)
            << (s >= beta ? " lowerbound" : s <= alpha ? " upperbound" : "")
            << " nodes " << nodes_searched
            << " nps " << nodes_searched * 1000 / elapsed;

        if (elapsed > 1000)
            ss << " hashfull " << TT.hashfull();

        ss << " time " << elapsed
           << " pv";

        for (Move m : root_moves[i].pv)
            ss << " " << toUSI(m);
    }

    return ss.str();
}

#if defined LEARN || defined GENSFEN
namespace Learn
{
    void initLearn(Board& b)
    {
        auto& limits = USI::Limits;
        limits.infinite = true;

        auto th = b.thisThread();

        th->max_ply = 0;

        auto& root_moves = th->root_moves;
        root_moves.clear();

        for (auto m : MoveList<LEGAL>(b))
            root_moves.push_back(Search::RootMove(m));

        assert(root_moves.size());

        th->root_depth = DEPTH_ZERO;
    }

    std::pair<Score, std::vector<Move>> search(Board& b, Score alpha, Score beta, Depth depth)
    {
        Stack stack[MAX_PLY + 7], *ss = stack + 5;
        memset(ss - 5, 0, 8 * sizeof(Stack));

        initLearn(b);
        Score best_score, delta;
        best_score = delta = -SCORE_INFINITE;

        auto th = b.thisThread();
        auto& root_depth = th->root_depth;
        auto& root_moves = th->root_moves;

        while ((root_depth += ONE_PLY) <= depth)
        {
            if (root_depth >= 5 * ONE_PLY)
            {
                delta = Score(60);
                alpha = std::max(root_moves[0].previous_score - delta, -SCORE_INFINITE);
                beta  = std::min(root_moves[0].previous_score + delta, SCORE_INFINITE);
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

    // 枝刈りなしの単純なαβ
    template <NodeType NT>
    Score ab(Board& b, Stack* ss, Score alpha, Score beta, Depth depth, bool cut_node)
    {
        return SCORE_ZERO;
    }

    template<NodeType NT, bool InCheck>
    Score qab(Board& b, Stack* ss, Score alpha, Score beta, Depth depth)
    {
        return SCORE_ZERO;
    }

    Score ab(Board& b, Score alpha, Score beta, Depth depth)
    {
        Search::Stack stack[MAX_PLY + 7], *ss = stack + 5;
        memset(ss - 5, 0, 8 * sizeof(Search::Stack));

        Learn::initLearn(b);
        Time.reset();
        TT.newSearch();
        Score best_score = -SCORE_INFINITE;

        auto th = b.thisThread();
        auto& root_depth = th->root_depth;
        auto& root_moves = th->root_moves;

        for (root_depth = ONE_PLY; root_depth <= depth; root_depth += ONE_PLY)
        {
            best_score = ab<PV>(b, ss, -SCORE_INFINITE, SCORE_INFINITE, root_depth, false);
            std::stable_sort(root_moves.begin(), root_moves.end());
            //std::cout << USI::pv(b, root_depth, -SCORE_INFINITE, SCORE_INFINITE) << std::endl;
        }

        return best_score;
    }
} // namespace Learn

#endif