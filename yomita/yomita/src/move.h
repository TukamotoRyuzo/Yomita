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

#pragma once

#include <algorithm>
#include <iostream>
#include "piece.h"
#include "platform.h"
#include "board.h"
#include "square.h"

// 指し手の種類
enum MoveType
{
    DROP,                          // 駒打ち。 二歩は含まない。
    CAPTURE_PLUS_PAWN_PROMOTE,     // 駒を取る手 + (歩の駒を取らない成る手)。
    NO_CAPTURE_MINUS_PAWN_PROMOTE, // 駒を取らない手 - (歩の駒を取らない成る手) - (香の2段目への駒を取らない不成)
    QUIETS,                        // DROP + NO_CAPTURE_MINUS_PROMOTE
    RECAPTURES,                    // 特定の位置への取り返しの手
    EVASIONS,                      // 王手回避。歩, 飛, 角 の不成は含まない。
    NO_EVASIONS,                   // 王手が掛かっていないときの合法手 (玉の移動による自殺手、pinされている駒の移動による自殺手は回避しない。)
    LEGAL,                         // 王手が掛かっていれば EVASION, そうでないならNO_EVASIONSを生成し、玉の自殺手とpinされてる駒の移動による自殺手を排除。(連続王手の千日手は排除しない)
    LEGAL_ALL,                     // LEGAL + 不成をすべて生成
    CHECK,                         // 王手生成だが使ってない。
    CHECK_ALL,                     // 前に作ったんだがどこかに行ってしまった。
    QUIET_CHECKS,                  // 駒を取らない王手生成
    NEAR_CHECK,                    // 近接王手生成。3手詰めルーチンに必要。
    MOVETYPE_NO
};

enum Move : uint32_t { MOVE_NONE = 0, MOVE_NULL = 129 };

ENABLE_OPERATORS_ON(Move);

namespace
{
    // 手は、32bitにパックする
    // 00000000 00000000 00000000 01111111 to
    // 00000000 00000000 00111111 10000000 from
    // 00000000 00000000 01000000 00000000 promote flag
    // 00000000 00000000 10000000 00000000 drop flag
    // 00000000 00011111 00000000 00000000 moved piece
    // 00000011 11100000 00000000 00000000 captured piece

    // Moveに格納するときに必要なシフト数
    const uint32_t TO_SHIFT      = 0;
    const uint32_t FROM_SHIFT    = 7;
    const uint32_t PROMOTE_SHIFT = 14;
    const uint32_t DROP_SHIFT    = 15;
    const uint32_t PIECE_SHIFT   = 16;
    const uint32_t CAPTURE_SHIFT = 21;

    // Moveとのマスク定数
    const uint32_t TO_MASK          = 0x7f << TO_SHIFT;
    const uint32_t FROM_MASK        = 0x7f << FROM_SHIFT;
    const uint32_t TURN_MASK        = 0x10 << PIECE_SHIFT;
    const uint32_t DROP_MASK        = 0x01 << DROP_SHIFT;
    const uint32_t PROMOTE_MASK     = 0x01 << PROMOTE_SHIFT;
    const uint32_t PIECE_MASK       = 0x1f << PIECE_SHIFT;
    const uint32_t CAPTURE_MASK     = 0x1f << CAPTURE_SHIFT;
    const uint32_t PIECETYPE_MASK   = 0x0f << PIECE_SHIFT;
    const uint32_t CAPTURETYPE_MASK = 0x0f << CAPTURE_SHIFT;
    
    // 歩を成る手かどうかを確かめるときに見なければならないビット
    const uint32_t PAWN_PROMOTE	= PROMOTE_MASK | (PAWN << PIECE_SHIFT);

    inline constexpr Move      toToMove(const Square to)   { return Move(to   <<      TO_SHIFT); }
    inline constexpr Move    fromToMove(const Square from) { return Move(from <<    FROM_SHIFT); }
    inline constexpr Move   pieceToMove(const Piece pc)    { return Move(pc   <<   PIECE_SHIFT); }
    inline constexpr Move captureToMove(const Piece pc)    { return Move(pc   << CAPTURE_SHIFT); }
    inline constexpr Move promoteToMove(const bool pro)    { return Move(pro  << PROMOTE_SHIFT); }
} // namespace

// 駒を動かす手を生成
template <MoveType MT> inline Move makeMove(const Square from, const Square to, const Piece p, const Board& b)
{
    assert(typeOf(b.piece(to)) != KING);
    assert(b.piece(from) != EMPTY);

    if (MT == NO_CAPTURE_MINUS_PAWN_PROMOTE)
        return toToMove(to) | fromToMove(from) | pieceToMove(p);
    else
        return toToMove(to) | fromToMove(from) | pieceToMove(p) | captureToMove(b.piece(to));
}

// 駒を成る手を生成
template <MoveType MT> inline Move makeMovePromote(const Square from, const Square to, const Piece p, const Board& b)
{
    assert(typeOf(b.piece(to)) != KING);
    assert(b.piece(from) != EMPTY);

    if (MT == NO_CAPTURE_MINUS_PAWN_PROMOTE)
        return toToMove(to) | fromToMove(from) | pieceToMove(p) | Move(PROMOTE_MASK);
    else
        return toToMove(to) | fromToMove(from) | pieceToMove(p) | captureToMove(b.piece(to)) | Move(PROMOTE_MASK);
}

// 駒を動かす手を生成
inline Move makeMove(const Square from, const Square to, const Piece p, const Piece c, const bool promote)
{
    assert(typeOf(c) != KING);

    return toToMove(to) | fromToMove(from) | pieceToMove(p) | captureToMove(c) | promoteToMove(promote);
}

// 駒を打つ手を生成
inline constexpr Move makeDrop(const Piece drop) { return Move(DROP_MASK) | pieceToMove(drop); }
inline Move makeDrop(const Piece drop, const Square to) { return toToMove(to) | Move(DROP_MASK) | pieceToMove(drop); }

// 移動先を返す。
inline Square toSq(const Move m) { return Square(m & TO_MASK); }

// 移動元を返す。
inline Square fromSq(const Move m) { return Square((m & FROM_MASK) >> FROM_SHIFT); }

// Moveの種類の判定
inline bool isDrop                (const Move m) { return m & DROP_MASK;        }
inline bool isPromote             (const Move m) { return m & PROMOTE_MASK;     }
inline bool isCapture             (const Move m) { return m & CAPTURETYPE_MASK; }
inline bool isPawnPromote         (const Move m) { return (m & (PROMOTE_MASK | PIECETYPE_MASK)) == PAWN_PROMOTE; }
inline bool isCaptureOrPawnPromote(const Move m) { return isCapture(m) || isPawnPromote(m); }

// 動かした駒を返す。駒打ちなら、打った駒。
inline Piece     movedPiece       (const Move m) { return Piece    ((m & PIECE_MASK    ) >> PIECE_SHIFT); }
inline PieceType movedPieceType   (const Move m) { return PieceType((m & PIECETYPE_MASK) >> PIECE_SHIFT); }

// 動かした駒を返すが、成りなら成った駒を返す。
inline Piece     movedPieceTo     (const Move m) { return isPromote(m) ? promotePiece    (movedPiece    (m)) : movedPiece    (m); }
inline PieceType movedPieceTypeTo (const Move m) { return isPromote(m) ? promotePieceType(movedPieceType(m)) : movedPieceType(m); }

// 取った駒を返す。
inline Piece     capturePiece	  (const Move m) { return Piece    ((m & CAPTURE_MASK    ) >> CAPTURE_SHIFT); }
inline PieceType capturePieceType (const Move m) { return PieceType((m & CAPTURETYPE_MASK) >> CAPTURE_SHIFT); }

// 指し手の手番を返す
inline Turn turnOf(const Move m) { return Turn(!!(m & TURN_MASK)); }

// NULLMOVEやNONEでないかの確認
inline bool isOK(const Move m) { return m != MOVE_NONE && m != MOVE_NULL; }

// USI変換用
std::string toUSI(const Move m);
std::string toCSA(const Move m);
std::string pretty(const Move m);

// Move型を人間にとってわかりやすい形式で出力する。デバッグ用。
std::ostream& operator << (std::ostream& os, const Move m);

// 手とスコアが一緒になった構造体
// TODO: スコアはスコアリングするときにくっつければいい気もする。
struct MoveStack
{
    Move move;
    int32_t score;

    operator Move() const { return move; }
    void operator = (Move m) { move = m; }
};

// insertionSort() や std::sort() で必要
inline bool operator < (const MoveStack& f, const MoveStack& s) { return f.score < s.score; }
inline bool operator > (const MoveStack& f, const MoveStack& s) { return f.score > s.score; }

