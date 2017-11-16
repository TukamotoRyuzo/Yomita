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

#pragma once

#include "range.h"
#include "common.h"
#include "platform.h"
#include "enumoperator.h"

enum Turn { BLACK, WHITE, TURN_MAX };

ENABLE_OPERATORS_ON(Turn);

inline constexpr Turn operator ~ (const Turn t) { return Turn(t ^ 1); }

enum File { FILE_9, FILE_8, FILE_7, FILE_6, FILE_5, FILE_4, FILE_3, FILE_2, FILE_1, FILE_ZERO = 0, FILE_MAX = 9 };
enum Rank { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_9, RANK_ZERO = 0, RANK_MAX = 9 };
enum Square
{
    SQ_91, SQ_81, SQ_71, SQ_61, SQ_51, SQ_41, SQ_31, SQ_21, SQ_11,
    SQ_92, SQ_82, SQ_72, SQ_62, SQ_52, SQ_42, SQ_32, SQ_22, SQ_12,
    SQ_93, SQ_83, SQ_73, SQ_63, SQ_53, SQ_43, SQ_33, SQ_23, SQ_13,
    SQ_94, SQ_84, SQ_74, SQ_64, SQ_54, SQ_44, SQ_34, SQ_24, SQ_14,
    SQ_95, SQ_85, SQ_75, SQ_65, SQ_55, SQ_45, SQ_35, SQ_25, SQ_15,
    SQ_96, SQ_86, SQ_76, SQ_66, SQ_56, SQ_46, SQ_36, SQ_26, SQ_16,
    SQ_97, SQ_87, SQ_77, SQ_67, SQ_57, SQ_47, SQ_37, SQ_27, SQ_17,
    SQ_98, SQ_88, SQ_78, SQ_68, SQ_58, SQ_48, SQ_38, SQ_28, SQ_18,
    SQ_99, SQ_89, SQ_79, SQ_69, SQ_59, SQ_49, SQ_39, SQ_29, SQ_19,
    SQ_ZERO = 0, SQ_MAX = 81,
    SQ_MAX_PLUS1 = SQ_MAX + 1,
    SQ_U = -9, SQ_D = 9, SQ_R = 1, SQ_L = -1,
    SQ_RU = -8, SQ_RD = 10, SQ_LD = 8, SQ_LU = -10,
};

// 方向を意味するbit
enum RelationType : uint8_t
{
    DIRECT_MISC,  // 方向が縦横斜めの関係に無い場合
    DIRECT_FILE,  // 方向が縦方向の関係である
    DIRECT_RANK,  // 方向が横方向の関係である
    DIRECT_DIAG1, // 方向が右上から左下方向の斜め関係である
    DIRECT_DIAG2, // 方向が左上から右下方向の斜め関係である
};

ENABLE_OPERATORS_ON(File);
ENABLE_OPERATORS_ON(Rank);
ENABLE_OPERATORS_ON(Square);

// それぞれの範囲に収まっているかどうかを返す
inline bool isOK(const File f) { return f >= FILE_ZERO && f < FILE_MAX; }
inline bool isOK(const Rank r) { return r >= RANK_ZERO && r < RANK_MAX; }
inline bool isOK(const Square sq) { return sq >= SQ_ZERO && sq < SQ_MAX; }

// Squareを与えると、Rank, Fileを返す。
inline Rank rankOf(const Square sq) { return Rank(sq / FILE_MAX); }
inline File fileOf(const Square sq) { return File(sq % RANK_MAX); }
inline Square sqOf(const File f, const Rank r) { return Square(f + r * (int)FILE_MAX); }

// 後手の位置を先手の位置へ変換
inline File inverse(const File f) { return File(FILE_MAX - 1 - f); }
inline Rank inverse(const Rank r) { return Rank(RANK_MAX - 1 - r); }
inline Square inverse(const Square sq) { return Square(SQ_MAX - 1 - sq); }

// 左右でミラーした位置を返す。
inline Square mirror(const Square sq) { return sqOf(inverse(fileOf(sq)), rankOf(sq)); }

// 手番側から見たRankを返す。
inline Rank relativeRank(const Rank r, const Turn t) { return t == BLACK ? r : inverse(r); }

// 画面出力用
std::ostream& operator << (std::ostream& os, const File f);
std::ostream& operator << (std::ostream& os, const Rank r);
std::ostream& operator << (std::ostream& os, const Square sq);

// 見やすい形で出力
std::string pretty(const File f);
std::string pretty(const Rank r);
std::string pretty(const Square sq);
std::string fromPretty(const Square sq);

// CSA形式で出力
std::string toCSA(const Rank r);
std::string toCSA(const File f);
std::string toCSA(const Square sq);

// USI形式で出力
std::string toUSI(const Rank r);
std::string toUSI(const File f);
std::string toUSI(const Square sq);

// Squareが手番側から見て成れるかどうかを返す。
inline bool canPromote(const Turn t, const Square sq) { return t == BLACK ? sq <= SQ_13 : sq >= SQ_97; }
inline bool canPromote(const Turn t, const Rank r) { return t == BLACK ? r <= RANK_3 : r >= RANK_7; }

// Rankがt_rankより手前にあるかどうかを返す。t_rankは先手から見たRankを渡す。
inline bool isBehind(const Turn t, const Rank t_rank, const Rank r) { return t == BLACK ? r > t_rank : r < inverse(t_rank); }
inline bool isBehind(const Turn t, const Rank t_rank, const Square sq) { return isBehind(t, t_rank, rankOf(sq)); }

extern RelationType SQUARE_RELATIONSHIP[SQ_MAX][SQ_MAX];

// 位置関係を返す。
inline RelationType relation(const Square sq1, const Square sq2) { return SQUARE_RELATIONSHIP[sq1][sq2]; }

// from, to, ksqが同じ位置関係のときtrueを返す。
inline bool isAligned(const Square from, const Square to, const Square ksq)
{
    const RelationType rt = relation(from, ksq);
    return rt != DIRECT_MISC && rt == relation(from, to);
}

// range名は新たな型名という扱いにする。こいつらは最後にsをつける。
constexpr Range<File, FILE_ZERO, FILE_MAX> Files{};
constexpr Range<Rank, RANK_ZERO, RANK_MAX> Ranks{};
constexpr Range<Square, SQ_ZERO, SQ_MAX> Squares{};
constexpr Range<Turn, BLACK, TURN_MAX> Turns{};

typedef uint64_t Key;

// 通常探索時の最大探索深さ
const int MAX_PLY = 128;

// 最大の合法手数
const int MAX_MOVES = 593 + 1;

// スコア(評価値)がどういう探索の結果得た値なのかを表現する。
enum Bound { BOUND_NONE, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT = BOUND_UPPER | BOUND_LOWER, };

// 評価値
enum Score
{
    SCORE_ZERO = 0,
    SCORE_DRAW = -50,
    SCORE_KNOWN_WIN = 10000,
    SCORE_MATE = 32000,
    SCORE_INFINITE = 32601,
    SCORE_NONE = 32602,
    SCORE_SUPERIOR = 30000,
    SCORE_INFERIOR = -30000,

    SCORE_MATE_IN_MAX_PLY = SCORE_MATE - MAX_PLY,
    SCORE_MATED_IN_MAX_PLY = -SCORE_MATE + MAX_PLY,

    SCORE_NOT_EVALUATED = INT_MAX,
};

ENABLE_OPERATORS_ON(Score);

// ply手で詰ました時のスコア、詰まされた時のスコア。
inline Score mateIn(int ply) { return  SCORE_MATE - ply; }
inline Score matedIn(int ply) { return -SCORE_MATE + ply; }

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
inline constexpr Piece operator | (const Turn t, const PieceType pt) { return Piece((int)pt | (int)t << 4); }

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
inline constexpr bool isTGold(const Piece p) { return IS_TGOLD & (1 << p); }
inline constexpr bool isTGold(const PieceType pt) { return IS_TGOLD & (1 << pt); }

// 金の動きをする駒なら金、そうでないならその駒自身を返す。	
inline Piece tgold(const Piece p) { return isTGold(p) ? Piece((p & 0x10) | GOLD) : p; }
inline PieceType tgold(const PieceType pt) { return isTGold(pt) ? GOLD : pt; }

// 成っているかどうかを返す。
inline constexpr bool isPromoted(const Piece p) { return p & PROMOTED; }
inline constexpr bool isPromoted(const PieceType pt) { return pt & PROMOTED; }

// 成れない駒かどうかを返す。
inline constexpr bool isNoPromotable(const Piece p) { return NO_PROMOTABLE & (1 << p); }
inline constexpr bool isNoPromotable(const PieceType pt) { return NO_PROMOTABLE & (1 << pt); }

// 飛び駒かどうかを返す
inline constexpr bool isSlider(const Piece p) { return IS_SLIDER & (1 << p); }

// 成りを除いた駒種を返す。
inline constexpr PieceType nativeType(const PieceType pt) { return PieceType(pt & 7); }
inline constexpr PieceType nativeType(const Piece p) { return PieceType(p & 7); }

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

// 駒種を返す。
inline PieceType typeOf(const Piece p) { return PieceType(p & 0x0f); }

// 駒の手番を返す。
inline Turn      turnOf(const Piece p) { return Turn(p >> 4); }

// pが自分の駒かどうかを返す。
inline bool isMine(const Turn t, const Piece p) { return p && (turnOf(p) == t); }

// USIプロトコルで駒を表す文字列を返す。
std::string toUSI(Piece pc); 

// BISHOP から PRO_SILVER まで。
constexpr Range<PieceType, BISHOP, PIECETYPE_MAX> PieceTypes{};

// BISHOP から KING まで。
constexpr Range<PieceType, BISHOP, NATIVE_MAX> NativeType{};

// BISHOP から GOLD まで。
constexpr Range<PieceType, BISHOP, HAND_MAX> HandPiece{};

// 駒の価値
constexpr Score PAWN_SCORE = static_cast<Score>(100 * 9 / 10);
constexpr Score LANCE_SCORE = static_cast<Score>(350 * 9 / 10);
constexpr Score KNIGHT_SCORE = static_cast<Score>(450 * 9 / 10);
constexpr Score SILVER_SCORE = static_cast<Score>(550 * 9 / 10);
constexpr Score GOLD_SCORE = static_cast<Score>(600 * 9 / 10);
constexpr Score BISHOP_SCORE = static_cast<Score>(950 * 9 / 10);
constexpr Score ROOK_SCORE = static_cast<Score>(1100 * 9 / 10);
constexpr Score PRO_PAWN_SCORE = static_cast<Score>(600 * 9 / 10);
constexpr Score PRO_LANCE_SCORE = static_cast<Score>(600 * 9 / 10);
constexpr Score PRO_KNIGHT_SCORE = static_cast<Score>(600 * 9 / 10);
constexpr Score PRO_SILVER_SCORE = static_cast<Score>(600 * 9 / 10);
constexpr Score HORSE_SCORE = static_cast<Score>(1050 * 9 / 10);
constexpr Score DRAGON_SCORE = static_cast<Score>(1550 * 9 / 10);
constexpr Score KING_SCORE = static_cast<Score>(15000);

// その駒を取ったときに動くスコア
constexpr Score CAPTURE_PAWN_SCORE = PAWN_SCORE * 2;
constexpr Score CAPTURE_LANCE_SCORE = LANCE_SCORE * 2;
constexpr Score CAPTURE_KNIGHT_SCORE = KNIGHT_SCORE * 2;
constexpr Score CAPTURE_SILVER_SCORE = SILVER_SCORE * 2;
constexpr Score CAPTURE_GOLD_SCORE = GOLD_SCORE * 2;
constexpr Score CAPTURE_BISHOP_SCORE = BISHOP_SCORE * 2;
constexpr Score CAPTURE_ROOK_SCORE = ROOK_SCORE * 2;
constexpr Score CAPTURE_PRO_PAWN_SCORE = PRO_PAWN_SCORE + PAWN_SCORE;
constexpr Score CAPTURE_PRO_LANCE_SCORE = PRO_LANCE_SCORE + LANCE_SCORE;
constexpr Score CAPTURE_PRO_KNIGHT_SCORE = PRO_KNIGHT_SCORE + KNIGHT_SCORE;
constexpr Score CAPTURE_PRO_SILVER_SCORE = PRO_SILVER_SCORE + SILVER_SCORE;
constexpr Score CAPTURE_HORSE_SCORE = HORSE_SCORE + BISHOP_SCORE;
constexpr Score CAPTURE_DRAGON_SCORE = DRAGON_SCORE + ROOK_SCORE;
constexpr Score CAPTURE_KING_SCORE = KING_SCORE * 2;

// その駒が成ることの価値
constexpr Score PROMOTE_PAWN_SCORE = PRO_PAWN_SCORE - PAWN_SCORE;
constexpr Score PROMOTE_LANCE_SCORE = PRO_LANCE_SCORE - LANCE_SCORE;
constexpr Score PROMOTE_KNIGHT_SCORE = PRO_KNIGHT_SCORE - KNIGHT_SCORE;
constexpr Score PROMOTE_SILVER_SCORE = PRO_SILVER_SCORE - SILVER_SCORE;
constexpr Score PROMOTE_BISHOP_SCORE = HORSE_SCORE - BISHOP_SCORE;
constexpr Score PROMOTE_ROOK_SCORE = DRAGON_SCORE - ROOK_SCORE;

constexpr Score SCORE_WIN = KING_SCORE;
