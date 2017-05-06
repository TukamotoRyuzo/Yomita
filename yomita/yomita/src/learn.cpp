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

// やねうら王midで使われている学習ルーチンです。

#include "learn.h"

#ifdef LEARN

#include <fstream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include "search.h"
#include "usi.h"
#include "sfen_rw.h"
#include "multi_think.h"

namespace Learn
{
    // 簡易なsearch。評価値とpvを返す。
    extern std::pair<Score, std::vector<Move>> qsearch(Board& b, Score alpha, Score beta);

    // シグモイド関数
    double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

    // 評価値を勝率[0,1]に変換する関数
    double winest(Score score) { return sigmoid(static_cast<int>(score) / 600.0); }

    // シグモイド関数の導関数。
    double dsigmoid(double x) { return sigmoid(x) * (1.0 - sigmoid(x)); }

    // 勾配を計算する関数
    double calcGrad(Score deep, Score shallow)
    {
        // 勝率の差の2乗が目的関数。それを最小化する。
        double p = winest(deep);
        double q = winest(shallow);
#if defined LOSS_FUNCTION_IS_WINNING_PERCENTAGE
        return (q - p) * dsigmoid(double(shallow) / 600.0);
#elif defined LOSS_FUNCTION_IS_CROSS_ENTOROPY
        return q - p;
#endif
    }

    // 誤差を計算する関数(rmseの計算用)
    // これは尺度が変わるといけないので目的関数を変更しても共通の計算にしておく。勝率の差の二乗和。
    double calcError(Score record_score, Score score)
    {
        double diff = winest(score) - winest(record_score);
        return diff * diff;
    }

    // 学習に対応している評価関数のときだけ定義。
#if defined EVAL_KPPT || defined EVAL_PPT || defined EVAL_PPTP || defined EVAL_KPPTP

    // 複数スレッドで学習するためのクラス
    struct LearnerThink : public MultiThink
    {
        LearnerThink(SfenReader& sr_) :sr(sr_), updating(false) {}
        virtual void work(size_t thread_id);

        // mseの計算用に1万局面ほど読み込んでおく。
        bool initMse();

        // rmseを計算して表示する。
        void calcRmse();

        // 局面ファイルをバックグラウンドで読み込むスレッドを起動する。
        void startFileReadWorker() { sr.startFileReadWorker(); }

        // 評価関数パラメーターをファイルに保存
        void save();

        // sfenの読み出し器
        SfenReader& sr;

        // mse計算用のバッファ
        std::vector<PackedSfenValue> sfen_for_mse;

        // 学習の反復回数のカウンター
        uint64_t epoch = 0;

        // ミニバッチサイズのサイズ。必ずこのclassを使う側で設定すること。
        uint64_t mini_batch_size = 1000 * 1000;

        // updateWeightsしている途中であることを表すフラグ
        std::atomic_bool updating;
    };

    void LearnerThink::work(size_t thread_id)
    {
        auto th = Threads[thread_id];
        auto& b = th->root_board;

        while (true)
        {
            // mseの表示(これはthread 0のみときどき行う)
            // ファイルから読み込んだ直後とかでいいような…。
            if (th->isMain() && sr.next_update_weights <= sr.total_read)
            {
                assert(thread_id == 0);
                uint64_t total_read_count = sr.total_read;

                // 現在時刻を出力
                static uint64_t sfens_output_count = 0;

                if ((sfens_output_count++ % LEARN_TIMESTAMP_OUTPUT_INTERVAL) == 0)
                    std::cout << std::endl << sr.total_read << " sfens , at " << localTime();
                else
                    std::cout << '.';

                // このタイミングで勾配をweight配列に反映。勾配の計算も1M局面ごとでmini-batch的にはちょうどいいのでは。
#ifdef SYNC_UPDATE_WEIGHT
                updating = true;

                // 他のスレッドがすべてaddGradし終えるのを待つ。
                for (auto t : Threads.slaves)
                {
                    std::unique_lock<Mutex> lk(t->update_mutex);
                    t->cond.wait(lk, [&] { return !t->add_grading; });
                }
#endif
                // 3回目ぐらいまではg2のupdateにとどめて、wのupdateは保留する。
                Eval::updateWeights(mini_batch_size, ++epoch);

#ifdef SYNC_UPDATE_WEIGHT
                updating = false;

                for (auto t : Threads.slaves)
                    t->cond.notify_one();
#endif
                // 20回、update_weight()するごとに保存。
                // 例えば、LEARN_MINI_BATCH_SIZEが1Mなら、1M×100 = 0.1G(1億)ごとに保存
                // ただし、updateWeights(),calcRmse()している間の時間経過は無視するものとする。
                if (++sr.save_count >= 100000000 / mini_batch_size)
                {
                    sr.save_count = 0;

                    // 定期的に保存
                    uint64_t change_name_size = (uint64_t)EVAL_FILE_NAME_CHANGE_INTERVAL;
                    Eval::saveEval(std::to_string(sr.total_read / change_name_size));
                }

                // rmseを計算する。1万局面のサンプルに対して行う。
                static uint64_t rmse_output_count = 0;

                if ((rmse_output_count++ % LEARN_RMSE_OUTPUT_INTERVAL) == 0)
                    calcRmse();

                // 次回、この一連の処理は、
                // total_read_count + LEARN_MINI_BATCH_SIZE <= total_read
                // となったときにやって欲しいのだけど、それが現在時刻(toal_read_now)を過ぎているなら
                // 仕方ないのでいますぐやる、的なコード。
                uint64_t total_read_now = sr.total_read;
                sr.next_update_weights = std::max(total_read_count + mini_batch_size, total_read_now);
            }

            // バッファからsfenを一つ受け取る。
            PackedSfenValue ps;

            if (!sr.getSfen(thread_id, ps))
                break;

            b.setFromPackedSfen(ps.data);
            b.setThread(Threads[thread_id]);

            // 浅い探索の評価値を得る
#ifdef USE_QSEARCH_FOR_SHALLOW_VALUE
            auto r = Learn::qsearch(b, -SCORE_INFINITE, SCORE_INFINITE);
            auto shallow_value = r.first;
#else
            auto shallow_value = Eval::evaluate(b);
#endif
            
            // 深い探索の評価値
            auto deep_value = (Score)*(int16_t*)&ps.data[32];
            //SYNC_COUT << b << "shallow = " << shallow_value << "deep = " << deep_value << SYNC_ENDL;
            // 勾配
            double dj_dw = calcGrad(deep_value, shallow_value);

            // 現在、leaf nodeで出現している特徴ベクトルに対する勾配(∂J/∂Wj)として、dj_dwを加算する。
            auto rootColor = b.turn();

#ifdef USE_QSEARCH_FOR_SHALLOW_VALUE
            auto pv = r.second;
            int ply = 0;
            StateInfo state[MAX_PLY]; // qsearchのPVがそんなに長くなることはありえない。

            for (auto m : pv)
            {
                b.doMove(m, state[ply++]);
#ifdef USE_PROGRESS
                Prog::evaluateProgress(b);
#endif
            }
#endif

#ifdef SYNC_UPDATE_WEIGHT
            // updateWeights中にaddGradしないためのwait機構。
            th->add_grading = false;

            // update中なので、addGradする前にメインスレッドがupdateを終えるのを待つ。
            while (updating)
            {
                std::unique_lock<Mutex> lk(th->update_mutex);

                // join待ちしているメインスレッドに通知。
                th->cond.notify_one();

                // updateスレッドが終わるのを知らせてくれるまで待つ。
                th->cond.wait(lk, [&] { return !updating; });
            }

            th->add_grading = true;
#endif
            // 現局面でevaluate()するので現局面がleafだと考えられる。
            Eval::addGrad(b, rootColor, dj_dw);
        }
    }

    bool LearnerThink::initMse()
    {
        for (int i = 0; i < 10000; ++i)
        {
            PackedSfenValue ps;

            if (!sr.getSfen(0, ps))
            {
                std::cout << "Error! read packed sfen , failed." << std::endl;
                return false;
            }

            sfen_for_mse.push_back(ps);
        }

        return true;
    }

    void LearnerThink::calcRmse()
    {
        const int thread_id = 0;
        auto& b = Threads[thread_id]->root_board;

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
            b.setThread(Threads[thread_id]);

#ifdef USE_QSEARCH_FOR_SHALLOW_VALUE
            auto r = Learn::qsearch(b, -SCORE_INFINITE, SCORE_INFINITE);
            Score shallow_value = r.first;
#else
            Score shallow_value = Eval::evaluate(b);
#endif
            // 深い探索の評価値
            auto deep_value = (Score)*(int16_t*)&ps.data[32];
#if 0
            if ((i++ % 1000) == 0)
                SYNC_COUT << "shallow = " << std::setw(5) << shallow_value
                          << " deep = "   << std::setw(5) << deep_value << SYNC_ENDL;
#endif
            // 誤差の計算
            sum_error += calcError(shallow_value, deep_value);
            sum_error2 += abs(shallow_value - deep_value);
        }

        auto rmse = std::sqrt(sum_error / sfen_for_mse.size());
        auto mean_error = sum_error2 / sfen_for_mse.size();
        std::cout << std::endl << "rmse = " << rmse << " , mean_error = " << mean_error << std::endl;
    }

    void LearnerThink::save()
    {
        // 定期的に保存
        // 10億局面ごとにファイル名の拡張子部分を"0","1","2",..のように変えていく。
        // (あとでそれぞれの評価関数パラメーターにおいて勝率を比較したいため)
        uint64_t change_name_size = uint64_t(1000) * 1000 * 1000;
        Eval::saveEval(std::to_string(sr.total_read / change_name_size));
    }

    // 生成した棋譜からの学習
    void learn(Board& b, std::istringstream& is)
    {
        auto thread_num = (int)USI::Options["Threads"];
        SfenReader sr(thread_num);

        LearnerThink learn_think(sr);
        std::vector<std::string> filenames;

        // mini_batch_size デフォルトで1M局面。これを大きくできる。
        auto mini_batch_size = LEARN_MINI_BATCH_SIZE;

        // ループ回数(この回数だけ棋譜ファイルを読み込む)
        int loop = 1;

        // 棋譜ファイルが格納されているフォルダ
        std::string dir;

        // ファイル名が後ろにずらずらと書かれていると仮定している。
        while (true)
        {
            std::string option;
            is >> option;

            if (option == "")
                break;

            if (option == "bat")
            {
                // mini-batchの局面数を指定
                is >> mini_batch_size;
                mini_batch_size *= 10000; // 単位は万
                continue;
            }
            else if (option == "loop")
            {
                // ループ回数の指定
                is >> loop;
                continue;
            }
            else if (option == "dir")
            {
                // 棋譜ファイル格納フォルダ
                is >> dir;
                continue;
            }

            filenames.push_back(option);
        }

#if 1
        // 学習棋譜ファイルの表示
        std::cout << "learn from ";
        for (auto s : filenames)
            std::cout << s << " , ";
#endif

        std::cout << "learn , dir = " << dir << " , loop = " << loop << std::endl;

        // ループ回数分だけファイル名を突っ込む。
        for (int i = 0; i < loop; ++i)
            // sfen reader、逆順で読むからここでreverseしておく。すまんな。
            for (auto it = filenames.rbegin(); it != filenames.rend(); ++it)
                sr.filenames.push_back(path(dir, *it));

        std::cout << "\nGradient Method : " << LEARN_UPDATE;
        std::cout << "\nLoss Function   : " << LOSS_FUNCTION;
        std::cout << "\nmini-batch size : " << mini_batch_size;
        std::cout << "\ninit..";

        // 評価関数パラメーターの読み込み
        USI::isReady();

        // 局面ファイルをバックグラウンドで読み込むスレッドを起動
        // (これを開始しないとmseの計算が出来ない。)
        learn_think.startFileReadWorker();

        learn_think.mini_batch_size = mini_batch_size;

        // mse計算用にデータ1万件ほど取得しておく。
        if (!learn_think.initMse())
            return;

        // 評価関数パラメーターの勾配配列の初期化
        Eval::initGrad();

        std::cout << "init done." << std::endl;

        // 学習開始。
        learn_think.think();

        // 最後に一度保存。
        learn_think.save();
    }
#else
    void learn(Board& b, std::istringstream& is)
    {
        // 現在の評価関数は学習には対応していません。
        static_assert(false, "Evaluation that is currently selected, does not supported the learning routine.");
    }
#endif
} // namespace Learn

#endif