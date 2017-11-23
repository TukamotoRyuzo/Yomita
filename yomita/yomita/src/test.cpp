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

#include <fstream>

#include "usi.h"
#include "book.h"
#include "board.h"
#include "sfen_rw.h"
#include "byteboard.h"

extern void byteboard_test(const Board& b);

// 引数で与えられるbはランダムな配置なのでさまざまなテストができる。
void testOnRandomPosition(Board& b)
{
    //b.init("ln5nl/r3g1s2/psppkp1pp/6p2/1pP1SG1P1/1P1+l1P2P/P2g2r1N/LB5+b1/1N1G1K3 w P3ps 63", Threads.main());
#if defined USE_BYTEBOARD && defined USE_BITBOARD
    byteboard_test(b);
#endif
}

void randomPlayer(Board& b, uint64_t loop_max)
{
    USI::isready();
    b.init(USI::START_POS, Threads.main());
    //b.init("1n1+L5/l1GP1P3/kr3p1g+L/1p2+B2pg/7P1/1P2P4/4+n1pG1/+p1P1S+l2S/1rSK1s3 w B4P3p2n 207", Threads.main());
    const int MAX_PLY = 256;
    StateInfo state[MAX_PLY];
    int ply = 0;
    int count1 = 0, count2 = 0;
    Move moves[MAX_PLY];
    PRNG rng(20160817);

    uint64_t  k = 0;
    for (uint64_t  i = 0; i < loop_max; i++)
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
                m = ml.begin()[rng.rand<int>() % ml.size()];
            } while (cc++ < 3 && isDrop(m));
#if 0
            if (b.seeGe(m, (Score)0) != b.seeGe2(m, SCORE_ZERO))
            {
                count1++;
                std::cout << b << pretty(m) << b.seeGe(m, SCORE_ZERO) << b.seeGe2(m, SCORE_ZERO) << std::endl;
                std::cout << b.seeGe(m, Score(0));
                std::cout << b.seeGe2(m, Score(0));
            }
            else
            {
                count2++;
            }
#endif
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

#if 0
    std::cout << "ok = " << count2 << "bad = " << count1
        << "true rate = " << (double)count2 / (double)(count1 + count2) * 100.0 << "%" << std::endl;
#endif
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

            if (b.inCheck())
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
Depth   Nodes        Captures   Promotions Checks     Checkmates
1       30           0          0          0          0
2       900          0          0          0          0
3       25470        59         30         48         0
4       719731       1803       842        1121       0
5       19861490     113680     57214      71434      0
6       547581517    3387051    1588324    1730177    0
7       15086269607  156289904  78496954   79636812   29
8       416062133009 4713670699	2222896064 2047229309 3420
*/

/* MAX_MOVES_POS
Depth   Nodes    Captures   Promotions  Checks   Checkmates
1       593      0          52          40       6
2       105677   538        0           3802     0
3       53393368 197899     4875102     3493971  566203
*/

void perft(Board& b, int depth)
{
    std::cout << "perft depth = " << depth << b << std::endl;
    PerftSolver solver;

    auto result = solver.perft(b, MOVE_NONE, depth);

    std::cout << "\nnodes = "      << result.nodes 
              << "\ncaptures = "   << result.captures 
              << "\npromotions = " << result.promotions  
              << "\nchecks = "     << result.checks 
              << "\ncheckmates = " << result.mates
              << std::endl;
}

void userTest()
{
#if 1 // ランダムプレイヤーテスト

    USI::isready();

    uint64_t loop_max = 100000;
    std::cout << "Random Player test , loop_max = " << loop_max << std::endl;
    Board b;

    randomPlayer(b, loop_max);
    std::cout << "finished." << std::endl;

#elif defined LEARN
    Move m; 
    do {
        int i;
        std::cin >> i;
        m = (Move)i;
        std::cout << pretty(m) << std::endl;
    } while (m != MOVE_NONE);
#else
    Hand n, p;
    std::cout << "hand test" << std::endl;
    int64_t count = 0;
    for (int pawn = 0; pawn <= 18; pawn++)
        for (int lance = 0; lance <= 4; lance++)
            for (int knight = 0; knight <= 4; knight++)
                for (int silver = 0; silver <= 4; silver++)
                    for (int gold = 0; gold <= 4; gold++)
                        for (int bishop = 0; bishop <= 2; bishop++)
                            for (int rook = 0; rook <= 2; rook++)
                            {
                                n.set(PAWN, pawn);
                                n.set(LANCE, lance);
                                n.set(KNIGHT, knight);
                                n.set(SILVER, silver);
                                n.set(GOLD, gold);
                                n.set(BISHOP, bishop);
                                n.set(ROOK, rook);

                                for (int dpawn = 0; dpawn <= 18 - pawn; dpawn++)
                                    for (int dlance = 0; dlance <= 4 - lance; dlance++)
                                        for (int dknight = 0; dknight <= 4 - knight; dknight++)
                                            for (int dsilver = 0; dsilver <= 4 - silver; dsilver++)
                                                for (int dgold = 0; dgold <= 4 - gold; dgold++)
                                                    for (int dbishop = 0; dbishop <= 2 - bishop; dbishop++)
                                                        for (int drook = 0; drook <= 2 - rook; drook++)
                                                        {
                                                            p.set(PAWN, dpawn);
                                                            p.set(LANCE, dlance);
                                                            p.set(KNIGHT, dknight);
                                                            p.set(SILVER, dsilver);
                                                            p.set(GOLD, dgold);
                                                            p.set(BISHOP, dbishop);
                                                            p.set(ROOK, drook);

                                                            bool superi1 = n.isSuperior(p);
                                                            bool superi2 = n.isSuperior2(p);
                                                            bool superi3 = p.isSuperior(n);
                                                            bool superi4 = p.isSuperior2(n);

                                                            if (superi1 != superi2
                                                                || superi3 != superi4)
                                                            {
                                                                std::cout << "n = " << n << std::endl
                                                                    << "p = " << p << std::endl
                                                                    << "superi1 = " << superi1 << std::endl;
                                                            }
                                                            count++;
                                                        }

                            }

    std::cout << count << std::endl;
#endif
}
