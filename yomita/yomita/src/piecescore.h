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

#include "score.h"

#if defined USE_EVAL_TURN
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
#else
// 駒の価値
constexpr Score PAWN_SCORE = static_cast<Score>(86);
constexpr Score LANCE_SCORE = static_cast<Score>(227);
constexpr Score KNIGHT_SCORE = static_cast<Score>(256);
constexpr Score SILVER_SCORE = static_cast<Score>(365);
constexpr Score GOLD_SCORE = static_cast<Score>(439);
constexpr Score BISHOP_SCORE = static_cast<Score>(563);
constexpr Score ROOK_SCORE = static_cast<Score>(629);
constexpr Score PRO_PAWN_SCORE = static_cast<Score>(540);
constexpr Score PRO_LANCE_SCORE = static_cast<Score>(508);
constexpr Score PRO_KNIGHT_SCORE = static_cast<Score>(517);
constexpr Score PRO_SILVER_SCORE = static_cast<Score>(502);
constexpr Score HORSE_SCORE = static_cast<Score>(826);
constexpr Score DRAGON_SCORE = static_cast<Score>(942);
constexpr Score KING_SCORE = static_cast<Score>(15000);
#endif

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

