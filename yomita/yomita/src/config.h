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

#include "platform.h"

// 教師棋譜の生成時、棋譜からの学習時に定義（評価関数、進行度）
#define LEARN

// 評価関数。
// 使用する場合、以下から一つを定義する。
// 定義しなかった場合、駒得のみの評価関数になる。

// KPP + 手番型の評価関数
#define EVAL_KPPT

// PP + 手番型の評価関数
//#define EVAL_PPT

// PP + 手番 + 進行度ボーナス
//#define EVAL_PPTP

// 縦型Square用のevalファイルを使いたいときに定義する。
#if defined EVAL_KPPT
#define USE_FILE_SQUARE_EVAL
#endif

// 以下から一つ選択。
// 二つ選択するとbyteboardのテスト機能が有効になる。

// byteboardを使用するときに定義
//#define USE_BYTEBOARD

// bitboardを使用するときに定義
#define USE_BITBOARD

// 縦型Squareで作られたハフマン化sfenを読み込みたいときに定義する。
// 読み込み方が対応するだけで、生成には対応しない。
//#define GENERATED_SFEN_BY_FILESQ

// なんらかの評価関数バイナリを使う場合のdefine。
#if defined EVAL_KPPT || defined EVAL_PPT || defined EVAL_PPTP
#define USE_EVAL
#endif

// 進行度を使うときに定義する。
#if defined USE_EVAL
#define USE_PROGRESS
#endif

// PPTP型なら必ず進行度を使う。
#if !defined USE_PROGRESS && defined EVAL_PPTP
#define USE_PROGRESS
#endif

// 評価関数バイナリが入っているディレクトリと、学習時に生成したバイナリを保存するディレクトリ
#if defined EVAL_KPPT
#ifdef USE_FILE_SQUARE_EVAL
#define EVAL_TYPE "kppt_file"
#else
#define EVAL_TYPE "kppt"
#endif
#elif defined EVAL_PPT
#define EVAL_TYPE "ppt"
#elif defined EVAL_PPTP
#define EVAL_TYPE "pptp"
#else
#define EVAL_TYPE "komadoku_only"
#endif
