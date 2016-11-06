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

#include "square.h"
#include "bitboard.h"

const Bitboard FILE9_MASK = Bitboard(0x40201008040201ULL << 0, 0x40201008040201ULL << 0);
const Bitboard FILE8_MASK = Bitboard(0x40201008040201ULL << 1, 0x40201008040201ULL << 1);
const Bitboard FILE7_MASK = Bitboard(0x40201008040201ULL << 2, 0x40201008040201ULL << 2);
const Bitboard FILE6_MASK = Bitboard(0x40201008040201ULL << 3, 0x40201008040201ULL << 3);
const Bitboard FILE5_MASK = Bitboard(0x40201008040201ULL << 4, 0x40201008040201ULL << 4);
const Bitboard FILE4_MASK = Bitboard(0x40201008040201ULL << 5, 0x40201008040201ULL << 5);
const Bitboard FILE3_MASK = Bitboard(0x40201008040201ULL << 6, 0x40201008040201ULL << 6);
const Bitboard FILE2_MASK = Bitboard(0x40201008040201ULL << 7, 0x40201008040201ULL << 7);
const Bitboard FILE1_MASK = Bitboard(0x40201008040201ULL << 8, 0x40201008040201ULL << 8);

const Bitboard FILE_MASK[FILE_MAX] =
{
	FILE9_MASK, FILE8_MASK, FILE7_MASK, FILE6_MASK, FILE5_MASK, FILE4_MASK, FILE3_MASK, FILE2_MASK, FILE1_MASK,
};

const Bitboard RANK1_MASK = Bitboard(0x1ffULL << (9 * 0), 0);
const Bitboard RANK2_MASK = Bitboard(0x1ffULL << (9 * 1), 0);
const Bitboard RANK3_MASK = Bitboard(0x1ffULL << (9 * 2), 0x1ffULL << (9 * 0));
const Bitboard RANK4_MASK = Bitboard(0x1ffULL << (9 * 3), 0x1ffULL << (9 * 1));
const Bitboard RANK5_MASK = Bitboard(0x1ffULL << (9 * 4), 0x1ffULL << (9 * 2));
const Bitboard RANK6_MASK = Bitboard(0x1ffULL << (9 * 5), 0x1ffULL << (9 * 3));
const Bitboard RANK7_MASK = Bitboard(0x1ffULL << (9 * 6), 0x1ffULL << (9 * 4));
const Bitboard RANK8_MASK = Bitboard(0, 0x1ffULL << (9 * 5));
const Bitboard RANK9_MASK = Bitboard(0, 0x1ffULL << (9 * 6));

const Bitboard RANK_MASK[RANK_MAX] =
{
	RANK1_MASK, RANK2_MASK, RANK3_MASK, RANK4_MASK, RANK5_MASK, RANK6_MASK, RANK7_MASK, RANK8_MASK, RANK9_MASK,	
};

const Bitboard SQUARE_MASK[SQ_MAX] =
{
	Bitboard(1ULL << 0, 0), Bitboard(1ULL << 1, 0), Bitboard(1ULL << 2, 0), Bitboard(1ULL << 3, 0), Bitboard(1ULL << 4, 0), Bitboard(1ULL << 5, 0),
	Bitboard(1ULL << 6, 0), Bitboard(1ULL << 7, 0), Bitboard(1ULL << 8, 0), Bitboard(1ULL << 9, 0), Bitboard(1ULL << 10, 0), Bitboard(1ULL << 11, 0),
	Bitboard(1ULL << 12, 0), Bitboard(1ULL << 13, 0), Bitboard(1ULL << 14, 0), Bitboard(1ULL << 15, 0), Bitboard(1ULL << 16, 0), Bitboard(1ULL << 17, 0),
	Bitboard(1ULL << 18, 1ULL << 0), Bitboard(1ULL << 19, 1ULL << 1), Bitboard(1ULL << 20, 1ULL << 2), Bitboard(1ULL << 21, 1ULL << 3), Bitboard(1ULL << 22, 1ULL << 4),
	Bitboard(1ULL << 23, 1ULL << 5), Bitboard(1ULL << 24, 1ULL << 6), Bitboard(1ULL << 25, 1ULL << 7), Bitboard(1ULL << 26, 1ULL << 8), Bitboard(1ULL << 27, 1ULL << 9),
	Bitboard(1ULL << 28, 1ULL << 10), Bitboard(1ULL << 29, 1ULL << 11), Bitboard(1ULL << 30, 1ULL << 12), Bitboard(1ULL << 31, 1ULL << 13), Bitboard(1ULL << 32, 1ULL << 14),
	Bitboard(1ULL << 33, 1ULL << 15), Bitboard(1ULL << 34, 1ULL << 16), Bitboard(1ULL << 35, 1ULL << 17), Bitboard(1ULL << 36, 1ULL << 18), Bitboard(1ULL << 37, 1ULL << 19),
	Bitboard(1ULL << 38, 1ULL << 20), Bitboard(1ULL << 39, 1ULL << 21), Bitboard(1ULL << 40, 1ULL << 22), Bitboard(1ULL << 41, 1ULL << 23), Bitboard(1ULL << 42, 1ULL << 24),
	Bitboard(1ULL << 43, 1ULL << 25), Bitboard(1ULL << 44, 1ULL << 26), Bitboard(1ULL << 45, 1ULL << 27), Bitboard(1ULL << 46, 1ULL << 28), Bitboard(1ULL << 47, 1ULL << 29),
	Bitboard(1ULL << 48, 1ULL << 30), Bitboard(1ULL << 49, 1ULL << 31), Bitboard(1ULL << 50, 1ULL << 32), Bitboard(1ULL << 51, 1ULL << 33), Bitboard(1ULL << 52, 1ULL << 34),
	Bitboard(1ULL << 53, 1ULL << 35), Bitboard(1ULL << 54, 1ULL << 36), Bitboard(1ULL << 55, 1ULL << 37), Bitboard(1ULL << 56, 1ULL << 38), Bitboard(1ULL << 57, 1ULL << 39),
	Bitboard(1ULL << 58, 1ULL << 40), Bitboard(1ULL << 59, 1ULL << 41), Bitboard(1ULL << 60, 1ULL << 42), Bitboard(1ULL << 61, 1ULL << 43), Bitboard(1ULL << 62, 1ULL << 44),
	Bitboard(0, 1ULL << 45), Bitboard(0, 1ULL << 46), Bitboard(0, 1ULL << 47), Bitboard(0, 1ULL << 48), Bitboard(0, 1ULL << 49), Bitboard(0, 1ULL << 50),
	Bitboard(0, 1ULL << 51), Bitboard(0, 1ULL << 52), Bitboard(0, 1ULL << 53), Bitboard(0, 1ULL << 54), Bitboard(0, 1ULL << 55), Bitboard(0, 1ULL << 56),
	Bitboard(0, 1ULL << 57), Bitboard(0, 1ULL << 58), Bitboard(0, 1ULL << 59), Bitboard(0, 1ULL << 60), Bitboard(0, 1ULL << 61), Bitboard(0, 1ULL << 62)
};

const Bitboard F1_B = Bitboard(0, 0);
const Bitboard F2_B = mask(RANK_1);
const Bitboard F3_B = F2_B | mask(RANK_2);
const Bitboard F4_B = F3_B | mask(RANK_3);
const Bitboard F5_B = F4_B | mask(RANK_4);
const Bitboard F6_B = F5_B | mask(RANK_5);
const Bitboard F7_B = F6_B | mask(RANK_6);
const Bitboard F8_B = F7_B | mask(RANK_7);
const Bitboard F9_B = F8_B | mask(RANK_8);
const Bitboard F9_W = Bitboard(0, 0);
const Bitboard F8_W = mask(RANK_9);
const Bitboard F7_W = F8_W | mask(RANK_8);
const Bitboard F6_W = F7_W | mask(RANK_7);
const Bitboard F5_W = F6_W | mask(RANK_6);
const Bitboard F4_W = F5_W | mask(RANK_5);
const Bitboard F3_W = F4_W | mask(RANK_4);
const Bitboard F2_W = F3_W | mask(RANK_3);
const Bitboard F1_W = F2_W | mask(RANK_2);

const Bitboard FRONT_MASK[TURN_MAX][RANK_MAX] =
{
	{ F1_B, F2_B, F3_B, F4_B, F5_B, F6_B, F7_B, F8_B, F9_B },
	{ F1_W, F2_W, F3_W, F4_W, F5_W, F6_W, F7_W, F8_W, F9_W },
};

#if defined USE_FILE_SQUARE_EVAL || defined CONVERT_EVAL
const EvalSquare SQ_TO_EVALSQ[SQ_MAX] =
{
	BSQ_91, BSQ_81, BSQ_71, BSQ_61, BSQ_51, BSQ_41, BSQ_31, BSQ_21, BSQ_11,
	BSQ_92, BSQ_82, BSQ_72, BSQ_62, BSQ_52, BSQ_42, BSQ_32, BSQ_22, BSQ_12,
	BSQ_93, BSQ_83, BSQ_73, BSQ_63, BSQ_53, BSQ_43, BSQ_33, BSQ_23, BSQ_13,
	BSQ_94, BSQ_84, BSQ_74, BSQ_64, BSQ_54, BSQ_44, BSQ_34, BSQ_24, BSQ_14,
	BSQ_95, BSQ_85, BSQ_75, BSQ_65, BSQ_55, BSQ_45, BSQ_35, BSQ_25, BSQ_15,
	BSQ_96, BSQ_86, BSQ_76, BSQ_66, BSQ_56, BSQ_46, BSQ_36, BSQ_26, BSQ_16,
	BSQ_97, BSQ_87, BSQ_77, BSQ_67, BSQ_57, BSQ_47, BSQ_37, BSQ_27, BSQ_17,
	BSQ_98, BSQ_88, BSQ_78, BSQ_68, BSQ_58, BSQ_48, BSQ_38, BSQ_28, BSQ_18,
	BSQ_99, BSQ_89, BSQ_79, BSQ_69, BSQ_59, BSQ_49, BSQ_39, BSQ_29, BSQ_19
};
#endif

#ifdef GENERATED_SFEN_BY_FILESQ
const Square NEXT_SQ[SQ_MAX] = 
{
	// SQ_11,がスタート
	SQ_92, SQ_82, SQ_72, SQ_62, SQ_52, SQ_42, SQ_32, SQ_22, SQ_12,
	SQ_93, SQ_83, SQ_73, SQ_63, SQ_53, SQ_43, SQ_33, SQ_23, SQ_13,
	SQ_94, SQ_84, SQ_74, SQ_64, SQ_54, SQ_44, SQ_34, SQ_24, SQ_14,
	SQ_95, SQ_85, SQ_75, SQ_65, SQ_55, SQ_45, SQ_35, SQ_25, SQ_15,
	SQ_96, SQ_86, SQ_76, SQ_66, SQ_56, SQ_46, SQ_36, SQ_26, SQ_16,
	SQ_97, SQ_87, SQ_77, SQ_67, SQ_57, SQ_47, SQ_37, SQ_27, SQ_17,
	SQ_98, SQ_88, SQ_78, SQ_68, SQ_58, SQ_48, SQ_38, SQ_28, SQ_18,
	SQ_99, SQ_89, SQ_79, SQ_69, SQ_59, SQ_49, SQ_39, SQ_29, SQ_19,
	SQ_MAX, SQ_91, SQ_81, SQ_71, SQ_61, SQ_51, SQ_41, SQ_31, SQ_21,
};

// EVAL_TO_SQの逆変換
const Square FILE_SQ[SQ_MAX] = 
{
	SQ_11, SQ_12, SQ_13, SQ_14, SQ_15, SQ_16, SQ_17, SQ_18, SQ_19,
	SQ_21, SQ_22, SQ_23, SQ_24, SQ_25, SQ_26, SQ_27, SQ_28, SQ_29,
	SQ_31, SQ_32, SQ_33, SQ_34, SQ_35, SQ_36, SQ_37, SQ_38, SQ_39,
	SQ_41, SQ_42, SQ_43, SQ_44, SQ_45, SQ_46, SQ_47, SQ_48, SQ_49,
	SQ_51, SQ_52, SQ_53, SQ_54, SQ_55, SQ_56, SQ_57, SQ_58, SQ_59,
	SQ_61, SQ_62, SQ_63, SQ_64, SQ_65, SQ_66, SQ_67, SQ_68, SQ_69,
	SQ_71, SQ_72, SQ_73, SQ_74, SQ_75, SQ_76, SQ_77, SQ_78, SQ_79,
	SQ_81, SQ_82, SQ_83, SQ_84, SQ_85, SQ_86, SQ_87, SQ_88, SQ_89,
	SQ_91, SQ_92, SQ_93, SQ_94, SQ_95, SQ_96, SQ_97, SQ_98, SQ_99,
};
#endif

// 人がわかりやすい形に変換
std::string pretty(const File f)
{
	static const char* str_file[] = { "９", "８", "７", "６", "５", "４", "３", "２", "１" };
	return std::string(str_file[f]);
}

std::string pretty(const Rank r)
{
	static const char* str_rank[] = { "一", "二", "三", "四", "五", "六", "七", "八", "九" };
	return std::string(str_rank[r]);
}

std::string pretty(const Square sq) { return pretty(fileOf(sq)) + pretty(rankOf(sq)); }
std::string toUSI(const File f) { const char c[] = { '9' - f, '\0' }; return std::string(c); }
std::string toUSI(const Rank r) { const char c[] = { 'a' + r, '\0' }; return std::string(c); }
std::string toUSI(const Square sq) { return toUSI(fileOf(sq)) + toUSI(rankOf(sq)); }
std::string toCSA(const File f) { const char c[] = { '9' - f, '\0' }; return std::string(c); }
std::string toCSA(const Rank r) { const char c[] = { '1' + r, '\0' }; return std::string(c); }
std::string toCSA(const Square sq) { return toCSA(fileOf(sq)) + toCSA(rankOf(sq)); }
std::string fromPretty(const Square sq) { return toUSI(fileOf(sq)) + toUSI(inverse(File(rankOf(sq)))); }
std::ostream& operator << (std::ostream& os, const File f) { os << toUSI(f); return os; }
std::ostream& operator << (std::ostream& os, const Rank r) { os << toUSI(r); return os; }
std::ostream& operator << (std::ostream& os, const Square sq) 
{ 
	Square d = sq;

	if (d >= SQ_MAX)
		d -= SQ_MAX;

	if (d == SQ_MAX)
		os << "Hand";
	else
		os << fileOf(d) << rankOf(d); 

	return os; 
}