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

#include "movepick.h"
#include "thread.h"

namespace
{
    // Our insertion sort, which is guaranteed to be stable, as it should be
    void insertionSort(MoveStack* begin, MoveStack* end)
    {
        MoveStack tmp, *p, *q;

        for (p = begin + 1; p < end; ++p)
        {
            tmp = *p;
            for (q = p; q != begin && *(q - 1) < tmp; --q)
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

    // 置換表の指し手が合法なら先頭に入れておく
    tt_move = ttm && b.pseudoLegal(ttm) ? ttm : MOVE_NONE;
    end_moves += !isNone(tt_move);
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
        stage = QSEARCH_WITHOUT_CHECKS;

    else
    {
        // qsearchのdepthがDEPTH_QS_RECAPTURESより深くなってくるとここにくる。
        // なので前回の指し手はqsearch内で生成されたものであり、MOVE_NULLはありえない。
        assert(prev != MOVE_NULL);
        stage = RECAPTURE;
        recapture_square = toSq(prev);
        ttm = MOVE_NONE;
    }

    tt_move = ttm && b.pseudoLegal(ttm) ? ttm : MOVE_NONE;
    end_moves += !isNone(tt_move);
}

MovePicker::MovePicker(const Board& b, Move ttm, Score th)
    : board(b), threshold(th)
{
    assert(!b.bbCheckers());

    stage = PROBCUT;

    // ProbCutフェーズではSEEがthresholdより高い手を生成する。
    tt_move = (ttm
        && b.pseudoLegal(ttm)
        && isCapture(ttm)
        && b.seeGe(ttm, threshold + 1)) ? ttm : MOVE_NONE;

    end_moves += !isNone(tt_move);
}

const Score LVATable[PIECETYPE_MAX] =
{
    Score(0), Score(7), Score(8), Score(1), Score(2), Score(3), Score(4), Score(5), Score(10000),
    Score(9), Score(10), Score(5), Score(5), Score(5), Score(5)
};

// 指し手をオーダリングするために指し手で取った駒に応じて点数をつける。
void MovePicker::scoreCaptures()
{
    for (auto& m : *this)
    {
        assert(isOK(m));
        m.score = pieceScore(capturePieceType(m));
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
    const HistoryStats& history = board.thisThread()->history;
    const CounterMoveStats* cm = (ss - 1)->counter_moves;
    const CounterMoveStats* fm = (ss - 2)->counter_moves;
    const FromToStats& from_to = board.thisThread()->from_to_history;

    Turn t = board.turn();

    for (auto& m : *this)
    {
        assert(isOK(m));

        // cm2, fm2はスコアリングに使わないほうが強かった。
        m.score = history.value(m)
            + (cm ? cm->value(m) : SCORE_ZERO)
            + (fm ? fm->value(m) : SCORE_ZERO)
            + from_to.get(t, m);
    }
}

void MovePicker::scoreEvasions()
{
    const HistoryStats& history = board.thisThread()->history;
    const FromToStats& from_to = board.thisThread()->from_to_history;
    Turn t = board.turn();
    Score see;

    for (auto& m : *this)
    {
        assert(isOK(m));

        if ((see = board.see(m)) < SCORE_ZERO)
            m.score = see - HistoryStats::Max;

        else if (isCapture(m))
        {
            m.score = captureScore(capturePieceType(m)) - LVATable[movedPieceType(m)] + HistoryStats::Max;

            if (isPawnPromote(m))
                m.score += promoteScore(movedPieceType(m));
        }

        else
            m.score = history.value(m) + from_to.get(t, m);
    }
}

// 指し手が尽きたら、ステージに応じた指し手を生成する.
void MovePicker::nextStage()
{
    assert(stage != Stage::STOP);

    current = moves;

    switch (++stage)
    {
        // 見た目の通り駒を取る手を生成。
    case GOOD_CAPTURES: case QCAPTURES_1: case QCAPTURES_2: case PROBCUT_CAPTURES:
        end_moves = generate<CAPTURE_PLUS_PROMOTE>(moves, board);
        scoreCaptures();
        break;

        // 直前の手を取り返す手のみを生成。
    case PH_RECAPTURES:
        end_moves = generate<RECAPTURES>(moves, board, recapture_square);
        scoreCaptures();
        break;

        // キラー手を試すフェーズ
    case KILLERS:
        killers[0] = ss->killers[0];
        killers[1] = ss->killers[1];
        killers[2] = counter_move;
        current = killers;
        end_moves = current + 2 + (counter_move != killers[0].move && counter_move != killers[1].move);
        break;

    case GOOD_QUIETS:
        end_moves = generate<NO_CAPTURE_MINUS_PROMOTE>(moves, board);
        end_moves = generate<DROP>(end_moves, board);
        end_quiets = end_moves;
        scoreQuiets();

        // SCORE_ZERO以上の手を先頭付近に持ってくる。
        end_moves = std::partition(current, end_moves, [](const MoveStack& m) { return m.score > SCORE_ZERO; });
        insertionSort(current, end_moves);
        break;

    case BAD_QUIETS:
        current = end_moves;
        end_moves = end_quiets;
        if (depth >= 3 * ONE_PLY)
            insertionSort(current, end_moves);
        break;

    case BAD_CAPTURES:
        current = moves + MAX_MOVES - 1;
        end_moves = end_bad_captures;
        break;

    case ALL_EVASIONS:
        end_moves = generate<EVASIONS>(moves, board);
        if (end_moves - moves > 1)
            scoreEvasions();
        break;

    case CHECKS:
        end_moves = generate<SPEED_CHECK>(moves, board);
        break;

    case EVASION: case QSEARCH_WITH_CHECKS: case QSEARCH_WITHOUT_CHECKS: case PROBCUT:
    case RECAPTURE: case STOP:
        stage = STOP;
        break;

    default: UNREACHABLE;
    }
}

// 次の指し手をひとつ返す
// 指し手が尽きればMOVE_NONEが返る。
Move MovePicker::nextMove()
{
    Move move;

    while (true)
    {
        // 現在指し手がなければ、何か指し手が生成されるか、最後のステージまで行くまでステージを進める。
        while (current == end_moves && stage != STOP)
            nextStage();

        switch (stage)
        {
        case MAIN_SEARCH: case EVASION: case QSEARCH_WITH_CHECKS:
        case QSEARCH_WITHOUT_CHECKS: case PROBCUT:
            ++current;
            return tt_move;

        case GOOD_CAPTURES:
            move = pickBest(current++, end_moves);

            if (move != tt_move)
            {
                if (board.seeGe(move, SCORE_ZERO))
                    return move;

                // 駒を損するcapturePieceは指し手バッファの一番後ろにやる。
                *end_bad_captures-- = move;
            }

            break;

        case KILLERS: // KILLERS faseでは駒を取る手は試す必要はない。この前のフェーズで駒を取る手はすべて試したから。
            move = *current++;

            if (!isNone(move)
                && move != tt_move
                && !board.piece(toSq(move))
                && !isPawnPromote(move)
                && board.pseudoLegal(move))
            {
                assert(!isCapture(move));
                return move;
            }

            break;

        case GOOD_QUIETS: case BAD_QUIETS:
            move = *current++;

            if (move != tt_move
                && move != killers[0].move
                && move != killers[1].move
                && move != killers[2].move)
                return move;

            break;

        case BAD_CAPTURES:
            return *current--;

        case ALL_EVASIONS: case QCAPTURES_1: case QCAPTURES_2:
            move = pickBest(current++, end_moves);

            if (move != tt_move)
                return move;

            break;

        case PROBCUT_CAPTURES:
            move = pickBest(current++, end_moves);

            if (move != tt_move && board.seeGe(move, threshold + 1))
                return move;

            break;


        case PH_RECAPTURES:
            move = pickBest(current++, end_moves);
            return move;

        case CHECKS:
            move = *current++;

            if (move != tt_move)
            {
                assert(!board.inCheck());
                //assert(MoveList<LEGAL>(board).contains(move));
                assert(!isCaptureOrPawnPromote(move));
                assert(board.givesCheck(move));

                return move;
            }

            break;

        case STOP:
            return MOVE_NONE;

        default:
            UNREACHABLE;
        }
    }

    return move;
}

int MovePicker::seeSign() const
{
    return stage == GOOD_CAPTURES ? 1
         : stage == BAD_CAPTURES ? -1 : 0;
    return 0;
}
