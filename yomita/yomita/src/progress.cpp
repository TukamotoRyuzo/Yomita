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

#include "config.h"

#ifdef USE_PROGRESS

#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>

#include "usi.h"
#include "board.h"
#include "learn.h"
#include "progress.h"

#define PROGRESS_BIN "progress.bin"
#define SAVE_PROGRESS_DIR "save"

namespace Progress
{
    int32_t PROGRESS[SQ_MAX][Eval::fe_end];

    // 進行度ファイルの読み込み
    void load()
    {
        std::string p = path(USI::Options["ProgressDir"], PROGRESS_BIN);
        std::ifstream ifs(p, std::ios::binary);

        if (!ifs)
            std::cout << "info string can't open " << p << "." << std::endl;

        else
        {
            ifs.read(reinterpret_cast<char*>(PROGRESS), sizeof(PROGRESS));

#if 0 // 実験的な初期化コード
            {
                PRNG rng(11);
                for (auto sq : Squares)
                    for (int i = 0; i < Eval::fe_end; i++)
                        PROGRESS[sq][i] = 0;// rng.rand<ValueProg>();
            }
#endif
            std::cout << "info string open success " << p << "." << std::endl;
        }
    }

    // 進行度ファイルの保存
    void save(std::string dir_name)
    {
        std::string prog_dir = path("progress/save", dir_name);
        _mkdir(prog_dir);
        std::ofstream ofs(path(prog_dir, PROGRESS_BIN), std::ios::binary);

        if (!ofs.write(reinterpret_cast<char*>(PROGRESS), sizeof(PROGRESS)))
            std::cout << "can't save progress." << std::endl;
    }

    double computeAll(const Board& b)
    {
        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk1 = inverse(b.kingSquare(WHITE));
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();
        int64_t bkp = 0, wkp = 0;

        for (int i = 0; i < PIECE_NO_KING; i++)
        {
            bkp += PROGRESS[sq_bk0][list_fb[i]];
            wkp += PROGRESS[sq_wk1][list_fw[i]];
        }

        b.state()->progress.set(bkp, wkp);

        return b.state()->progress.rate();
    }

    // 進行度を差分計算
    double computeDiff(const Board& b)
    {
        auto now = b.state();

        if (!now->progress.isNone())
            return now->progress.rate();

        auto prev = now->previous;

        if (prev->progress.isNone())
            return computeAll(b);

        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk1 = inverse(b.kingSquare(WHITE));
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();
        auto& dp = now->dirty_piece;
        auto prog = prev->progress;
        auto dirty = dp.piece_no[0];
        int k = dp.dirty_num;

        if (dirty >= PIECE_NO_KING)
        {
            if (dirty == PIECE_NO_BKING)
            {
                prog.bkp = 0;

                for (int i = 0; i < PIECE_NO_KING; i++)
                    prog.bkp += PROGRESS[sq_bk0][list_fb[i]];

                if (k == 2)
                {
                    prog.wkp -= PROGRESS[sq_wk1][dp.pre_piece[1].fw];
                    prog.wkp += PROGRESS[sq_wk1][dp.now_piece[1].fw];
                }
            }
            else
            {
                prog.wkp = 0;

                for (int i = 0; i < PIECE_NO_KING; i++)
                    prog.wkp += PROGRESS[sq_wk1][list_fw[i]];

                if (k == 2)
                {
                    prog.bkp -= PROGRESS[sq_bk0][dp.pre_piece[1].fb];
                    prog.bkp += PROGRESS[sq_bk0][dp.now_piece[1].fb];
                }
            }
        }
        else
        {
#define ADD_BWKP(W0,W1,W2,W3) \
        prog.bkp -= PROGRESS[sq_bk0][W0]; \
        prog.wkp -= PROGRESS[sq_wk1][W1]; \
        prog.bkp += PROGRESS[sq_bk0][W2]; \
        prog.wkp += PROGRESS[sq_wk1][W3]; \

            if (k == 1)
            {
                ADD_BWKP(dp.pre_piece[0].fb, dp.pre_piece[0].fw, dp.now_piece[0].fb, dp.now_piece[0].fw);
            }
            else if (k == 2)
            {
                ADD_BWKP(dp.pre_piece[0].fb, dp.pre_piece[0].fw, dp.now_piece[0].fb, dp.now_piece[0].fw);
                ADD_BWKP(dp.pre_piece[1].fb, dp.pre_piece[1].fw, dp.now_piece[1].fb, dp.now_piece[1].fw);
            }
        }

        now->progress = prog;

        return now->progress.rate();
    }

    double evaluate(const Board& b)
    {
        return computeDiff(b);
    }

    double ProgressSum::rate() const
    {
        return Learn::sigmoid(double(bkp + wkp) / double(1 << 16)); 
    }

} // namespace Progress

#ifdef LEARN
namespace Learn
{

#define eta 1.0
    struct Weight
    {
        double w, g, g2;

        void addGrad(double delta) { g += delta; }

        // 勾配値を重みに反映させる
        bool update()
        {
            if (g == 0)
                return false;

            g2 += g * g;
            double e = std::sqrt(g2);

            if (e != 0)
                w = w - g * eta / e;

            g = 0;

            return true;
        }
    };

    Weight prog_w[SQ_MAX][Eval::fe_end];

    // 勾配配列の初期化
    void initGrad()
    {
        memset(prog_w, 0, sizeof(prog_w));
#if 1
        for (auto k : Squares)
            for (auto p1 = Eval::BONA_PIECE_ZERO; p1 < Eval::fe_end; ++p1)
                prog_w[k][p1].w = Progress::PROGRESS[k][p1];
#endif
    }

    // 現在の局面で出現している特徴すべてに対して、勾配値を勾配配列に加算する。
    void addGrad(Board& b, double delta_grad)
    {
        auto f = delta_grad;
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFb();
        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk1 = inverse(b.kingSquare(WHITE));

        for (int i = 0; i < PIECE_NO_NB; ++i)
        {
            prog_w[sq_bk0][list_fb[i]].addGrad(f);
            prog_w[sq_wk1][list_fw[i]].addGrad(f);
        }
    }

    // 教師の数も少なく、現在の出力値を求める時間も少ないので、mini batchは必要ない。
    void updateWeights()
    {
        for (auto sq : Squares)
            for (int i = 0; i < (int)Eval::fe_end; ++i)
            {
                auto& w = prog_w[sq][i];

                if (w.update())
                    Progress::PROGRESS[sq][i] = static_cast<int32_t>(w.w);
            }
    }

    // 勾配を計算する関数
    double calcGrad(double teacher, double now)
    {
        return now - teacher;
    }

    // 誤差を計算する関数(rmseの計算用)
    double calcError(double teacher, double now)
    {
        double diff = now - teacher;
        return diff * diff;
    }

    struct Game
    {
        short ply;
        Move move[512];
    };

    typedef std::vector<Game> GameVector;

    namespace ProgressSpace
    {
        std::vector<GameVector> games;
        GameVector errors;
        uint64_t max_loop;

        struct ProgressLearnThread : public WorkerThread
        {
            virtual void search()
            {
                const int MAX_PLY = 256;
                StateInfo st[MAX_PLY + 64];
                Board& b = root_board;
                const bool is_main = isMain();
#ifdef _DEBUG
                const int interval = 5000;
#else
                const int interval = 10000000;
#endif
                for (uint64_t loop = 0; loop < max_loop && !Threads.stop;)
                {
                    for (auto game : games[idx])
                    {
                        // 平手初期局面セット
                        b.init(USI::START_POS, this);

                        // 再生
                        for (int i = 0; i < game.ply - 1; i++)
                        {
                            Move m = game.move[i];
                            b.doMove(m, st[i]);

                            // 教師の進行度
                            auto t = (double)(i + 1) / (double)game.ply;

                            // 出力
                            auto p = Progress::evaluate(b);

                            // 勾配ベクトル
                            auto delta = calcGrad(t, p);

                            addGrad(b, delta);

                            if (is_main)
                            {
#if 0
                                SYNC_COUT << b << "progress = " << p * 100.0 << "%\n"
                                    << "teacher = " << t * 100.0 << "%" << SYNC_ENDL;
#endif
#if 1
                                static int j = 0;
                                if (++j % interval == 0)
                                {
                                    SYNC_COUT << "now = " << std::setw(5) << std::setprecision(2) << p * 100 << "%"
                                        << " teacher = " << std::setw(5) << std::setprecision(2) << t * 100 << "%" << SYNC_ENDL;
                                }
#endif
                            }
                        }
                    }

                    // メインスレッドだけがWeightを変更する。
                    if (is_main)
                    {
                        int j = 0;
                        double sum_error = 0;

                        for (auto game : errors)
                        {
                            // 平手初期局面セット
                            b.init(USI::START_POS, this);

                            // 誤差の計算
                            for (int i = 0; i < game.ply - 1; i++)
                            {
                                Move m = game.move[i];
                                b.doMove(m, st[i]);
                                auto t = (double)(i + 1) / (double)game.ply;
                                auto p = Progress::evaluate(b);
                                sum_error += calcError(p, t);
                                j++;
                            }
                        }

                        auto rmse = std::sqrt(sum_error / j);
                        SYNC_COUT << std::endl << std::setprecision(8) << "rmse = " << rmse << SYNC_ENDL;

                        for (auto th : Threads.slaves)
                            th->join();

                        updateWeights();

                        for (auto th : Threads.slaves)
                            th->startSearching();

                        if (++loop % 100 == 0)
                        {
                            Progress::save(std::to_string(rmse));
                            SYNC_COUT << localTime() << SYNC_ENDL;
                        }
                    }

                    // その他のスレッドは待ってもらう。
                    else
                    {
                        searching = false;
                        startSearching(true);
                        wait(searching);
                        ++loop;
                    }
                }

                // 最後に一回保存！
                if (is_main)
                {
                    Progress::save("end");
                    SYNC_COUT << "learn_progress end." << SYNC_ENDL;
                }
            }
        };
    } // namespace ProgressSpace

    // 直接変換する
    Move csaToMove(std::string csa, Board& b)
    {
        File f;
        Rank r;
        Square from;
        Move ret;

        if (csa[0] != '0')
        {
            f = File('9' - csa[0]);
            r = Rank(csa[1] - '1');
            from = sqOf(f, r);
        }
        else
            from = SQ_MAX;

        f = File('9' - csa[2]);
        r = Rank(csa[3] - '1');
        Square to = sqOf(f, r);

        // 持ち駒を打つとき、USIにこの文字列を送信する。
        static const std::string piece_to_csa[] = { "", "KA", "HI", "FU", "KY", "KE", "GI", "KI", "OU", "UM", "RY", "TO", "NY", "NK", "NG" };
        PieceType pt;

        for (pt = BISHOP; pt < 15; pt++)
            if (piece_to_csa[pt] == csa.substr(4))
                break;

        assert(pt < 15);

        if (from == SQ_MAX)
            ret = makeDrop(pt | b.turn(), to);
        else
            ret = pt == typeOf(b.piece(from)) ? makeMove<LEGAL>(from, to, pt | b.turn(), b)
                                       : makeMovePromote<LEGAL>(from, to, b.piece(from), b);

        return ret;
    }

    // 進行度学習
    void learnProgress(Board& b, std::istringstream& is)
    {
        std::string token,
        file_name = "records_2800.txt",
        dir = "";

        // 最初は10000くらいで。
        uint64_t loop_max = 100000;

        while (true)
        {
            token.clear();
            is >> token;

            if (token == "")
                break;
            else if (token == "loop")
                is >> loop_max;
            else if (token == "file")
                is >> file_name;
            else if (token == "dir")
                is >> dir;
        }

        // 勾配配列の初期化
        Progress::load();
        initGrad();
        USI::isready();

        // file_nameは技巧形式の棋譜
        std::vector<std::string> kifus;
        readAllLines(path(dir, file_name), kifus);

        // 手数
        int max_ply;
        StateInfo st[512];
        int kifu_num = 0;
        int thread_size = USI::Options["Threads"];
        const int kifu_size = kifus.size() / 2;
        const int kifu_per_thread = static_cast<int>(std::ceil((double)kifu_size / (double)thread_size));

        ProgressSpace::max_loop = loop_max;

        for (int i = 0; i < thread_size; i++)
            ProgressSpace::games.push_back(GameVector());

        // どうせ学習棋譜は少ないのであらかじめすべてメモリに読み込んでおく。
        for (auto it = kifus.begin(); it < kifus.end(); ++it)
        {
            // (技巧の形式）
            // 1行目 : <棋譜番号> <対局開始日> <先手名> <後手名> <勝敗(0:引き分け, 1 : 先手勝ち, 2 : 後手勝ち)> <手数> <棋戦> <戦型>
            // 2行目 : <CSA形式の指し手(1手6文字)を一行に並べたもの>
            std::istringstream iss(*it++);

            // ゲームの手数を取得（plyが出てくるまで読み飛ばす)
            for (int i = 0; i < 6; i++)
                iss >> std::skipws >> token;

            Game g;

            g.ply = max_ply = atoi(token.c_str());

            iss.clear(std::stringstream::goodbit);
            iss.str(*it);

            // 平手初期局面セット
            b.init(USI::START_POS, Threads.main());

            // ゲームの進行
            for (int i = 0; i < max_ply - 1; i++)
            {
                token.clear();
                iss >> std::skipws >> token;
                Move m = csaToMove(token, b);
                g.move[i] = m;
                b.doMove(m, st[i]);
            }

            if (kifu_num++ <= kifu_size - 100)
                ProgressSpace::games[kifu_num / kifu_per_thread].push_back(g);
            else
                ProgressSpace::errors.push_back(g);
        }

        Threads.startWorkers<ProgressSpace::ProgressLearnThread>(thread_size);
    }
} // namespace Learn


#endif // LEARN
#endif // USE_PROGRESS
