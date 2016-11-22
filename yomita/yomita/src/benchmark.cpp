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
            "name Hash value 1024"
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

#if 1
            //"sfen lR1B3nl/2gp5/ngk1+BspPp/1s2p2p1/p4S3/1Pp6/P5P1P/LGG6/KN5NL b Prs5p 1", // 240
            //"sfen 5S2l/1rP2s1k1/p2+B1gnp1/5np2/3G3n1/5S2p/P1+p1PpPP1/1P1PG2KP/L2+rLPGNL b Bs3p 1", // 260
            //"sfen lR6l/1s1g5/1k1s1+P2p/1+bpp1+Bs2/1n1n2Pp1/2P6/S2R4P/K1GG5/9 b 2NPg2l9p 1",
            //"sfen l4g1nl/4g1k2/2n1sp1p1/p5pPp/5Ps2/1P1p2s2/P1G1+p1N1P/6K2/LN5RL b RBG3Pbs3p 1",
            /*"sfen 1n4g1k/6r2/1+P1psg1p+L/2p1pp3/3P5/p1P1PPPP1/3SGS3/1+p1K1G2r/9 b 2BNLPs2n2l3p 1",
            "sfen +B2+R3n1/3+L2gk1/5gss1/p1p1p1ppl/5P2p/PPPnP1PP1/3+p2N2/6K2/L4S1RL b BGS3Pgnp 1",
            "sfen 3R4l/1kg6/2ns5/spppp2+Bb/p7p/1PPPP1g2/nSNSNP2P/KG1G5/5r2L b L4Pl2p 1",
            "sfen ln5nl/2r2gk2/1p2sgbpp/pRspppp2/L1p4PP/3PP1G2/N4PP2/3BS1SK1/5G1NL b 3P 1",
            "sfen ln7/1r2k1+P2/p3gs3/1b1g1p+B2/1p5R1/2pPP4/PP1S1P3/2G2G3/LN1K5 b SNL3Psnl5p 1",
            "sfen 3+P3+Rl/2+P2kg2/+B2psp1p1/4p1p1p/9/2+p1P+bnKP/P6P1/4G1S2/L4G2L b G2S2NLrn5p 1",
            "sfen ln1gb2nl/1ks4r1/1p1g4p/p1pppspB1/5p3/PPPPP1P2/1KNG1PS1P/2S4R1/L2G3NL b Pp 1",
            "sfen lr6l/4g1k1p/1s1p1pgp1/p3P1N1P/2Pl5/PPbBSP3/6PP1/4S1SK1/1+r3G1NL b N3Pgn2p 1",
            "sfen l1ks3+Bl/2g2+P3/p1npp4/1sNn2B2/5p2p/2PP5/PPN1P1+p1P/1KSSg2P1/L1G5+r b GL4Pr 1",
            "sfen ln3k1nl/2P1g4/p1lpsg1pp/4p4/1p1P1p3/2SBP4/PP1G1P1PP/1K1G3+r1/LN1s2PNR b BSPp 1",
            "sfen +N6nl/1+R2pGgk1/5Pgp1/p2p1sp2/3B1p2p/P1pP4P/6PP1/L3G1K2/7NL b RNL2Pb3s2p 1",
            "sfen ln1g5/1r4k2/p2pppn2/2ps2p2/1p7/2P6/PPSPPPPLP/2G2K1pr/LN4G1b b BG2SLPnp 1",
            "startpos",
            "startpos moves 7g7f 8c8d 8h7g 3c3d 6i7h 2b7g+ 7h7g 3a3b 2g2f 7a6b B*2b B*4d 2b4d+ 4c4d 5i4h 6a5b B*2b B*3c 2b3c+ 3b3c 9g9f 5c5d 4g4f 5a4b 6g6f 4b3b 3i3h 7c7d 4h5h 8a7c",
            "sfen ln3g1nl/7k1/prp+S1pPp1/1s1p2S1p/5B3/3PGPg1P/P5+n2/2G2S3/LR3K2L w 3Pbn4p 1",
            "startpos moves 2g2f 1c1d 7g7f 4a3b 7i7h 3c3d 8h2b+ 3a2b 7h7g 8c8d 6i7h 6c6d B*6c 7a6b 6c4e+ 2b3c 6g6f 5a4b 4e5e 6b6c 5e4f 9c9d 5g5f 4c4d 4f5g 6a5b 9g9f 4b3a 4g4f 7c7d 5g4g 3a2b 4i5h 5b4c 4g3f 6c5d 5h4g 8a7c 5f5e 5d5e 3f6c 7c8e 7g6h 9d9e 9f9e P*9h 9i9h 7d7e 8g8f 7e7f 8f8e 8d8e 6c7c 8b8a P*8c 8a7a 7c8d 4d4e 8c8b+ 4e4f 4g4f 5e4f 8b7a B*5f 6h6g 5f4e R*7e 4e5d 9h9g 8e8f N*6i G*2g 2h5h 4f4g+ 5h6h P*4f 7e7c+ P*9f 9g9f 2g3g 2i3g 4g3g P*4h 8f8g+ 7h8g 4f4g+ 4h4g P*4f P*3h 3g4g 3i2h 3c4d P*8b N*1e 8b8a+ 1e2g+ 2h3i P*8f 8g8f 2g1h 1i1h 5d1h+ P*4h 1h1g G*4i L*5d P*5h 1g2f 8d8c 6d6e 8c6e 9a9c 7c9c 4c3c P*8c 1d1e P*2g 2f3f 6e7f 2c2d 9c9b 3f2g 4h4g 4f4g+ P*4h 4g4f S*4a 4d3e 4a3b+ 3c3b G*4c S*2c 4c3b 2c3b L*4c G*3c 4c4b+ 3b2c 4b3a 2c3b 3a3b 3c3b S*4c P*4b 4c4b+ 3b4b 9b4b 2b2c 4b5c 2c1d 7f5d 2g5d 5c5d S*2c G*2b P*4g B*4a B*3b 2b3b 4g4h+ 3i4h P*4g 4h3i L*4h 3b2b 4h4i+ 5i4i G*4h 3i4h 4g4h+ 4i4h P*4g 4h3i 4g4h+",
            "sfen lns1k1b1l/1r5s1/pp3p1pp/n1p1N4/3PP4/2PS2P2/PPN1G2PP/7K1/L3RG2L w BG2Pgs2p 1",
            "startpos moves 9g9f 7a6b 2g2f 5a4b 6i7h 4b3b 9f9e 3a4b 4i4h 7c7d 4g4f 6a5b 7g7f 7d7e 7f7e 6b7c 2f2e 7c6d 7e7d 1c1d",
            "startpos moves 2g2f 3c3d 4i3h 8b4b 9g9f 5a6b 3h2g 6b7b 5g5f 7b8b 2g3f 3a3b 3f4f 4c4d 4f3f 9a9b 3i4h 8b9a 5i5h 7a8b 2f2e 2b3c 8h9g 4a5b 9g8h 6a7a 4g4f 3b4c 4h5g 6c6d 5g6f 5b6c 1g1f 7c7d 1i1h 4c5d 2h4h 1c1d 5h6h 6d6e 6f5g 7d7e 6h5i 6c6d 6i7h 5d6c 7h6h 5c5d 3f2f 5d5e 5f5e 6d5e P*5f 5e5d 2f3f 6c7d 6h5h 1a1b 7i7h P*5e 4h2h 4b5b 5i6h 5e5f 5g5f P*5e 5f4g 5b6b 8h7i 6e6f 6g6f 6b6f 6h5i 6f6b P*6g 7d6e 7i9g 5d6d 5i6h 5e5f 5h4h 6d7d 9g8h P*6f 6g6f 6e6f P*6g 6f5e P*5g 5f5g+ 4h5g P*5f 4g5f 5e5f 5g5f S*4g 5f5g 4g3f 3g3f 4d4e 4f4e 7e7f S*4d 3c5a 7g7f 7d6e 4d5c+ 6b6d 8h1a+ P*5f 5g4g 5a3c 1a2a 3c9i+ 8i7g 6e5e 2a1b 9i8h L*6e G*5g 4g5g 5f5g+ 6h5g G*4f 5g6h 5e6e S*7i 4f5g 6h6i 8h7i 6i7i 6e7f B*5e 6d7d G*6d 7d7e 6d6e 7f7g 6e7e S*8h 7i6i 7g7h 2h7h S*7g 5e7g 8h7g+ 7h7g L*7b P*7c 7b7c P*7d 7c7d 7e7d N*6e 7g7e P*6f 6i7i B*9g 7i8i 9g7e+ 7d7e 6e7g+ R*2h 6f6g+ G*6i R*4i L*5i 5g6h S*9i 6h6i 8i9h 4i5i+ P*7b 7a7b",
            "startpos moves 7g7f 3c3d 5i6h 6c6d 6g6f 6d6e 6f6e 8b6b 6e6d 5a4b 6d6c+ 4b3b 6c6b 2b8h+ 7i8h 7a7b 6b7b 3b3c 7b6a 3c4b 6a5a 4b5a R*8b 4a5b B*6c B*6a",
            "startpos moves 7g7f 3c3d 5i6h 6c6d 6g6f 6d6e 6f6e 8b6b 6e6d 5a4b 6d6c+ 4b3b 6c6b 2b8h+ 7i8h 7a7b 6b7b 3b3c 7b6a 3c4b 6a5a 4b5a R*8b 4a5b S*6c P*6b 8b8a+ B*6a 6c7b+ 5a4b 8a6a 1c1d 7b6b 9c9d 6a5b",
            "startpos moves 9g9f 7a6b 5i6h 5a4b 3g3f 4b3b 5g5f 3a4b 7i7h 5c5d 2g2f 1c1d 6h5h 3c3d 2f2e 5d5e 4g4f 5e5f 5h4g 8c8d 2h5h 2a1c 5h5f P*5e 5f7f 1c2e 4g3h 6a5b 3h2g 5e5f 7f5f 7c7d P*2f P*5e 5f7f 7d7e 7f7e 5e5f 8h7i 2e1g+ 2g1g 6b7c 7e7f P*7e 7f7e 2b4d 7e7f 7c6d 7f5f 6d6e 5f5h 4d2b 4f4e 5b5c 3f3e 5c5d 3e3d 2b5e 3i2h 8d8e P*1b 1a1b P*1c 8e8f 1c1b+ P*5g 5h5g 2c2d 5g5e 5d5e B*1a 4a5a 1a5e+ 8f8g+ 5e8b R*4g L*3g 3b4a 8b8a 4g3g+ 2h3g 8g7h 8a6c 5a5b N*3c 4b3c R*2a N*3a N*5c 4a4b 3d3c+ 4b3c 7i2d",
            "startpos moves 9g9f 7a6b 7i7h 6a5b 9f9e 5a4b 2g2f 3c3d 6i5h 4b3b 2f2e 2b3c 4g4f 8c8d 9i9f 8d8e 8h9g 5c5d 9g7e 7c7d 7e9g 3a2b 5i6h 6b5c 1g1f 6c6d 3g3f 4c4d 2i3g 3c4b 6g6f 8a7c 2h4h 5d5e 7h6g 5c5d 6g7f 2b3c 6h7h 5b4c 3i2h 3b2b 2h2g 4a3b 4i5i 8b8d 4h2h 9c9d 9e9d 9a9d P*9e 9d9e 9f9e P*9f 9g7i 6d6e 6f6e P*6f 9e9c+ 7d7e 7f7e 4b7e L*7f 8e8f 8g8f 7e8f P*8g 8f4b 7f7c+ 8d8e 2e2d 3c2d N*8f 8e9e 9c9d 9e6e P*6h 2d3c P*9h 3c2d 9d8c P*8e 8f7d 8e8f 8g8f 4b8f P*8g 8f6d 7d8b+ 5e5f 5i4h P*8h 7i8h P*8f 8g8f S*3i 2h3h 3i4h+ 3h4h 5f5g+ 5h5g 6d7e 4h1h 7e8f P*8g 8f9e S*8f 9e8f 8g8f P*8e 7g7f 8e8f 8h6f S*5e 6f9c+ P*5f 5g6g G*8g 7h6i P*6f 8i7g 6e6d 7c7d 6d7d 6g6f 8g7g 6f7e 7d7e 9c7e L*5g P*5h N*6f P*2e P*6g 6i5i 6f5h+ 1h5h 5g5h+ 5i5h R*2h B*3h 2d3c 5h4i 5e4f L*2i 5f5g+ 4i3i 2h2g+ 3h2g 4f3g+ 7e5g N*4g 5g4g 3g4g R*4i S*5h 4i8i G*3g 2i2h",
            "startpos moves 9g9f 7a6b 1g1f 5c5d 9f9e 5a4b 6g6f 3c3d 7g7f 4b3b 4i5h 7c7d 5h6g 2b3c 6g5f 4c4d 3g3f 3b2b 6f6e 6b5c 2i3g 3a3b 9i9f 6a5b 5i6h 5b4c 4g4f 6c6d 6e6d 5c6d 6h7h 7d7e 2h6h P*6e 7f7e 8b7b 5f6e 6d6e 6h6e G*2i S*2h 3c2d 6e6a+ 7b7e 7h6g P*6e 6a8a 2d4f N*5h 4f7c P*4f 2i3i 2h3i S*7f 6g7h 7e8e G*9g 7c6d 9g9h 8e8d P*6f 6e6f 8h6f P*7e 3i4h P*6e 6f9i 2a3c 9e9d 9c9d P*9b 9a9b 8a9b 7f8e L*8f 8e8f 8g8f 8d8f 9b9d 8f8d 9d9c L*2d P*8e 8d8e 8i9g 8e8f P*8g 8f7f S*7g 7f9f 9c9f L*9a 9f5f 7e7f 7g8f P*9f 9g8e 9f9g+ 5f6e 9g9h 9i6f 6d5c P*6d G*4i 6i5i 4i5i 4h5i G*5e 6f5e 5d5e 6d6c+ 5c8f 8g8f B*8i 7h6i 7f7g+ G*6h 7g6h 5i6h 2d2g+ 7i7h S*5d 6e7d 2g3g 7h8i 9h8i 6c6d 8i8h P*7i G*8d 7d7g 9a9h+ 6d5d 4c5d 7g7a P*6g 6h6g N*7e R*6b 7e6g+",
            "startpos moves 9g9f 8c8d 2g2f 8d8e 8h9g 5a4b 2f2e 3a3b 2e2d 2c2d 2h2d P*2c 2d2f 7a6b 1g1f 9c9d 9g8h 3c3d 2f7f 3b3c 5i6h 7c7d 6h7h 6b7c 4i3h 7c6d",
            "startpos moves 9g9f 8c8d 2g2f 8d8e 8h9g 5a4b 2f2e 3a3b 2e2d 2c2d 2h2d P*2c 2d2f 7a6b 1g1f 3c3d",
            "startpos moves 9g9f 8c8d 2g2f 8d8e 8h9g 5a4b 2f2e 3a3b 2e2d 2c2d 2h2d P*2c 2d2f 7a6b",
            "startpos moves 9g9f 8c8d 2g2f 8d8e 8h9g 5a4b 2f2e 3a3b 2e2d 2c2d"
            "startpos moves 9g9f 8c8d 9f9e 8d8e 2g2f 8e8f 8g8f 8b8f 6i7h 8f8b P*8g 3c3d 2f2e 2b3c 7g7f 3a2b 8h3c+ 2b3c 8i7g 7a6b 7i8h 4a3b 6g6f 5a4b 6f6e 4b3a 7h6g 5c5d 7f7e 6b5c 5g5f 5c4d B*9f 6a5b 9f8e 3d3e 8g8f 8b8d 8h8g 5d5e 5f5e 4d5e P*5f 5e4d 8e7f 4d4e 6g5g 8d5d 7f6g B*3d 2h2f 3e3f 3i4h 4e5f 6g5f 3d5f 5g5f 5d5f S*5g B*3e",*/
#endif
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
            << "\nAttention Node  : " << g[21]
            << "\nNodes/second    : " << 1000 * nodes / elapsed << endl;

        g[21] = 0;
        
        //for (int i = 0; i < 30; i++)
            //std::cout << "g[" << i << "] = " << g[i] << std::endl;
}