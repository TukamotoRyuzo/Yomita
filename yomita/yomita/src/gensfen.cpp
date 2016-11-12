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

#include "gensfen.h"

#ifdef GENSFEN

#include <sstream>
#include "usi.h"
#include "search.h"

namespace Learn
{
    void MultiThinkGenSfen::work(size_t thread_id)
    {
        const int MAX_PLY = 256;
        StateInfo state[MAX_PLY + 64];
        int ply;
        Move m = MOVE_NONE;
        auto th = Threads[thread_id];
        auto& b = th->root_board;
        b.init(USI::START_POS, th);

        while (true)
        {
            b.init(USI::START_POS, th);

            for (ply = 0; ply < MAX_PLY - 16; ++ply)
            {
                if (b.isMate())
                    break;

                switch (b.repetitionType(16))
                {
                case NO_REPETITION: case REPETITION_SUPERIOR: case REPETITION_INFERIOR: break;
                case REPETITION_DRAW: case REPETITION_WIN: case REPETITION_LOSE: goto CONTINUE;
                default: UNREACHABLE;
                }

                // 序盤においてrandomな手を指させることで、進行をばらけさせる。
                if (b.ply() <= 20)
                {
                    MoveList<LEGAL> ml(b);

                    if (ml.size() == 0)
                        break;

                    int play = 0;

                    do {
                        m = ml.begin()[rand(ml.size())].move;
                    } while (isCapture(m) && ++play < 10);
                }
                else
                {
                    // search_depth手読みの評価値とPV(最善応手列)
                    // 進行をばらけさせるため、読みの深さはランダムな確立で+1させる。
                    auto pv_value1 = Learn::search(b, -SCORE_INFINITE, SCORE_INFINITE, search_depth + (int)rand(2));
                    auto value1 = pv_value1.first;
                    auto pv1 = pv_value1.second;

                    // 評価値の絶対値がこの値以上の局面については
                    // その局面を学習に使うのはあまり意味がないのでこの試合を終了する。
                    if (abs(value1) >= eval_limit)
                        break;

                    // 局面を書き出そうと思ったら規定回数に達していた。
                    if (getNextLoopCount() == UINT64_MAX)
                        goto FINALIZE;

                    uint8_t data[32];

                    // packを要求されているならpackされたsfenとそのときの評価値を書き出す。
                    b.sfenPack(data);

                    // このwriteがスレッド排他を行うので、ここでの排他は不要。
                    sw.write(thread_id, data, value1);

#ifdef TEST_UNPACK_SFEN

                    // sfenのpack test
                    // pack()してunpack()したものが元のsfenと一致するのかのテスト。

                    //      b.sfen_pack(data);
                    auto sfen = b.sfenUnPack(data);
                    auto b_sfen = b.sfen();
                    Board bb;
                    bb.setFromPackedSfen(data);
                    auto bb_sfen = bb.sfen();
                    // 手数の部分の出力がないので異なる。末尾の数字を消すと一致するはず。
                    auto trim = [](std::string& s)
                    {
                        while (true)
                        {
                            auto c = *s.rbegin();
                            if (c < '0' || '9' < c)
                                break;
                            s.pop_back();
                        }
                    };
                    trim(sfen);
                    trim(b_sfen);
                    trim(bb_sfen);

                    if (sfen != b_sfen || sfen != bb_sfen)
                    {
                        std::cout << "Error: sfen packer error\n" << sfen << std::endl << b_sfen << std::endl;
                    }
#endif

#if 0
                    // デバッグ用に局面と読み筋を表示させてみる。
                    std::cout << b;
                    std::cout << "search() PV = ";
                    for (auto pv_move : pv1)
                        std::cout << pv_move << " ";
                    std::cout << std::endl;

                    // 静止探索のpvは存在しないことがある。(駒の取り合いがない場合など)　その場合は、現局面がPVのleafである。
                    //std::cout << "qsearch() PV = ";
                    //for (auto pv_move : pv2)
                    //	std::cout << pv_move << " ";
                    //std::cout << std::endl;
#endif

#ifdef TEST_LEGAL_LEAF
                // デバッグ用の検証として、
                // PVの指し手でleaf nodeまで進めて、非合法手が混じっていないかをテストする。
                    auto go_leaf_test = [&](auto pv)
                    {
                        int ply2 = ply;
                        for (auto m : pv)
                        {
                            // 非合法手はやってこないはずなのだが。
                            if (!b.pseudoLegal(m) || !b.legal(m))
                            {
                                std::cout << b << m << std::endl;
                                assert(false);
                            }
                            b.doMove(m, state[ply2++]);
                        }
                        // leafに到達
                        //      std::cout << b;

                        // 巻き戻す
                        auto pv_r = pv;
                        std::reverse(pv_r.begin(), pv_r.end());
                        for (auto m : pv_r)
                            b.undoMove(m);
                    };

                    go_leaf_test(pv1); // 通常探索のleafまで行くテスト
                                       //      go_leaf_test(pv2); // 静止探索のleafまで行くテスト
#endif
                    
                    {
                        // 3手読みの指し手で局面を進める。
                        m = pv1[0];
                    }
                }

                b.doMove(m, state[ply]);
            }
        CONTINUE:;
        }
    FINALIZE:;
        sw.finalize(thread_id);
    }

    void genSfen(Board& b, std::istringstream& is)
    {
        uint32_t thread_num = USI::Options["Threads"];
        uint64_t loop_max = 8000000000;
        int eval_limit = 2000;
        int search_depth = 6;

        std::string dir = "";
        std::string file_name =	"generated_kifu_" + std::to_string(now()) + ".bin";
        std::string token;

        while (true)
        {
            token = "";
            is >> token;
            if (token == "")
                break;

            if (token == "depth")
                is >> search_depth;
            else if (token == "loop")
                is >> loop_max;
            else if (token == "file")
                is >> file_name;
            else if (token == "eval_limit")
                is >> eval_limit;
            else if (token == "dir")
                is >> dir;
        }

        is >> search_depth >> loop_max;

        file_name = path(dir, file_name);

        std::cout << "gen_sfen : "
            << "depth = " << search_depth
            << " , loop = " << loop_max
            << " , eval_limit = " << eval_limit
            << " , thread = " << thread_num
            << " , file = " << file_name
            << std::endl;

        // SfenWriterのデストラクタでjoinするので、joinが終わってから終了したというメッセージを
        // 表示させるべきなのでここをブロックで囲む。
        {
            // file_nameを開き、thread_num個のpackedsfenのvectorを用意する。
            SfenWriter sw(file_name, thread_num);
            MultiThinkGenSfen multi_think(search_depth, sw);
            multi_think.setLoopMax(loop_max);
            multi_think.eval_limit = eval_limit;
            multi_think.startFileWriteWorker();
            multi_think.think();
        }
            
        std::cout << "gen_sfen finished." << std::endl;
    }

    // 細切れになったsfenファイルを一纏めにするコマンド。
    void mergeSfen(std::istringstream & is)
    {
        // 評価関数の読み込みを済ませないとバグる。
        USI::isReady();

        // 出力ファイル名は"merged_sfen_" + タイムスタンプ.bin
        const std::string output_filename = "merged_sfen_" + std::to_string(now()) + ".bin";

        // read_size単位で読み込む。
        const size_t read_size = 100000;
        Learn::PackedSfenValue *p = new Learn::PackedSfenValue[read_size];

        const size_t sf_size = sizeof(Learn::PackedSfenValue);
        std::ofstream os;
        std::string sfen;
        uint64_t sfen_count = 0;

        while (true)
        {
            sfen.clear();

            // 読み込むべきファイル名
            is >> sfen;

            // ファイル名が終わったら終わり。
            if (sfen == "")
                break;

            std::ifstream fs(sfen, std::ios::binary);
            
            // 読み込みに失敗したら次のファイル。
            if (!fs)
            {
                std::cout << "\n" << sfen << " not found." << std::endl;
                continue;
            }
        
            if (sfen_count == 0)
                os.open(output_filename, std::ios::binary);

            std::cout << "\nmerging " << sfen << "." << std::endl;

            size_t n = 0;

            while ((n = fs.read((char*)p, sf_size * read_size).gcount()) > 0)
            {
                if (n < read_size * sf_size)
                {
                    // 端数を切り捨てたい。
                    n /= sf_size;
                    n *= sf_size;
                }
                sfen_count += n / sf_size;
                std::cout << ".";

                os.write((const char*)&(p[0].data), n);
                os.flush();
            }
        }

        if (sfen_count == 0)
            std::cout << "files are not merged." << std::endl;
        else
            std::cout << "\nmerge end! sfen = " << sfen_count 
            << "\noutput file = " << output_filename << std::endl;

        delete[] p;
    }

#ifdef LEARN
    // 複数スレッドでSfenのテストを行うためのクラス
    struct SfenTester : public MultiThink
    {
        SfenTester(SfenReader& sr_) :sr(sr_) {}
        virtual void work(size_t thread_id);

        // 局面ファイルをバックグラウンドで読み込むスレッドを起動する。
        void startFileReadWorker() { sr.startFileReadWorker(); }

        // sfenの読み出し器
        SfenReader& sr;
    };

    // 生成されたpacked sfenファイルで盤面再生できるかのテストコマンド。
    void testPackedSfenFile(std::istringstream& is)
    {
        auto thread_num = (int)USI::Options["Threads"];
        SfenReader sr(thread_num);
        SfenTester tester(sr);
        std::vector<std::string> filenames;

        // 棋譜ファイルが格納されているフォルダ
        std::string dir = "";

        // ファイル名が後ろにずらずらと書かれていると仮定している。
        while (true)
        {
            std::string option;
            is >> option;

            if (option == "")
                break;

            if (option == "dir")
                is >> dir;
            else
                filenames.push_back(option);
        }

#if 1
        // 学習棋譜ファイルの表示
        std::cout << "test from ";
        for (auto s : filenames)
            std::cout << s << " , ";
#endif

        std::cout << "packtest, dir = " << dir << std::endl;

        // sfen reader、逆順で読むからここでreverseしておく。すまんな。
        for (auto it = filenames.rbegin(); it != filenames.rend(); ++it)
            sr.filenames.push_back(path(dir, *it));

        // 局面ファイルをバックグラウンドで読み込むスレッドを起動
        tester.startFileReadWorker();

        // test start!
        tester.think();

        std::cout << "test passed." << std::endl;
    }

    void SfenTester::work(size_t thread_id)
    {
        auto th = Threads[thread_id];
        auto& b = th->root_board;

        uint64_t count = 0;

        while (true)
        {
            // バッファからsfenを一つ受け取る。
            PackedSfenValue ps;

            if (!sr.getSfen(thread_id, ps))
                break;

            b.setFromPackedSfen(ps.data);

            //SYNC_COUT << b << SYNC_ENDL;

            if (!b.verify())
                SYNC_COUT << "NO!" << SYNC_ENDL;

            count++;

            if (count % 200000 == 0)
            {
                static uint64_t sc = 0;
                if (th->isMain() && ++sc % 10 == 0)
                {
                    SYNC_COUT << std::endl << sr.total_read << " sfens , at " << localTime() << SYNC_ENDL;
                }
                else
                    std::cout << ".";
            }
        }

        if (th->isMain())
            SYNC_COUT << std::endl << sr.total_read << " sfens , at " << localTime() << SYNC_ENDL;
    }
#endif
} // namespace Learn

#endif