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

#include "learn.h"

#ifdef LEARN

#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#define NOMINMAX

#ifdef _MSC_VER
#include <Windows.h>
#endif

#include "tt.h"
#include "usi.h"
#include "search.h"
#include "sfen_rw.h"

namespace Eval
{
    LearnFloatType Weight::eta = 64.0f;
    int Weight::skip_count = 10;
}

namespace Learn
{
    // 評価値を勝率[0,1]に変換する関数
    double winest(Score score) { return sigmoid(static_cast<int>(score) / 600.0); }

    const double LAMBDA = 1.0;
    const Score EVAL_LIMIT = (Score)2000;

    // 式変形したelmo式
    double calcGrad(Score deep, Score shallow, bool win, double progress)
    {
        double p = winest(deep);
        double q = winest(shallow);
        double w = win ? 1.0 : 0.0;
        double o = LAMBDA * (w - p) * progress;
        return q - (p + o);
    }

    namespace LearnSpace
    {
        // sfenの読み出し器
        AsyncSfenRW sr;

        // mse計算用のバッファ
        std::vector<PackedSfenValue> sfen_for_mse;

        // 学習の反復回数のカウンター
        uint64_t epoch = 0;

        // ミニバッチサイズ
        uint64_t mini_batch_size = 1000 * 1000;

        // updateWeightsしている途中であることを表すフラグ
        std::atomic_bool updating;

        // mini batch何回ごとにrmseを計算するか。
        const int LEARN_RMSE_OUTPUT_INTERVAL = 10;

        // evalフォルダの名前を変える周期。
        const int EVAL_FILE_NAME_CHANGE_INTERVAL = (1000 * 1000 * 500);

        bool initMse()
        {
            for (int i = 0; i < 10000; ++i)
            {
                PackedSfenValue ps;

                if (!sr.read(0, ps))
                {
                    std::cout << "Error! read packed sfen , failed." << std::endl;
                    return false;
                }

                sfen_for_mse.push_back(ps);
            }

            return true;
        }

        void calcRmse(Board& b)
        {
            if (sfen_for_mse.size() == 0)
            {
                std::cout << "error! sfen_for_mse is empty." << std::endl;
                return;
            }

            int i = 0;
            double sum_error = 0;
            double sum_error2 = 0;

            for (auto& ps : sfen_for_mse)
            {
                b.setFromPackedSfen(ps.data);
                auto r = Learn::qsearch(b);
                Score shallow_value = r.first;
                Score deep_value = Score(ps.deep);
                if ((deep_value >= EVAL_LIMIT && ps.win) || (deep_value <= -EVAL_LIMIT && !ps.win))
                    continue;
#if 0
                if ((i++ % 1000) == 0)
                    SYNC_COUT << "shallow = " << std::setw(5) << shallow_value
                    << " deep = " << std::setw(5) << deep_value
                    << " grad = " << calcGrad(shallow_value, deep_value, ps.win) << SYNC_ENDL;
#endif
                // 誤差の計算
                auto grad = calcGrad(deep_value, shallow_value, ps.win, Progress::evaluate(b));
                sum_error += grad * grad;
                sum_error2 += abs(shallow_value - deep_value);
            }

            auto rmse = std::sqrt(sum_error / sfen_for_mse.size());
            auto ame = sum_error2 / sfen_for_mse.size();
            std::cout << std::endl << "rmse = " << rmse << " , mean_error = " << ame << std::endl;
        }

        struct LearnThread : public WorkerThread
        {
            virtual void search()
            {
                Board& b = root_board;
                uint64_t next_update_weights = mini_batch_size;

                while (!Threads.stop)
                {
                    if (isMain() && next_update_weights <= sr.readCount())
                    {
                        static uint64_t sfens_output_count = 0;
                        static uint64_t rmse_output_count = 0;
                        static uint64_t save_count = 0;
                        uint64_t read_count = sr.readCount();

                        if ((sfens_output_count++ % LEARN_RMSE_OUTPUT_INTERVAL) == 0)
                            std::cout << std::endl << read_count << " sfens , at " << localTime();
                        else
                            std::cout << '.';
                        
                        updating = true;

                        for (auto th : Threads.slaves)
                            th->join();

                        Eval::updateWeights(++epoch);

                        updating = false;

                        for (auto t : Threads.slaves)
                            t->startSearching();

                        if (++save_count >= 100000000 / mini_batch_size)
                        {
                            save_count = 0;
                            Eval::GlobalEvaluater->save(std::to_string(read_count / (uint64_t)EVAL_FILE_NAME_CHANGE_INTERVAL));
                        }

                        if ((rmse_output_count++ % LEARN_RMSE_OUTPUT_INTERVAL) == 0)
                            calcRmse(b);

                        next_update_weights = std::max(read_count + mini_batch_size, read_count);
                    }

                    // バッファからsfenを一つ受け取る。
                    PackedSfenValue ps;

                    if (!sr.read(idx, ps))
                        break;

                    // 深い探索の評価値
                    auto deep_value = Score(ps.deep);

                    if ((deep_value >= EVAL_LIMIT && ps.win) || (deep_value <= -EVAL_LIMIT && !ps.win))
                        continue;

                    b.setFromPackedSfen(ps.data);
                    auto root_turn = b.turn();
                    auto r = Learn::qsearch(b);
                    auto shallow_value = r.first;
#if 0
                    SYNC_COUT << b
                        << "shallow = " << shallow_value
                        << " deep = " << deep_value
                        << " win = " << ps.win
                        << " move = " << pretty(ps.m) << SYNC_ENDL;
#endif
                    // update中なので、addGradする前にメインスレッドがupdateを終えるのを待つ。
                    while (updating)
                    {
                        searching = false;
                        startSearching(true);
                        wait(searching);
                    }

                    // 現在、leaf nodeで出現している特徴ベクトルに対する勾配(∂J/∂Wj)として、dj_dwを加算する。
                    double dj_dw = calcGrad(deep_value, shallow_value, ps.win, Progress::evaluate(b));

                    if (dj_dw == 0.0)
                        continue;

                    int ply = 0;
                    StateInfo state[MAX_PLY];

                    for (auto m : r.second)
                        b.doMove(m, state[ply++]);

                    Eval::addGrad(b, root_turn, dj_dw);
                }

                if (isMain())
                {
                    sr.finalizeReader();
                    Eval::GlobalEvaluater->save("finish");
                    SYNC_COUT << "learn end" << SYNC_ENDL;
                }
            }
        };
    } // namespace LearnSpace

    // 生成した棋譜からの学習
    void learn(Board& b, std::istringstream& is)
    {
        auto thread_num = (int)USI::Options["Threads"];
        std::vector<std::string> filenames;
        int mini_batch_size = 1000000;
        int loop = 100;

        while (true)
        {
            std::string option;
            is >> option;

            if (option == "")
                break;
            else if (option == "bat")
                is >> mini_batch_size;
            else if (option == "loop")
                is >> loop;
            else
                filenames.push_back(option);
        }

        std::cout << "learn from ";

        for (auto s : filenames)
            std::cout << s << " , ";

        std::cout << "loop = " << loop << std::endl;

        std::vector<std::string> files;

        for (int i = 0; i < loop; ++i)
            for (auto it = filenames.rbegin(); it != filenames.rend(); ++it)
                files.push_back(*it);

        std::cout << "\nLoss Function   : " << "cross entoropy";
        std::cout << "\nmini-batch size : " << mini_batch_size;
        std::cout << "\neta             : " << Eval::Weight::eta;
        std::cout << "\ninit..";

        // 評価関数パラメーターの読み込み
        USI::isready();

        // 局面ファイルをバックグラウンドで読み込むスレッドを起動
        LearnSpace::sr.initReader(thread_num, files);
        LearnSpace::sr.startReader();
        LearnSpace::mini_batch_size = mini_batch_size;

        // mse計算用にデータ1万件ほど取得しておく。
        if (!LearnSpace::initMse())
            return;

        // 評価関数パラメーターの勾配配列の初期化
        Eval::initGrad();

        std::cout << "init done." << std::endl;

        Threads.startWorkers<LearnSpace::LearnThread>(thread_num);
    }

    // 教師データを生成しながら逐次学習させていくためのクラス。
    // これをオンライン学習といっていいのか？ミニバッチ学習が正しいか？わからん。
    namespace OnlineSpace
    {
        // スレッド間で共有したい情報をここに書く
        struct Teacher
        {
            uint8_t data[32];
            int16_t deep;
            bool win;
            std::vector<Move> pv;
        };

        uint64_t epoch = 0;
        std::vector<Teacher> sfen_buffer;
        Mutex mutex;
        bool gen_sfen = true;
        uint64_t teacher_win = 0, training_win = 0, draw = 0;
        Eval::Evaluater* teacher_base;
        std::atomic<double> rmse = {0.0};
        std::atomic<uint64_t> rmse_count = {0};

        // mini batchの大きさ
        uint64_t mini_batch_size;

        // 教師生成時の探索深さ
        int search_depth;

        // multiPVの候補手数
        int multi_pv;

        // 何手目までをmultiPVで指すか
        int move_count;

        // multiPVで許される最善手とのスコア差
        Score multi_pv_range;

        // 自己対戦を打ち切る評価値（この値以下であれば打ち切る）
        Score limit_score;

        // 何回連続で教師を上回っていれば教師を超えたと判定するか
        int update_exceed_count_threshold;

        // 教師を上回ったと判定する勝率
        double update_training_winrate_threshold;

        void write(Teacher& ps, bool win)
        {
            ps.win = win;
            std::unique_lock<Mutex> lk(mutex);
            sfen_buffer.emplace_back(ps);

            if (sfen_buffer.size() % (mini_batch_size / 10) == 0)
                std::cout << ".";

            if (sfen_buffer.size() >= mini_batch_size)
                gen_sfen = false;
        }

        bool read(Teacher& ps)
        {
            std::unique_lock<Mutex> lk(mutex);

            if (sfen_buffer.size())
            {
                ps = sfen_buffer.back();
                sfen_buffer.pop_back();
                return true;
            }

            return false;
        }

        struct OnlineLearnThread : public WorkerThread
        {
            virtual void search()
            {
                const int MAX_PLY = 512;
                const bool is_main = isMain();
                Teacher psv[MAX_PLY];
                StateInfo state[MAX_PLY + 64];
                Board& b = root_board;
                Turn teacher_turn = ~Turn(idx % 2);
                Thread* teacher_th = this;
                teacher_th->tt = new TranspositionTable;
                teacher_th->tt->resize(32);
                Thread* training_th = new Thread;
                training_th->tt = new TranspositionTable;
                training_th->tt->resize(32);
                teacher_th->clear();
                training_th->clear();

                while (!Threads.stop)
                {
                    // 棋譜生成フェーズ。決められたdepthで対戦を行う。
                    if (is_main)
                        std::cout << "epoch:" << epoch;

                    teacher_th->evaluater = &teacher_base;
                    training_th->evaluater = &Eval::GlobalEvaluater;

                LOOP_BEGIN:

                    while (gen_sfen && !Threads.stop)
                    {
                        int gameply = 0;
                        b.init(USI::START_POS, this);
                        teacher_th->clear();
                        training_th->clear();

                        for (int ply = 0; ply < MAX_PLY; ++ply)
                        {
                            if (b.isMate())
                                goto LOOP_BEGIN;

                            if (b.repetitionType(16) == REPETITION_DRAW)
                            {
                                draw++;
                                goto LOOP_BEGIN;
                            }

                            b.setThread(b.turn() == teacher_turn ? teacher_th : training_th);
                            Eval::computeAll(b);

                            Move m;

                            if (multi_pv || ply >= move_count)
                            {
                                auto r = ply < move_count ? rand(multi_pv) + 1 : 1;
                                auto pv_value1 = Learn::search(b, search_depth * ONE_PLY, r, multi_pv_range);
                                psv[gameply].deep = pv_value1.first;
                                psv[gameply].pv = pv_value1.second;

                                if (ply == move_count && abs(psv[gameply].deep) >= 100)
                                    goto LOOP_BEGIN;

                                if (abs(psv[gameply].deep) >= SCORE_MATE_IN_MAX_PLY
                                    || psv[gameply].deep <= limit_score)
                                    break;

#if 0
                                if (idx == 0
                                    //&& ply == multi_pv_move_count
                                    )
                                {
                                    SYNC_COUT << b
                                        << "deep = " << (b.turn() == BLACK ? psv[gameply].deep : -psv[gameply].deep)
                                        << "\nmoves = ";

                                    for (auto m : psv[gameply].pv)
                                        std::cout << pretty(m) << " ";

                                    std::cout << SYNC_ENDL;
                                }
#endif
                                m = pv_value1.second[0];
                            }
                            else
                            {
                                // 序盤においてrandomな手を指させることで、進行をばらけさせる。
                                assert(ply < move_count);
                                MoveList<LEGAL> ml(b);
                                m = ml.begin()[rand(ml.size())];
                            }

                            // packを要求されているならpackされたsfenとそのときの評価値を書き出す。
                            if (move_count <= ply)
                            {
                                b.sfenPack(psv[gameply].data);
                                gameply++;
                            }

                            b.doMove(m, state[ply]);
                        }

                        // 対局終了。勝ったほうには1,負けたほうには0のフラグを立てておく。
                        Turn winner = psv[gameply].deep >= 0 ? b.turn() : ~b.turn();

                        if (winner == teacher_turn)
                            teacher_win++;
                        else
                            training_win++;

                        const Turn start_turn = (move_count & 1) ? WHITE : BLACK;

                        for (int i = 0; i < gameply - 1; i++)
                        {
                            Turn t = (i & 1) ? ~start_turn : start_turn;
                            bool win = t == winner;
#if 0
                            b.setFromPackedSfen(psv[i].data);
                            SYNC_COUT << b
                                << "deep = " << (b.turn() == BLACK ? psv[i].deep : -psv[i].deep)
                                << " deep+1 = " << (b.turn() == BLACK ? -psv[i + 1].deep : psv[i + 1].deep)
                                << " win = " << psv[i].win
                                << "\nmoves = ";

                            for (auto m : psv[i].pv)
                                std::cout << pretty(m) << " ";

                            std::cout << "\nmoves+1 = ";

                            for (auto m : psv[i + 1].pv)
                                std::cout << pretty(m) << " ";

                            std::cout << SYNC_ENDL;
#endif
                            // 勝ったほうの評価値を教師とする。
                            // ここでさらに未来の評価値を使うという手もあるが効果はよくわからなかった。
                            psv[i].deep = win ? psv[i].deep : -psv[i + 1].deep;
                            //psv[i].deep = t != teacher_turn ? psv[i].deep : -psv[i + 1].deep;

#if 0
                            // 相手の指し手が自分の読みになかった場合、自分の読み筋の進行は実際には対局には現れない。
                            // その進行は、自分が最終的に勝った場合には良い手順であったのだろうし、負けた場合は悪い手順であったのだろうと判断できる。
                            // 対局に現れなかったpvは学習に使うことができる重要な情報なので保存しておくが、効果があるかどうかはわからない。
                            // 対局に現れたpvは二重に学習することになるのでいらない。
                            if (psv[i].pv[1] == psv[i + 1].pv[0])
#endif
                                psv[i].pv.resize(0);

                            write(psv[i], win);
                        }
                    }

                    if (Threads.stop)
                        break;

                    // 勝ち負けを集計する。
                    if (is_main)
                    {
                        for (auto th : Threads.slaves)
                            th->join();
#if 0
                        // 同一局面を取り除く。
                        int pos_size = sfen_buffer.size();
                        int c = 0;
                        for (auto i = sfen_buffer.begin(); i != sfen_buffer.end() - 1; i++)
                            for (auto j = i + 1; j != sfen_buffer.end(); j++)
                            {
                                auto mask = _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i*)i->data)
                                    , _mm256_loadu_si256((const __m256i*)j->data));

                                if (_mm256_testc_si256(mask, _mm256_cmpeq_epi8(mask, mask)))
                                {
                                    c++;
                                    j = sfen_buffer.erase(j) - 1;
                                }
                            }
                        SYNC_COUT << ((double)c / (double)pos_size) * 100.0 << "[%][same/pos]" << SYNC_ENDL;
#endif
                        double training_winrate = (double)training_win / (double)(teacher_win + training_win);

                        SYNC_COUT << " training:" << training_win
                                  << " draw:"     << draw
                                  << " teacher:"  << teacher_win
                                  << " winrate:"  << training_winrate * 100.0 << "%" << SYNC_ENDL;

                        teacher_win = training_win = draw = 0;
                        static int continue_exceed = 0;

                        if (training_winrate >= update_training_winrate_threshold)
                            continue_exceed++;
                        else
                            continue_exceed = 0;

                        // 次の世代に移るかどうかを決定する。
                        if (continue_exceed >= update_exceed_count_threshold)
                        {
                            static int generation = 0;
                            Eval::GlobalEvaluater->save("epoch" + std::to_string(generation));
                            teacher_th->tt->clear();

                            std::cout << "*************************************************\n"
                                      << "**************** exceed teacher! ****************\n"
                                      << "*************************************************\n"
                                      << "next generation = " << generation << std::endl;

                            *teacher_base = *Eval::GlobalEvaluater;
                            Eval::initGrad();
                            generation++;
                            epoch = 0;

                            // 教師を超えたので今回の教師データは捨てる。
                            sfen_buffer.clear();
                            gen_sfen = true;
                            continue_exceed = 0;
                        }

                        for (auto th : Threads.slaves)
                            th->startSearching();
                    }

                    else
                    {
                        // mainスレッドを待つ。
                        searching = false;
                        startSearching(true);
                        wait(searching);
                    }

                    b.setThread(training_th);

                    while (!gen_sfen)
                    {
                        // バッファからsfenを一つ受け取る。
                        Teacher ps;

                        if (!read(ps))
                        {
                            if (is_main)
                            {
                                // ほかのスレッドのsearchingフラグがfalseになるのを待つ。
                                for (auto th : Threads.slaves)
                                    th->join();

                                SYNC_COUT << "rmse:" << std::sqrt(rmse / rmse_count) << " count:" << rmse_count << SYNC_ENDL;
                                rmse = 0.0;
                                rmse_count = 0;
                                Eval::updateWeights(++epoch);
                                training_th->clear();
                                gen_sfen = true;

                                for (auto th : Threads.slaves)
                                    th->startSearching();

                                if (epoch % 100 == 0)
                                    Eval::GlobalEvaluater->save(std::to_string(epoch / 100));
                            }

                            else
                            {
                                searching = false;
                                startSearching(true);
                                wait(searching);
                            }

                            continue;
                        }

                        // 深い探索の評価値
                        auto deep_value = Score(ps.deep);

                        if ((deep_value >= EVAL_LIMIT && ps.win) || (deep_value <= -EVAL_LIMIT && !ps.win))
                            continue;

                        b.setFromPackedSfen(ps.data);

                        int pvi = 0;

                        // pvで進めた局面すべてに対して勾配を適用
                        do {
                            auto root_turn = b.turn();
                            auto r = Learn::qsearch(b);
                            auto shallow_value = r.first;
                            double dj_dw = calcGrad(deep_value, shallow_value, ps.win, Progress::evaluate(b));
                            rmse = rmse + dj_dw * dj_dw;
                            rmse_count++;
                            int ply = pvi;
#if 0
                            if (idx == 0)
                            {
                                SYNC_COUT << b
                                    << "shallow = " << shallow_value
                                    << " deep = " << deep_value
                                    << " win = " << ps.win;

                                if (pvi < ps.pv.size())
                                    std::cout << " move = " << pretty(ps.pv[pvi]);

                                std::cout << SYNC_ENDL;
                            }
#endif
                            for (auto m : r.second)
                                b.doMove(m, state[ply++]);

                            Eval::addGrad(b, root_turn, dj_dw);

                            if (pvi >= (int)ps.pv.size())
                                break;

                            for (auto it = r.second.rbegin(); it != r.second.rend(); it++)
                                b.undoMove(*it);

                            // pv[1]で進めた局面は実際の進行に表れているので捨てる。
                            do {
                                b.doMove(ps.pv[pvi], state[pvi]);
                                ps.win = !ps.win;
                                deep_value = -deep_value;
                            } while (++pvi == 1);
                        } while (true);
                    }
                }

                delete teacher_th->tt;
                delete training_th->tt;
                delete training_th;

                if (is_main)
                {
                    for (auto th : Threads.slaves)
                        th->join();

                    delete teacher_base;
                    SYNC_COUT << "online learning end." << SYNC_ENDL;
                }
                else
                {
                    searching = false;
                    startSearching(true);
                }
            }
        };

        // sfenの書き出し器
        AsyncSfenRW sw;

        struct GenSfenThread : public WorkerThread
        {
            virtual void search()
            {
                const int MAX_PLY = 512;
                StateInfo state[MAX_PLY + 64];
                PackedSfenValue psv[MAX_PLY];
                Board& b = root_board;
                tt = new TranspositionTable;
                tt->resize(USI::Options["Hash"]);
                tt->clear();

                while (!Threads.stop)
                {
                LOOP_BEGIN:

                    int gameply = 0;
                    b.init(USI::START_POS, this);

                    for (int ply = 0; ply < MAX_PLY; ++ply)
                    {
                        if (b.isMate())
                            break;

                        if (b.repetitionType(16) != NO_REPETITION)
                            goto LOOP_BEGIN;

                        Move m;

                        if (multi_pv || ply >= move_count)
                        {
                            auto r = ply < move_count ? rand(multi_pv) + 1 : 1;
                            auto pv_value1 = Learn::search(b, search_depth * ONE_PLY, r, multi_pv_range);
                            psv[gameply].deep = pv_value1.first;

                            if (abs(psv[gameply].deep) >= SCORE_MATE_IN_MAX_PLY
                                || psv[gameply].deep <= limit_score)
                                break;

                            if (ply == move_count && abs(psv[gameply].deep) >= 100)
                                goto LOOP_BEGIN;
#if 0
                            if (idx == 0
                                && ply == move_count
                                )
                            {
                                SYNC_COUT << b
                                    << "deep = " << (b.turn() == BLACK ? psv[gameply].deep : -psv[gameply].deep)
                                    << "\nmoves = ";

                                for (auto m : pv_value1.second)
                                    std::cout << pretty(m) << " ";

                                std::cout << SYNC_ENDL;
                            }
#endif
                            m = pv_value1.second[0];
                        }
                        else
                        {
                            // 序盤においてrandomな手を指させることで、進行をばらけさせる。
                            assert(ply < move_count);
                            MoveList<LEGAL> ml(b);
                            m = ml.begin()[rand(ml.size())];
                        }

                        // packを要求されているならpackされたsfenとそのときの評価値を書き出す。
                        if (move_count <= ply)
                        {
                            b.sfenPack(psv[gameply].data);
                            gameply++;
                        }

                        b.doMove(m, state[ply]);
                    }

                    // 対局終了。勝ったほうには1,負けたほうには0のフラグを立てておく。
                    Turn winner = psv[gameply].deep >= 0 ? b.turn() : ~b.turn();
                    const Turn start_turn = (move_count & 1) ? WHITE : BLACK;

                    for (int i = 0; i < gameply; i++)
                    {
                        Turn t = (i & 1) ? ~start_turn : start_turn;
                        psv[i].win = t == winner;
                        sw.write(idx, psv[i]);
                    }
                }

                sw.finalizeWriter(idx);

                if (isMain())
                {
                    sw.terminate();
                    SYNC_COUT << "gensfen end." << SYNC_ENDL;
                }

                delete tt;
                tt = &GlobalTT;
            }
        };
    } // namespace OnlineSpace

    void onlineLearning(Board& b, std::istringstream& is)
    {
        uint64_t mini_batch_size = 100000;
        int search_depth = 3;
        int multi_pv = 0;
        int multi_pv_range = 1000;
        int limit_score = -1000;
        int update_exceed_count_threshold = 100;
        int thread_num = USI::Options["Threads"];
        double update_training_winrate_threshold = 0.5;
        double eta = 64.0f;

        while (true)
        {
            std::string option;
            is >> option;

            if (option == "")
                break;
            else if (option == "bat")
                is >> mini_batch_size;
            else if (option == "depth")
                is >> search_depth;
            else if (option == "multi_pv")
                is >> multi_pv;
            else if (option == "multi_pv_range")
                is >> multi_pv_range;
            else if (option == "limit_score")
                is >> limit_score;
            else if (option == "exceed_count")
                is >> update_exceed_count_threshold;
            else if (option == "training_winrate")
                is >> update_training_winrate_threshold;
            else if (option == "eta")
                is >> eta;
            else if (option == "thread")
                is >> thread_num;
        }

        if (multi_pv < 0)
        {
            std::cout << "multi_pv >= 0" << std::endl;
            return;
        }

        if (update_training_winrate_threshold < 0.5 || update_training_winrate_threshold >= 1.0)
        {
            std::cout << "0.5 <= training_winrate < 1.0" << std::endl;
            return;
        }

        // 同一局面の出現確率はmini_bati_sizeに比例し、multi_pvのmulti_pv_move_count乗に反比例する。
        // なぜかはよくわからないがsearch_depthには反比例する。（同一局面でも探索結果が同じになりにくいため？）
        // multi_pv_move_countはこれらの値から決める。(少しでもハイパーパラメータを減らすため）
        const double P = 0.1 / 1000000.0;
        int move_count = multi_pv ? std::ceil(log(mini_batch_size / (search_depth * P)) / log(multi_pv)) : 20;

        std::cout << "\nLoss Function   : " << "cross entoropy";
        std::cout << "\nmini-batch size : " << mini_batch_size;
        std::cout << "\nsearch depth    : " << search_depth;
        std::cout << "\nmultipv count   : " << move_count;
        std::cout << "\nmultipv         : " << multi_pv;
        std::cout << "\nmultipv range   : " << multi_pv_range;
        std::cout << "\nlimit score     : " << limit_score;
        std::cout << "\nexceed count    : " << update_exceed_count_threshold;
        std::cout << "\nteacher winrate : " << update_training_winrate_threshold;
        std::cout << "\nthread          : " << thread_num;
        std::cout << "\neta             : " << eta;
        std::cout << "\ninit..";

        USI::isready();

        OnlineSpace::search_depth = search_depth;
        OnlineSpace::mini_batch_size = mini_batch_size;
        OnlineSpace::multi_pv = multi_pv;
        OnlineSpace::move_count = move_count;
        OnlineSpace::multi_pv_range = (Score)multi_pv_range;
        OnlineSpace::limit_score = (Score)limit_score;
        OnlineSpace::update_exceed_count_threshold = update_exceed_count_threshold;
        OnlineSpace::update_training_winrate_threshold = update_training_winrate_threshold;
        OnlineSpace::teacher_base = new Eval::Evaluater;
        *OnlineSpace::teacher_base = *Eval::GlobalEvaluater;
        Eval::Weight::eta = (LearnFloatType)eta;
        GlobalTT.resize(1);

        //OnlineSpace::teacher_base->clear();
        //OnlineSpace::teacher_base->load("eval/kppt/");
        
        //Eval::GlobalEvaluater->clear();
        //Eval::GlobalEvaluater->load("eval/kppt/");

        Eval::initGrad();
        std::cout << "init done." << std::endl;

        Threads.startWorkers<OnlineSpace::OnlineLearnThread>(thread_num);
    }

    void genSfen(Board& b, std::istringstream& is)
    {
        int thread_num = USI::Options["Threads"];
        uint64_t loop_max = 8000000000;
        int search_depth = 8;
        int multi_pv = 0;
        int multi_pv_range = 200;
        int limit_score = -2000;

        std::string dir = "";
        std::string file_name = "generated_kifu_" + timeStamp() + ".bin";
        std::string token;

        while (true)
        {
            token = "";
            is >> token;

            if (token == "")
                break;
            else if (token == "depth")
                is >> search_depth;
            else if (token == "loop")
                is >> loop_max;
            else if (token == "file")
                is >> file_name;
            else if (token == "dir")
                is >> dir;
            else if (token == "thread")
                is >> thread_num;
            else if (token == "multi_pv")
                is >> multi_pv;
            else if (token == "multi_pv_range")
                is >> multi_pv_range;
            else if (token == "limit_score")
                is >> limit_score;
        }

        if (multi_pv < 0)
        {
            std::cout << "multi_pv >= 0" << std::endl;
            return;
        }

        const double P = 0.1 / 1000000.0;
        int move_count = multi_pv ? std::ceil(log(loop_max / (search_depth * P)) / log(multi_pv)) : 30;
        file_name = path(dir, file_name);

        std::cout << "\ndepth           : " << search_depth;
        std::cout << "\nmultipv count   : " << move_count;
        std::cout << "\nmultipv         : " << multi_pv;
        std::cout << "\nmultipv range   : " << multi_pv_range;
        std::cout << "\nlimit score     : " << limit_score;
        std::cout << "\nthread          : " << thread_num;
        std::cout << "\nfile            : " << file_name;
        std::cout << "\nloop            : " << loop_max;
        std::cout << "\ninit..";

        USI::isready();
        Search::clear();
        OnlineSpace::search_depth = search_depth;
        OnlineSpace::multi_pv = multi_pv;
        OnlineSpace::move_count = move_count;
        OnlineSpace::multi_pv_range = (Score)multi_pv_range;
        OnlineSpace::limit_score = (Score)limit_score;
        OnlineSpace::sw.initWriter(thread_num, file_name);
        OnlineSpace::sw.startWriter();
        OnlineSpace::GenSfenThread::reseed();
        std::cout << "init done." << std::endl;

        // 生成開始！
        Threads.startWorkers<OnlineSpace::GenSfenThread>(thread_num);
    }

    // sfenファイルをチェックするコマンド
    void cleanSfen(Board& b, std::istringstream& is)
    {
        USI::isready();
        Learn::PackedSfenValue p;
        const size_t sf_size = sizeof(Learn::PackedSfenValue);
        std::string sfen;
        uint64_t sfen_count = 0;
        uint64_t reverse_eval = 0;

        while (true)
        {
            sfen.clear();
            is >> sfen;

            // ファイル名が終わったら終わり。
            if (sfen == "")
                break;
#if 0 // Nバイト目以降を切り捨てたい場合に使えるかも？
            auto hFile = CreateFile(sfen.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL);
            if (hFile == INVALID_HANDLE_VALUE) {
                std::cout << "\n" << sfen << " not found." << std::endl;
                continue;
            }
            SetFilePointer(hFile, sizeof(PackedSfenValue) * 798674126, NULL, FILE_BEGIN); // NULLじゃだめだよ！
            SetEndOfFile(hFile);
            CloseHandle(hFile);
            return;
#endif
            std::ifstream fs(sfen, std::ios::binary);

            // 読み込みに失敗したら次のファイル。
            if (!fs)
            {
                std::cout << "\n" << sfen << " not found." << std::endl;
                continue;
            }

            std::cout << "\ntesting " << sfen << "." << std::endl;
            size_t n = 0;
#if 0
            std::ofstream os("merge" + timeStamp() + ".bin", std::ios::binary);
#endif

           // fs.seekg(sizeof(PackedSfenValue) * 798674126, std::ios_base::cur);
            while ((n = fs.read((char*)&p, sf_size).gcount()) > 0)
            {
                if (n != sf_size)
                    break;

                sfen_count++;
#if 0
                if (sfen_count != 22444853 && sfen_count != 22444853)
                    os.write((char*)&p, sf_size);
#else
                //if (sfen_count > 44954)
                {
                    if (sfen_count % 200000 == 0)
                        std::cout << ".";

                    b.setFromPackedSfen(p.data);

                    //SYNC_COUT << b << p.deep << " win = " << p.win << SYNC_ENDL;
                    if ((p.win && p.deep < -2000) || (!p.win && p.deep > 2000))
                    {
                       // SYNC_COUT << b << p.deep << " win = " << p.win << SYNC_ENDL;
                        reverse_eval++;
                    }

                    if (!b.verify())
                        SYNC_COUT << "NO!" << SYNC_ENDL;
                }
#endif
            }

            //os.flush();
        }

        SYNC_COUT << "sfen_count = " << sfen_count << " reverse = " << reverse_eval << " end" << SYNC_ENDL;
    }
#ifdef EVAL_KPPT
    // 評価関数をブレンドする。
    void blend(Board& b, std::istringstream& is)
    {
        std::string token;
        std::string eval1, eval2, out = "blend" + timeStamp();
        float ratio1, ratio2;

        while (true)
        {
            token = "";
            is >> token;

            if (token == "")
                break;
            else if (token == "eval1")
                is >> eval1;
            else if (token == "eval2")
                is >> eval2;
            else if (token == "out")
                is >> out;
            else if (token == "ratio1")
                is >> ratio1;
            else if (token == "ratio2")
                is >> ratio2;
        }

        Eval::Evaluater* e1 = new Eval::Evaluater;
        Eval::Evaluater* e2 = new Eval::Evaluater;
        e1->load(eval1);
        e2->load(eval2);
        e1->blend(ratio1, *e2, ratio2);
        e1->save(out, true);
        delete e1;
        delete e2;
    }
#endif
} // namespace Learn

#endif
