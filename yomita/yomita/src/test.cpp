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

#include <fstream>
#include "test.h"
#include "board.h"
#include "usi.h"
#include "genmove.h"
#include "sfen_rw.h"

// 引数で与えられるbはランダムな配置なのでさまざまなテストができる。
void testOnRandomPosition(Board& b)
{
    if (!b.inCheck())
    {
        MoveStack mlist[MAX_MOVES];
        auto p = generate<NO_CAPTURE_MINUS_PROMOTE>(mlist, b);
        p = generate<DROP>(p, b);

        MoveStack clist[MAX_MOVES];
        auto c = generate<SPEED_CHECK>(clist, b);

        MoveStack* curr = mlist;

        while (curr != p)
        {
            if (!b.givesCheck(curr->move) || isCaptureOrPawnPromote(curr->move))
                *curr = *(--p);
            else
                ++curr;
        }

        curr = mlist;
        auto legal_checks = p - curr;
        auto speed_checks = c - clist;

        if (legal_checks != speed_checks)
        {
            std::cout << b << "legals :";

            while (curr != p)
                std::cout << pretty((curr++)->move) << " ";

            curr = clist;

            std::cout << "\nspeeds: ";

            while (curr != c)
                std::cout << pretty((curr++)->move) << " ";

            std::cout << std::endl;
        }
    }
}

void randomPlayer(Board& b, uint64_t loop_max)
{
    USI::isReady();
    b.init(USI::START_POS, Threads.main());
    const int MAX_PLY = 256;
    StateInfo state[MAX_PLY];
    int ply = 0;
    int count1 = 0, count2 = 0;
    Move moves[MAX_PLY];

    PRNG rng(20160817);

    int k = 0;
    for (int i = 0; i < loop_max; i++)
    {
        for (ply = 0; ply < MAX_PLY; ply++)
        {
            MoveList<LEGAL_ALL> ml(b);
            
            if (ml.size() == 0)
                break;
    
            Move m;
            int cc = 0;

            // dropは選ばれすぎるので少し確率を下げる。
            do {
                m = ml.begin()[rng.rand<int>() % ml.size()].move;
            } while (cc++ < 3 && isDrop(m));

            if (b.seeGe(m, Score(1)) != (b.see(m) >= 1))
            {
                std::cout << b << pretty(m) << "see = " << b.see(m) << std::endl;

                b.seeGe(m, Score(1));
            }

            b.doMove(m, state[ply], b.givesCheck(m));
            moves[ply] = m;

            // ランダムな局面でテストをする。
            testOnRandomPosition(b);
        }

        while (ply > 0)
            b.undoMove(moves[--ply]);

        if ((i % 1000) == 0)
            std::cout << ".";
    }
}

struct PerftSolverResult 
{
    uint64_t nodes, captures, promotions, checks, mates;

    void operator += (const PerftSolverResult& other) 
    {
        nodes += other.nodes;
        captures += other.captures;
        promotions += other.promotions;
        checks += other.checks;
        mates += other.mates;
    }
};

struct PerftSolver 
{
    PerftSolverResult perft(Board& b, const Move m, int depth) 
    {
        PerftSolverResult result = {};

        if (depth == 0)
        {
            assert(b.verify());				

            result.nodes++;

            if (isCapture(m))
                result.captures++;

            if (isPromote(m))
                result.promotions++;

            if (b.bbCheckers())
            {
                result.checks++;

                if (b.isMate())
                    result.mates++;
            }
        }
        else
        {
            StateInfo st;
            MoveStack legal_moves[MAX_MOVES];
            MoveStack* pms = &legal_moves[0];

            for (auto m : MoveList<LEGAL_ALL>(b))
            {
                b.doMove(m, st, b.givesCheck(m));
                result += perft(b, m, depth - 1);
                b.undoMove(m);

                static uint64_t i = 0;
                
                if (i++ % 1000000 == 0)
                    std::cout << ".";
            }
        }

        return result;
    }
};

/* START_POS
Depth	Nodes		 Captures	Promotions Checks	  Checkmates
1	    30	         0	        0		   0	      0
2	    900	         0	        0	       0		  0
3		25470		 59	        30	       48		  0
4		719731		 1803	    842	       1121		  0
5		19861490	 113680	    57214	   71434	  0
6		547581517	 3387051	1588324	   1730177	  0
7		15086269607	 156289904	78496954   79636812	  29
8		416062133009 4713670699	2222896064 2047229309 3420
*/

/* MAX_MOVES_POS
Depth	Nodes	 Captures	Promotions	Checks	 Checkmates
1	    593	     0          52          40	     6
2	    105677	 538        0	        3802     0
3     	53393368 197899	    4875102	    3493971  566203
*/

void perft(Board& b, int depth)
{
    std::cout << "perft depth = " << depth << b << std::endl;
    PerftSolver solver;

    auto result = solver.perft(b, MOVE_NONE, depth);

    std::cout << "\nnodes = "	   << result.nodes 
              << "\ncaptures = "   << result.captures 
              << "\npromotions = " << result.promotions  
              << "\nchecks = "	   << result.checks 
              << "\ncheckmates = " << result.mates
              << std::endl;
}

void userTest()
{
#if 1 // ランダムプレイヤーテスト
    USI::isReady();
    uint64_t loop_max = 1000000;
    std::cout << "Random Player test , loop_max = " << loop_max << std::endl;
    Board b;
    randomPlayer(b, loop_max);
    std::cout << "finished." << std::endl;

#elif defined LEARN || defined GENSFEN

    USI::isReady();
    
    Learn::PackedSfenValue p, p2;
    Board b(Threads.main());

    p.data[0] = 130;
    p.data[1] = 142; 
    p.data[2] = 113; 
    p.data[3] = 5; 
    p.data[4] = 70; 
    p.data[5] = 128; 
    p.data[6] = 76; 
    p.data[7] = 248;
    p.data[8] = 145; 
    p.data[9] = 240; 
    p.data[10] = 10; 
    p.data[11] = 192; 
    p.data[12] = 42; 
    p.data[13] = 35; 
    p.data[14] = 196; 
    p.data[15] = 17; 
    p.data[16] = 32; 
    p.data[17] = 60; 
    p.data[18] = 7; 
    p.data[19] = 6;
    p.data[20] = 30; 
    p.data[21] = 126; 
    p.data[22] = 25; 
    p.data[23] = 22; 
    p.data[24] = 224;
    p.data[25] = 125; 
    p.data[26] = 0; 
    p.data[27] = 120;
    p.data[28] = 146;
    p.data[29] = 100;
    p.data[30] = 157;
    p.data[31] = 19; 

    b.setFromPackedSfen(p.data);
    b.setThread(Threads.main());
    b.sfenPack(p2.data);

    for (int i = 0; i < 32; i++)
    {
        if (p.data[i] != p2.data[i])
            std::cout << "#";
    }
    std::cout << b;
    auto r = Learn::qsearch(b, -SCORE_INFINITE, SCORE_INFINITE);
    auto shallow_value = r.first;

    std::cout << r.first;
#else
    // 011 1111 1001 1111 1100 1111 1110 0111 1111 0011 1111 1001 1111 1100 1111 1110
    // 

    std::ofstream of("E:\\デスクトップ\\bit.txt");

    for (auto sq : Squares)
    {
        of << "0x" << std::hex << ((bishopAttackToEdge(sq) & ~mask(sq)).merge() & 0x3f9fcfe7f3f9fcfeULL) << "ULL, ";

        if ((sq + 1) % 9 == 0)of << std::endl;
    }
#endif
}