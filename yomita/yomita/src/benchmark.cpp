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
#include "benchmark.h"
#include "search.h"
#include "thread.h"

uint64_t g[30];
using namespace std;

namespace USI
{
    extern void isReady();
    extern void position(Board& b, istringstream& up);
    extern void go(const Board& b, istringstream& ss_cmd);
    extern void setOption(istringstream& ss_cmd);
}

void benchmark(Board& b)
{
    USI::isReady();

    for (int i = 0; i < 30; i++)
        g[i] = 0;

        for (int i = 0; i < 30; i++)
            if (i != 19)
                g[i] = 0;

        // ここに探索時の条件を追加
        string options[] =
        {
            "name Threads value 1",
            "name Hash value 1024",
            "name USI_Ponder value true",
            "name Minimum_Thinking_Time value 15",
            "name Move_Overhead value 70",
            "name nodestime value 0",
            "name byoyomi_margin value 1000",
            "name Draw_Score value -100",
        };

        // ここに探索局面を追加
        const vector<string> positions =
        {
            // 5f4e
            //"startpos moves 5g5f",
            //"sfen l4+N2l/3s1+N3/2S3kpp/2p1pp3/1P1P2P1P/2PGPBg2/1nS2P3/3G1K3/P+r1N1b2L w Gr5pls 143"
            //"startpos moves 7g7f 8c8d 2g2f 8d8e 8h7g 3c3d 7i8h 4a3b 6i7h 2b7g+ 8h7g 3a4b 3i3h 7a7b 9g9f 6c6d 5i6h 7c7d 4i5h 7b6c 4g4f 6c5d 3h4g 5a4a 4g5f 4a3a 3g3f 4c4d 6h7i 6a5b 2i3g 6d6e 1g1f 1c1d 9f9e 8a7c 7i8h B*6d 2h4h 4b4c 4h4i 5b4b 2f2e 3a2b 5h6h 2a3c 5f4g 8e8f 7g8f 6d5e B*7g 5e7g+ 6h7g 7c8e 8f8e 8b8e N*2f 6e6f 6g6f 3c2e 3g2e 8e2e 4i2i 2b3a N*3g 2e8e 4f4e B*5e 2i2g 4d4e B*6a S*4h 4g5f 5e3g+ 2g3g 4h3g 2f3d 4c3d 6a3d+ 8e8a",
            //"startpos moves 7g7f 8c8d 2g2f 8d8e 8h7g 3c3d 7i8h 4a3b 6i7h 2b7g+ 8h7g 3a2b 3i3h 7a6b 4g4f 5a4b 4i5h 7c7d 3h4g 2b3c 5i6h 6b7c 4g5f 7c6d 6g6f 7d7e 6f6e 7e7f 7g7f 6d7c 5h6g 6c6d 6e6d 7c6d P*6e 6d7c",
            //"startpos moves 7g7f 3c3d 2g2f 8c8d 2f2e 8d8e 6i7h 4a3b 2e2d 2c2d 2h2d 8e8f 8g8f 8b8f 2d3d 2b3c 3d3f 8f8d 3f2f 3a2b P*8g 5a5b 5i5h 7c7d 3i3h 7a7b 3g3f",
            //"startpos moves 2g2f 3c3d 2f2e 2b3c 9g9f 8c8d 3i4h 7a6b 3g3f 4a3b 4h3g 8d8e 6i7h 3a2b 3g4f 7c7d 5i6h 3c4b 7g7f 5c5d 6h6i 6b5c 5g5f 5a4a 3f3e 3d3e 4f3e 8e8f 8g8f 4c4d 2h3h 8b8f P*8g",
            "startpos moves 2g2f 3c3d 2f2e 2b3c 7g7f 3a2b 5g5f 3c8h+ 7i8h B*5g 3g3f 1c1d 3i4h 5g1c+ B*7i 1c1b 5i6h 2b1c 6h7h 1b2b 7i4f 4c4d 4f5e 8b4b 8g8f 2b3b 8h8g 3b5d 4h3g 5d4e 5e7g 4e5f 4i5h 5c5d 3g4f 5f7d 3f3e 3d3e 4f3e 7d6d 4g4f 5d5e 2e2d 2c2d 3e2d 6d5d 2d1c+ P*2g 2h3h 1a1c P*3d 2g2h+ 3h2h S*2g S*6e 5d3f 2h4h P*2h P*5d 2h2i+ 5d5c+", // 250付近ｎ
            //"startpos moves 7g7f 3c3d 2g2f 8c8d 2f2e 8d8e 6i7h 4a3b 2e2d 2c2d 2h2d 8e8f 8g8f 8b8f 2d3d 2b3c 5i6h 3a2b 3g3f 8f8b 2i3g 3c8h+ 7i8h 2b3c P*8c 8b8c P*8d 8c8b 3d3e 8b8d B*6f 8d8b P*8c",

            // △59飛車が詰めろ(19手詰み)
            //"startpos moves 7g7f 8c8d 5g5f 8d8e 8h7g 7a6b 5f5e 5a4b 2h5h 7c7d 5i4h 6b7c 4h3h 7c6d 7i7h 6a5b 6g6f 7d7e 7f7e 8b8d 3h2h 4b3b 3i3h 6d7e 7g6h P*7f 6h4f 6c6d 5e5d 3a4b 5d5c+ 4b5c P*7b 8e8f 8g8f P*8h 7b7a+ 8h8i+ 7a8a 8d8a P*5d 5c4b P*7c 8a8c 7c7b+ 8i8h 7b7c 8c7c N*6e 8h7h 6e7c+ 7h6i R*7b G*5i 5d5c+ 5i5h 5c5b 5h4i 5b4b 4a4b 3h4i R*8h G*3h G*6a 7b4b+ 3b4b 4f3e P*5b G*5d N*4a 7c6c 2b3a S*5c 5b5c 6c5c 4a5c 3e5c+ 4b4a 5c4c",

            // ▲23銀で21手詰み
            //"sfen lr6+L/2P3gk1/4g4/4pppp1/p8/1Pnp+s1P2/P5SP1/LS7/K7R b BGS2NL6Pbgnp 1",
        };

        // ここに探索時の持ち時間など　探索深さでもいい
        string str_go =
            " depth 20";
        //" btime 0 wtime 0 byoyomi 10000";

        for (auto& str : options)
        {
            istringstream is(str);
            USI::setOption(is);
        }

        Search::clear();
        int64_t nodes = 0;
        TimePoint elapsed = now();

        for (size_t i = 0; i < positions.size(); ++i)
        {
            istringstream is(positions[i]);
            USI::position(b, is);
            //cout << b << endl;
            is.clear(stringstream::goodbit);
            is.str(str_go);
            USI::go(b, is);
            Threads.main()->join();
            nodes += Threads.main()->root_board.nodeSearched();
            Search::clear();
        }

        elapsed = now() - elapsed + 1;

        cout << "\n==========================="
             << "\nTotal time (ms) : " << elapsed
             << "\nNodes searched  : " << nodes
             << "\nNodes/second    : " << 1000 * nodes / elapsed << endl;

        g[21] = 0;
        
        //for (int i = 0; i < 30; i++)
            //std::cout << "g[" << i << "] = " << g[i] << std::endl;
}