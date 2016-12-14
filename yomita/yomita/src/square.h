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

#include <string>
#include "config.h"
#include "platform.h"
#include "range.h"
#include "enumoperator.h"
#include "turn.h"

class Bitboard;

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
    DELTA_N = -9, DELTA_S = 9, DELTA_E = 1, DELTA_W = -1,
    DELTA_NE = -8, DELTA_SE = 10, DELTA_SW = 8, DELTA_NW = -10,
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

// 先手から見たRankを返す。
inline Rank relativeRank(const Rank r, const Turn t) { return t == BLACK ? r : inverse(r); }

// やねうら王やAperyのevalファイルは右上開始の縦型Square用だが読み太は横型Squareなので、
// 動作が同じになるように変換用関数を定義する。また、評価関数ファイルを変換するときにも使う。
#if defined USE_FILE_SQUARE_EVAL || defined CONVERT_EVAL
enum EvalSquare
{
    BSQ_11, BSQ_12, BSQ_13, BSQ_14, BSQ_15, BSQ_16, BSQ_17, BSQ_18, BSQ_19,
    BSQ_21, BSQ_22, BSQ_23, BSQ_24, BSQ_25, BSQ_26, BSQ_27, BSQ_28, BSQ_29,
    BSQ_31, BSQ_32, BSQ_33, BSQ_34, BSQ_35, BSQ_36, BSQ_37, BSQ_38, BSQ_39,
    BSQ_41, BSQ_42, BSQ_43, BSQ_44, BSQ_45, BSQ_46, BSQ_47, BSQ_48, BSQ_49,
    BSQ_51, BSQ_52, BSQ_53, BSQ_54, BSQ_55, BSQ_56, BSQ_57, BSQ_58, BSQ_59,
    BSQ_61, BSQ_62, BSQ_63, BSQ_64, BSQ_65, BSQ_66, BSQ_67, BSQ_68, BSQ_69,
    BSQ_71, BSQ_72, BSQ_73, BSQ_74, BSQ_75, BSQ_76, BSQ_77, BSQ_78, BSQ_79,
    BSQ_81, BSQ_82, BSQ_83, BSQ_84, BSQ_85, BSQ_86, BSQ_87, BSQ_88, BSQ_89,
    BSQ_91, BSQ_92, BSQ_93, BSQ_94, BSQ_95, BSQ_96, BSQ_97, BSQ_98, BSQ_99,
};

ENABLE_OPERATORS_ON(EvalSquare);
extern const EvalSquare SQ_TO_EVALSQ[SQ_MAX];
inline EvalSquare toEvalSq(const Square sq) { return SQ_TO_EVALSQ[sq]; }
inline EvalSquare mirror(const EvalSquare sq) { return toEvalSq(mirror(Square(sq))); }
inline EvalSquare inverse(const EvalSquare sq) { return EvalSquare(81 - 1 - sq); }

#else
typedef Square EvalSquare;
#define toEvalSq(sq) (sq)
#endif

#ifdef GENERATED_SFEN_BY_FILESQ
extern const Square NEXT_SQ[SQ_MAX];
extern const Square FILE_SQ[SQ_MAX];

// 縦型Squareのfor文での増分用関数。
inline Square nextSq(const Square sq) { return NEXT_SQ[sq]; }

// 縦型Squareから横型Squareに変換する。
inline Square fileSq(const Square sq) {	return FILE_SQ[sq]; }
#endif

// 画面出力用
std::ostream& operator << (std::ostream& os, const File f);
std::ostream& operator << (std::ostream& os, const Rank r);
std::ostream& operator << (std::ostream& os, const Square sq);

// 見やすい形で出力
std::string pretty(const File f);
std::string pretty(const Rank r);
std::string pretty(const Square sq);
std::string fromPretty(const Square sq);

// USI形式で出力
std::string toUSI(const Rank r);
std::string toUSI(const File f);
std::string toUSI(const Square sq);

// CSA形式で出力
std::string toCSA(const Rank r);
std::string toCSA(const File f);
std::string toCSA(const Square sq);

// Squareが手番側から見て成れるかどうかを返す。
inline bool canPromote(const Turn t, const Square sq) { return t == BLACK ? sq <= SQ_13 : sq >= SQ_97; }
inline bool canPromote(const Turn t, const Rank r)    { return t == BLACK ? r <= RANK_3 : r >= RANK_7; }

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
