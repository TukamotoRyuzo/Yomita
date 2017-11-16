﻿/*
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

#include "thread.h"
#include "movepick.h"
#include <string>
namespace
{
    enum Stages
    {
        MAIN_SEARCH, CAPTURES_INIT, GOOD_CAPTURES, KILLERS, COUNTERMOVE,
        QUIET_INIT, QUIET, BAD_CAPTURES,
        EVASION, EVASIONS_INIT, ALL_EVASIONS,
        PROBCUT, PROBCUT_INIT, PROBCUT_CAPTURES,
        QSEARCH_WITH_CHECKS, QCAPTURES_1_INIT, QCAPTURES_1, QCHECKS,
        QSEARCH_NO_CHECKS, QCAPTURES_2_INIT, QCAPTURES_2,
        QSEARCH_RECAPTURES, QRECAPTURES
    };

    // An insertion sort, which sorts moves in descending order up to and including a given limit.
    // The order of moves smaller than the limit is left unspecified.
    // To keep the implementation simple, *begin is always included in the list of sorted moves.
    void partial_insertion_sort(MoveStack* begin, MoveStack* end, int limit)
    {
        for (MoveStack *sortedEnd = begin, *p = begin + 1; p < end; ++p)
            if (p->score >= limit)
            {
                MoveStack tmp = *p, *q;
                *p = *++sortedEnd;
                for (q = sortedEnd; q != begin && *(q - 1) < tmp; --q)
                    *q = *(q - 1);
                *q = tmp;
            }
    }

    // pick_best() finds the best move in the range (begin, end) and moves it to
    // the front. It's faster than sorting all the moves in advance when there
    // are few moves, e.g., the possible captures.
    Move pickBest(MoveStack* begin, MoveStack* end)
    {
        std::swap(*begin, *std::max_element(begin, end));
        return *begin;
    }

} // namespace

  // 通常探索から呼び出されるとき用。
MovePicker::MovePicker(const Board& b, Move ttm, Depth d, Search::Stack* s)
    : board(b), ss(s), depth(d)
{
    assert(d > DEPTH_ZERO);

    const Move pm = (ss - 1)->current_move;
    counter_move = isOK(pm) ? b.thisThread()->counter_moves.value(pm) : MOVE_NONE;

    // 王手がかかっているなら回避手(EVASIONS)
    stage = board.inCheck() ? EVASION : MAIN_SEARCH;
    tt_move = ttm && b.pseudoLegal(ttm) ? ttm : MOVE_NONE;
    stage += (tt_move == MOVE_NONE);
}

// 静止探索から呼び出される時用。
MovePicker::MovePicker(const Board& b, Move ttm, Depth d, Move prev)
    : board(b)
{
    assert(d <= DEPTH_ZERO);

    if (board.inCheck())
        stage = EVASION;

    else if (d > DEPTH_QS_NO_CHECKS)
        stage = QSEARCH_WITH_CHECKS;

    else if (d > DEPTH_QS_RECAPTURES)
        stage = QSEARCH_NO_CHECKS;

    else
    {
        // qsearchのdepthがDEPTH_QS_RECAPTURESより深くなってくるとここにくる。
        // なので前回の指し手はqsearch内で生成されたものであり、MOVE_NULLはありえない。
        assert(prev != MOVE_NULL);
        stage = QSEARCH_RECAPTURES;
        recapture_square = toSq(prev);
        return;
    }

    tt_move = ttm && b.pseudoLegal(ttm) ? ttm : MOVE_NONE;
    stage += (tt_move == MOVE_NONE);
}

MovePicker::MovePicker(const Board& b, Move ttm, Score th)
    : board(b), threshold(th)
{
    assert(!b.inCheck());

    stage = PROBCUT;

    // ProbCutフェーズではSEEがthresholdより高い手を生成する。
    tt_move = ttm
        && b.pseudoLegal(ttm)
        && isCaptureOrPawnPromote(ttm)
        && b.seeGe(ttm, threshold) ? ttm : MOVE_NONE;

    stage += (tt_move == MOVE_NONE);
}

const Score LVATable[PIECETYPE_MAX] =
{
    Score(0), Score(7), Score(8), Score(1), Score(2), Score(3), Score(4), Score(5), Score(256),
    Score(9), Score(10), Score(5), Score(5), Score(5), Score(5)
};

#ifdef USE_BYTEBOARD

void scoreCaptureMove(MoveStack& m, Piece p, Piece c)
{
    m.score = pieceScore(typeOf(c)) - LVATable[typeOf(p)];

    assert(isOK(m));
    assert(typeOf(c) != KING);

    if (isPawnPromote(m.move))
        m.score += PROMOTE_PAWN_SCORE;

    // 移動先が前になるほど高評価
    const Rank to_rank = relativeRank(rankOf(toSq(m.move)), turnOf(m));
    m.score += inverse(to_rank);
}

// 指し手をオーダリングするために指し手で取った駒に応じて点数をつける。
void MovePicker::initReCaptures(Square rsq)
{
    end_moves = end_bads = cur = moves;

    Move tmp[MAX_MOVES];
    Move* end = generateRecapture(board, tmp, rsq);

    for (Move* mp = tmp; mp < end; mp++)
    {
        end_moves->move = *mp;
        scoreCaptureMove(*end_moves, movedPiece(*mp), capturePiece(*mp));
        end_moves++;
    }

    assert(inRange(cur));
    assert(inRange(end_moves));
    assert(inRange(end_bads));
}

void MovePicker::scoreCaptures()
{
    end_moves = end_bads = cur = moves;

    // まずは指し手を生成する。
    __m256i tmp[39];
    Move16* end = (Move16*)generateOnBoard<CAPTURE_PLUS_PAWN_PROMOTE>(board, tmp);

    for (Move16* mp = (Move16*)tmp; mp < end; mp++)
    {
        Move tm = ((Move)*mp);

        if (tm != MOVE_NONE)
        {
            Piece c = board.piece(toSq(tm));
            Piece p = board.piece(fromSq(tm));
            Move m = move16ToMove(tm, p, c);
            assert(c);
            end_moves->move = m;
            scoreCaptureMove(*end_moves, p, c);
            end_moves++;
        }
    }

    assert(inRange(cur));
    assert(inRange(end_moves));
    assert(inRange(end_bads));
}

// 駒取りでも成りでもない手に点数をつける
void MovePicker::scoreQuiets()
{
    end_moves = cur = end_bads;

    const CounterMoveStats* cm = (ss - 1)->counter_moves;
    const CounterMoveStats* fm = (ss - 2)->counter_moves;
    const CounterMoveStats* f2 = (ss - 4)->counter_moves;
    const HistoryStats& history = board.thisThread()->history;
    Turn t = board.turn();

    // まずは指し手を生成する。
    __m256i tmp[39];
    Move16* end = (Move16*)generateOnBoard<NO_CAPTURE_MINUS_PAWN_PROMOTE>(board, tmp);

    for (Move16* mp = (Move16*)tmp; mp < end; mp++)
    {
        Move tm = ((Move)*mp);

        if (tm != MOVE_NONE)
        {
            Piece p = board.piece(fromSq(tm));
            Move m = move16ToMove(tm, p, EMPTY);
            end_moves->move = m;
            end_moves->score = cm->value(m) + fm->value(m) + f2->value(m) + history.get(t, m);
            end_moves++;
        }
    }

    Move tmp2[MAX_MOVES];
    Move* end2 = generateDrop(board, tmp2);

    for (Move* mp = tmp2; mp < end2; mp++)
    {
        Move m = *mp;
        end_moves->move = m;
        end_moves->score = cm->value(m) + fm->value(m) + f2->value(m) + history.get(t, m);
        end_moves++;
    }

    assert(inRange(cur));
    assert(inRange(end_moves));
    assert(inRange(end_bads));
}

void MovePicker::scoreEvasions()
{
    end_moves = cur = moves;

    Move tmp[MAX_MOVES];
    Move* end = generateEvasion(board, tmp);
    size_t size = end - tmp;
    const HistoryStats& history = board.thisThread()->history;
    Turn t = board.turn();

    for (Move* mp = tmp; mp < end; mp++)
    {
        MoveStack& m = *end_moves;
        m.move = *mp;

        assert(isOK(m));

        if (isCapture(m))
        {
            m.score = captureScore(capturePieceType(m)) - LVATable[movedPieceType(m)] + HistoryStats::Max;

            if (isPawnPromote(m))
                m.score += promoteScore(movedPieceType(m));
        }

        else
            m.score = history.get(t, m) - LVATable[movedPieceType(m)];

        end_moves++;
    }

    assert(inRange(cur));
    assert(inRange(end_moves));
}

void MovePicker::initQuietChecks()
{
    cur = moves;
    end_moves = generateQuietCheck(board, cur);
    assert(inRange(cur));
    assert(inRange(end_moves));
}

// 次の指し手をひとつ返す
// 指し手が尽きればMOVE_NONEが返る。
Move MovePicker::nextMove()
{
    Move move;

    switch (stage)
    {
    case MAIN_SEARCH: case EVASION: case QSEARCH_WITH_CHECKS:
    case QSEARCH_NO_CHECKS: case PROBCUT:
        ++stage;
        return tt_move;

    case CAPTURES_INIT:
        scoreCaptures();
        ++stage;

    case GOOD_CAPTURES:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);

            if (move != tt_move)
            {
                if (board.seeGe(move, SCORE_ZERO))
                    return move;

                *end_bads++ = move;
            }
        }

        ++stage;
        move = ss->killers[0];

        if (move != MOVE_NONE
            && move != tt_move
            && !board.piece(toSq(move))
            && !isPawnPromote(move)
            && board.pseudoLegal(move))
        {
            assert(!isCapture(move));
            return move;
        }

    case KILLERS: // KILLERS faseでは駒を取る手は試す必要はない。この前のフェーズで駒を取る手はすべて試したから。
        ++stage;
        move = ss->killers[1];

        if (move != MOVE_NONE
            && move != tt_move
            && !board.piece(toSq(move))
            && !isPawnPromote(move)
            && board.pseudoLegal(move))
        {
            assert(!isCapture(move));
            return move;
        }

    case COUNTERMOVE:
        ++stage;
        move = counter_move;
        if (move != MOVE_NONE
            && move != tt_move
            && move != ss->killers[0]
            && move != ss->killers[1]
            && !board.piece(toSq(move))
            && !isPawnPromote(move)
            && board.pseudoLegal(move))
        {
            assert(!isCapture(move));
            return move;
        }

    case QUIET_INIT:
        scoreQuiets();
        partial_insertion_sort(cur, end_moves, -4000 * (depth / ONE_PLY));
        ++stage;

    case QUIET:
        while (cur < end_moves)
        {
            move = *cur++;

            if (move != tt_move
                && move != ss->killers[0]
                && move != ss->killers[1]
                && move != counter_move)
                return move;
        }

        ++stage;
        cur = moves; // bad capturesの先頭を指す

    case BAD_CAPTURES:
        if (cur < end_bads)
            return *cur++;

        break;

    case EVASIONS_INIT:
        scoreEvasions();
        ++stage;

    case ALL_EVASIONS:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);

            if (move != tt_move)
                return move;
        }

        break;

    case PROBCUT_INIT:
        scoreCaptures();
        ++stage;

    case PROBCUT_CAPTURES:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);

            if (move != tt_move && board.seeGe(move, threshold + 1))
                return move;
        }

        break;

    case QCAPTURES_1_INIT: case QCAPTURES_2_INIT:
        scoreCaptures();
        ++stage;

    case QCAPTURES_1: case QCAPTURES_2:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);

            if (move != tt_move)
                return move;
        }

        if (stage == QCAPTURES_2)
            break;

        initQuietChecks();
        ++stage;

    case QCHECKS:
        while (cur < end_moves)
        {
            move = cur++->move;

            if (move != tt_move)
            {
                assert(board.givesCheck(move));
                assert(!board.inCheck());
                assert(!isCapture(move));
                return move;
            }
        }

        break;

    case QSEARCH_RECAPTURES:
        initReCaptures(recapture_square);
        ++stage;

    case QRECAPTURES:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);
            assert(toSq(move) == recapture_square);
            return move;
        }

        break;

    default:
        UNREACHABLE;
    }

    return MOVE_NONE;
}

int MovePicker::seeSign() const
{
    return stage == GOOD_CAPTURES ? 1
        : stage == BAD_CAPTURES ? -1 : 0;
}

#else
// 指し手をオーダリングするために指し手で取った駒に応じて点数をつける。
void MovePicker::scoreCaptures()
{
    for (auto& m : *this)
    {
        assert(isOK(m));
        m.score = pieceScore(capturePieceType(m)) - LVATable[movedPieceType(m)];
        assert(capturePieceType(m) != KING);

        if (isPawnPromote(m))
            m.score += PROMOTE_PAWN_SCORE;

        // 移動先が前になるほど高評価
        const Rank to_rank = relativeRank(rankOf(toSq(m)), board.turn());
        m.score += inverse(to_rank);
    }
}


// 駒取りでも成りでもない手に点数をつける
void MovePicker::scoreQuiets()
{
    const CounterMoveStats* cm = (ss - 1)->counter_moves;
    const CounterMoveStats* fm = (ss - 2)->counter_moves;
    const CounterMoveStats* f2 = (ss - 4)->counter_moves;
    const HistoryStats& history = board.thisThread()->history;
    Turn t = board.turn();

    for (auto& m : *this)
        m.score = cm->value(m) + fm->value(m) + f2->value(m) + history.get(t, m);
}

void MovePicker::scoreEvasions()
{
    const HistoryStats& history = board.thisThread()->history;
    Turn t = board.turn();

    for (auto& m : *this)
    {
        assert(isOK(m));

        if (isCapture(m))
        {
            m.score = captureScore(capturePieceType(m)) - LVATable[movedPieceType(m)] + HistoryStats::Max;

            if (isPawnPromote(m))
                m.score += promoteScore(movedPieceType(m));
        }

		else
		{
			m.score = history.get(t, m) - LVATable[movedPieceType(m)];
		}
    }
}

// 次の指し手をひとつ返す
// 指し手が尽きればMOVE_NONEが返る。
Move MovePicker::nextMove()
{
    Move move;

    switch (stage)
    {
    case MAIN_SEARCH: case EVASION: case QSEARCH_WITH_CHECKS:
    case QSEARCH_NO_CHECKS: case PROBCUT:
        ++stage;
        return tt_move;

    case CAPTURES_INIT:
        end_bads = cur = moves;
        end_moves = generate<CAPTURE_PLUS_PAWN_PROMOTE>(cur, board);
        scoreCaptures();
        ++stage;

    case GOOD_CAPTURES:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);

            if (move != tt_move)
            {
                if (board.seeGe(move, SCORE_ZERO))
                    return move;

                // 駒を損するcapturePieceは指し手バッファの一番前にやる。
                *end_bads++ = move;
            }
        }

        ++stage;
        move = ss->killers[0];

        if (move != MOVE_NONE
            && move != tt_move
            && !board.piece(toSq(move))
            && !isPawnPromote(move)
            && board.pseudoLegal(move))
        {
            assert(!isCapture(move));
            return move;
        }

    case KILLERS: // KILLERS faseでは駒を取る手は試す必要はない。この前のフェーズで駒を取る手はすべて試したから。
        ++stage;
        move = ss->killers[1];

        if (move != MOVE_NONE
            && move != tt_move
            && !board.piece(toSq(move))
            && !isPawnPromote(move)
            && board.pseudoLegal(move))
        {
            assert(!isCapture(move));
            return move;
        }

    case COUNTERMOVE:
        ++stage;
        move = counter_move;
        if (move != MOVE_NONE
            && move != tt_move
            && move != ss->killers[0]
            && move != ss->killers[1]
            && !board.piece(toSq(move))
            && !isPawnPromote(move)
            && board.pseudoLegal(move))
        {
            assert(!isCapture(move));
            return move;
        }

    case QUIET_INIT:
        cur = end_bads;
        end_moves = generate<QUIETS>(cur, board);
        scoreQuiets();
        partial_insertion_sort(cur, end_moves, -4000 * (depth / ONE_PLY));
        ++stage;

    case QUIET:
        while (cur < end_moves)
        {
            move = *cur++;

            if (move != tt_move
                && move != ss->killers[0]
                && move != ss->killers[1]
                && move != counter_move)
                return move;
        }

        ++stage;
        cur = moves; // bad capturesの先頭を指す

    case BAD_CAPTURES:
        if (cur < end_bads)
            return *cur++;

        break;

    case EVASIONS_INIT:
        cur = moves;
        end_moves = generate<EVASIONS>(cur, board);
        scoreEvasions();
        ++stage;

    case ALL_EVASIONS:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);

            if (move != tt_move)
                return move;
        }

        break;

    case PROBCUT_INIT:
        cur = moves;
        end_moves = generate<CAPTURE_PLUS_PAWN_PROMOTE>(cur, board);
        scoreCaptures();
        ++stage;

    case PROBCUT_CAPTURES:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);

            if (move != tt_move
                && board.seeGe(move, threshold + 1))
                return move;
        }

        break;

    case QCAPTURES_1_INIT: case QCAPTURES_2_INIT:
        cur = moves;
        end_moves = generate<CAPTURE_PLUS_PAWN_PROMOTE>(cur, board);
        scoreCaptures();
        ++stage;

    case QCAPTURES_1: case QCAPTURES_2:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);

            if (move != tt_move)
                return move;
        }

        if (stage == QCAPTURES_2)
            break;

        cur = moves;
        end_moves = generate<QUIET_CHECKS>(cur, board);
        ++stage;

    case QCHECKS:
        while (cur < end_moves)
        {
            move = cur++->move;

            if (move != tt_move)
            {
                assert(!board.inCheck());
                assert(!isCaptureOrPawnPromote(move));
                assert(board.givesCheck(move));
                return move;
            }
        }

        break;

    case QSEARCH_RECAPTURES:
        cur = moves;
        end_moves = generate<RECAPTURES>(cur, board, recapture_square);
        scoreCaptures();
        ++stage;

    case QRECAPTURES:
        while (cur < end_moves)
        {
            move = pickBest(cur++, end_moves);
            assert(toSq(move) == recapture_square);
            return move;
        }

        break;

    default:
        UNREACHABLE;
    }

    return MOVE_NONE;
}

int MovePicker::seeSign() const
{
    return stage == GOOD_CAPTURES ? 1
        : stage == BAD_CAPTURES ? -1 : 0;
}
#endif