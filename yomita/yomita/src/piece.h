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

#include "platform.h"
#include "score.h"
#include "range.h"
#include "bitboard.h"

// 奇妙な並びだが、メリットはPRO_PAWN以上の成駒はすべて金の動きをする駒として扱えること。
// また、GOLD以上の駒はすべて成駒として扱える。
// 以前のソースではこの並びを活用してBoardクラスのPieceType毎のbitboardをDRAGONまでにして節約していた。
enum PieceType
{
    NO_PIECE_TYPE, BISHOP, ROOK, PAWN, LANCE, KNIGHT, SILVER, GOLD, KING,
    HORSE, DRAGON, PRO_PAWN, PRO_LANCE, PRO_KNIGHT, PRO_SILVER, OCCUPIED = NO_PIECE_TYPE,
    PIECETYPE_MAX = PRO_SILVER + 1,
    NATIVE_MAX = KING + 1,
    HAND_MAX = GOLD + 1,
}; 

enum Piece
{	
    EMPTY = 0, B_BISHOP, B_ROOK, B_PAWN, B_LANCE, B_KNIGHT, B_SILVER, B_GOLD, B_KING,
    B_HORSE, B_DRAGON, B_PRO_PAWN, B_PRO_LANCE, B_PRO_KNIGHT, B_PRO_SILVER,
    W_BISHOP = 17, W_ROOK, W_PAWN, W_LANCE, W_KNIGHT, W_SILVER, W_GOLD, W_KING,
    W_HORSE, W_DRAGON, W_PRO_PAWN, W_PRO_LANCE, W_PRO_KNIGHT, W_PRO_SILVER,
    PIECE_MAX = W_PRO_SILVER + 1,
    SIGN_WHITE = 16,
};

inline bool isOK(const PieceType pt) { return pt >= NO_PIECE_TYPE && pt < PIECETYPE_MAX; }
inline bool isOK(const Piece p) { return p >= EMPTY && p < PIECE_MAX; }

ENABLE_OPERATORS_ON(PieceType);
ENABLE_OPERATORS_ON(Piece);

inline constexpr Piece operator | (const PieceType pt, const Turn t) { return Piece((int)pt | (int)t << 4); }

// Boardクラスで用いる、駒リスト(どの駒がどこにあるのか)を管理するときの番号
// 駒得しか使わない場合だといらないが、Boardクラスのインタフェースを変えないために定義。
enum PieceNo
{
    PIECE_NO_PAWN = 0, PIECE_NO_LANCE = 18, PIECE_NO_KNIGHT = 22, PIECE_NO_SILVER = 26,
    PIECE_NO_GOLD = 30, PIECE_NO_BISHOP = 34, PIECE_NO_ROOK = 36, PIECE_NO_KING = 38,
    PIECE_NO_BKING = 38, PIECE_NO_WKING = 39,
    PIECE_NO_ZERO = 0, PIECE_NO_NB = 40,
};

ENABLE_OPERATORS_ON(PieceNo);

// PieceNoの整合性の検査。assert用。
constexpr bool isOK(PieceNo pn) { return PIECE_NO_ZERO <= pn && pn < PIECE_NO_NB; }

const int PROMOTED = 8;
const int IS_SLIDER = 1 << B_BISHOP | 1 << B_HORSE | 1 << B_ROOK | 1 << B_DRAGON | 1 << B_LANCE
                    | 1 << W_BISHOP | 1 << W_HORSE | 1 << W_ROOK | 1 << W_DRAGON | 1 << W_LANCE;

const int NO_PROMOTABLE = 1 << B_GOLD | 1 << B_KING | 1 << B_PRO_PAWN | 1 << B_PRO_LANCE | 1 << B_PRO_KNIGHT | 1 << B_PRO_SILVER | 1 << B_HORSE | 1 << B_DRAGON |
                          1 << W_GOLD | 1 << W_KING | 1 << W_PRO_PAWN | 1 << W_PRO_LANCE | 1 << W_PRO_KNIGHT | 1 << W_PRO_SILVER | 1 << W_HORSE | 1 << W_DRAGON;

const int IS_TGOLD = 1 << B_GOLD | 1 << B_PRO_PAWN | 1 << B_PRO_LANCE | 1 << B_PRO_KNIGHT | 1 << B_PRO_SILVER | 
                     1 << W_GOLD | 1 << W_PRO_PAWN | 1 << W_PRO_LANCE | 1 << W_PRO_KNIGHT | 1 << W_PRO_SILVER;

// 金の動きをする駒かどうかを返す。
inline bool isTGold(const Piece p) { return IS_TGOLD & (1 << p); }
inline bool isTGold(const PieceType pt) { return IS_TGOLD & (1 << pt); }

// 金の動きをする駒なら金、そうでないならその駒自身を返す。	
inline Piece tgold(const Piece p) { return isTGold(p) ? Piece((p & 0x10) | GOLD) : p; }
inline PieceType tgold(const PieceType pt) { return isTGold(pt) ? GOLD : pt; }

// 成っているかどうかを返す。
inline bool isPromoted(const Piece p) { return p & PROMOTED; }
inline bool isPromoted(const PieceType pt) { return pt & PROMOTED; }

// 成れない駒かどうかを返す。
inline bool isNoPromotable(const Piece p) { return NO_PROMOTABLE & (1 << p); }
inline bool isNoPromotable(const PieceType pt) { return NO_PROMOTABLE & (1 << pt); }

// 飛び駒かどうかを返す
inline bool isSlider(const Piece p) { return IS_SLIDER & (1 << p); }

// 成りを除いた駒種を返す。
inline PieceType nativeType(const PieceType pt) { return PieceType(pt & 7); }
inline PieceType nativeType(const Piece p) { return PieceType(p & 7); }

// 成った駒を返す。
inline Piece     promotePiece    (const Piece p)      { return Piece    (p  | PROMOTED); }
inline PieceType promotePieceType(const PieceType pt) { return PieceType(pt | PROMOTED); }

// テーブル
extern const Score SCORE        [PIECE_MAX];
extern const Score CAPTURE_SCORE[PIECE_MAX];
extern const Score PROMOTE_SCORE[PIECE_MAX];

// 駒の価値を返す。後手の駒ならマイナス
inline Score pieceScore  (int p) { assert(isOK(Piece(p))); return SCORE[p]; }

// その駒を取ったときに動くスコア：駒の価値の2倍を返す。後手の駒ならマイナス
inline Score captureScore(int p) { assert(isOK(Piece(p))); return CAPTURE_SCORE[p]; }

// その駒が成ることの価値を返す。なることのできない駒なら0を返す。後手の駒ならマイナス
inline Score promoteScore(int p) { assert(isOK(Piece(p))); return PROMOTE_SCORE[p]; }

// 画面出力用
std::ostream& operator << (std::ostream& os, const Piece p);
std::string pretty(const PieceType pt);

inline constexpr Piece toPiece(const PieceType pt, const Turn t) { return  Piece(pt | (t << 4)); }

// 駒種を返す。
inline PieceType typeOf(const Piece p) { return PieceType(p & 0x0f); }

// 駒の手番を返す。
inline Turn      turnOf(const Piece p) { return Turn(p >> 4); }

// 駒が定数でない場合は、これで振り分ける
inline Bitboard attackAll(const Piece pc, const Square sq, const Bitboard& occupied)
{
    switch (pc)
    {

    case B_PAWN:	return pawnAttack(BLACK, sq);
    case B_KNIGHT:	return knightAttack(BLACK, sq);
    case B_SILVER:	return silverAttack(BLACK, sq);
    case B_LANCE:   return lanceAttack(BLACK, sq, occupied);
    case B_GOLD: case B_PRO_PAWN: case B_PRO_LANCE: case B_PRO_KNIGHT: case B_PRO_SILVER: return goldAttack(BLACK, sq);

    case W_PAWN:	return pawnAttack(WHITE, sq);
    case W_LANCE:   return lanceAttack(WHITE, sq, occupied);
    case W_KNIGHT:	return knightAttack(WHITE, sq);
    case W_SILVER:	return silverAttack(WHITE, sq);
    case W_GOLD: case W_PRO_PAWN: case W_PRO_LANCE: case W_PRO_KNIGHT: case W_PRO_SILVER: return goldAttack(WHITE, sq);

    case B_BISHOP: case W_BISHOP:	return bishopAttack(sq, occupied);
    case B_ROOK:   case W_ROOK:		return rookAttack(sq, occupied);
    case B_HORSE:  case W_HORSE:	return horseAttack(sq, occupied);
    case B_DRAGON: case W_DRAGON:   return dragonAttack(sq, occupied);
    case B_KING:   case W_KING:		return kingAttack(sq);
    
    default: UNREACHABLE;
    }
    return allZeroMask();
}

// USIプロトコルで駒を表す文字列を返す。
std::string toUSI(Piece pc); 

// BISHOP から PRO_SILVER まで。
constexpr Range<PieceType, BISHOP, PIECETYPE_MAX> PieceTypes{};

// BISHOP から KING まで。
constexpr Range<PieceType, BISHOP, NATIVE_MAX> NativeType{};

// BISHOP から GOLD まで。
constexpr Range<PieceType, BISHOP, HAND_MAX> HandPiece{};

