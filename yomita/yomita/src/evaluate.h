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

#if defined(__GNUC__)
#include <cstring>
#include <mm_malloc.h>
using namespace std ;
#endif

// 評価関数全般に関するヘッダファイル

#include "types.h"
#include "config.h"
#include "evalsum.h"

class Board;

std::string evalDir();
std::string evalSaveDir();

namespace Eval
{
    // 評価関数のインターフェース。手番側から見た点数を返す。
    // どの評価関数もこのインターフェースから呼び出せなければならない。
    Score evaluate(const Board& b);

#ifdef USE_EVAL
    // 評価関数ファイルを読み込む。
    void load();

    // 駒割り以外の全計算して、その合計を返す。Board::set()で一度だけ呼び出される。
    // あるいは差分計算が不可能なときに呼び出される。
    Score computeAll(const Board& b);

    // SquareとPieceに対する一意な番号
    enum BonaPiece : uint32_t
    {
        BONA_PIECE_ZERO = 0,
        f_hand_pawn = BONA_PIECE_ZERO + 1,
        e_hand_pawn = f_hand_pawn + 19,
        f_hand_lance = e_hand_pawn + 19,
        e_hand_lance = f_hand_lance + 5,
        f_hand_knight = e_hand_lance + 5,
        e_hand_knight = f_hand_knight + 5,
        f_hand_silver = e_hand_knight + 5,
        e_hand_silver = f_hand_silver + 5,
        f_hand_gold = e_hand_silver + 5,
        e_hand_gold = f_hand_gold + 5,
        f_hand_bishop = e_hand_gold + 5,
        e_hand_bishop = f_hand_bishop + 3,
        f_hand_rook = e_hand_bishop + 3,
        e_hand_rook = f_hand_rook + 3,
        fe_hand_end = e_hand_rook + 2,

        // --- 盤上の駒
        f_pawn = fe_hand_end,
        e_pawn = f_pawn + 81,
        f_lance = e_pawn + 81,
        e_lance = f_lance + 81,
        f_knight = e_lance + 81,
        e_knight = f_knight + 81,
        f_silver = e_knight + 81,
        e_silver = f_silver + 81,
        f_gold = e_silver + 81,
        e_gold = f_gold + 81,
        fe_gold_end = e_gold + 81,
        f_bishop = fe_gold_end,
        e_bishop = f_bishop + 81,
        f_horse = e_bishop + 81,
        e_horse = f_horse + 81,
        f_rook = e_horse + 81,
        e_rook = f_rook + 81,
        f_dragon = e_rook + 81,
        e_dragon = f_dragon + 81,
        fe_end = e_dragon + 81,

        // 王も一意な駒番号を付与。これは2駒関係をするときに王に一意な番号が必要なための拡張
        f_king = fe_end,
        e_king = f_king + SQ_MAX,
        fe_end2 = e_king + SQ_MAX,
    };

    ENABLE_OPERATORS_ON(BonaPiece);

    struct Evaluater
    {
#if defined EVAL_KPPT
        ValueKk kk_[SQ_MAX][SQ_MAX];
        ValueKpp kpp_[SQ_MAX][fe_end][fe_end];
        ValueKkp kkp_[SQ_MAX][SQ_MAX][fe_end];
#elif defined EVAL_PPT || defined EVAL_PPTP
        ValuePp pp_[fe_end2][fe_end2];
#endif
        // これがメソッドである理由は、評価関数テーブルを二つ確保してそれぞれ別々の評価関数を読み込んだりしたいから。
        void load(std::string dir);
        void save(std::string dir, bool save_in_eval_dir = false);
        void clear() { memset(this, 0, sizeof(Evaluater)); }
#ifdef USE_FILE_SQUARE_EVAL
        void saveNoFileConvert(std::string dir);
#endif
        void blend(float ratio1, Evaluater& e, float ratio2);

        static void* operator new (size_t s) { return Is64bit ? _mm_malloc(s, 32) : malloc(s); }
        static void operator delete (void* th) { Is64bit ? _mm_free(th) : free(th); }
    };

    // ポインタにしておくことで、読み込みを遅らせて起動を早くしたり、
    // 評価関数を二つ確保して手番ごとに評価関数を入れ替えるなどを容易にする。
    extern Evaluater* GlobalEvaluater;

    // BonaPieceを後手から見たとき(先手の39の歩を後手から見ると後手の71の歩)の番号とを
    // ペアにしたものをExtBonaPiece型と呼ぶことにする。
    struct ExtBonaPiece { BonaPiece fb, fw;	};

    inline bool isOK(const BonaPiece bp) { return bp > BONA_PIECE_ZERO && bp < fe_end2; }

    // BonaPieceの内容を表示する。手駒ならH,盤上の駒なら升目。例) HP3 (3枚目の手駒の歩)
    std::ostream& operator << (std::ostream& os, BonaPiece bp);

    // BonaPiece、fb側だけを表示する。
    inline std::ostream& operator << (std::ostream& os, ExtBonaPiece bp) { os << bp.fb; return os; }

    // KPPテーブルの盤上の駒pcに対応するBonaPieceを求めるための配列。
    // 例)
    // BonaPiece fb = BP_BOARD_ID[pc].fb + sq; // 先手から見たsqにあるpcに対応するBonaPiece
    // BonaPiece fw = BP_BOARD_ID[pc].fw + sq; // 後手から見たsqにあるpcに対応するBonaPiece
    extern ExtBonaPiece BP_BOARD_ID[PIECE_MAX];

    // KPPの手駒テーブル
    extern ExtBonaPiece BP_HAND_ID[TURN_MAX][KING];

    // 評価関数で用いる駒リスト。どの駒(PieceNo)がどこにあるのか(BonaPiece)を保持している構造体
    struct EvalList 
    {
        // 評価関数(FV38型)で用いる駒番号のリスト。駒番号に対するBonaPieceが入っている。
        // Fbは先手から見た位置、Fwは後手から見た位置。
        const BonaPiece* pieceListFb() const { return piece_list_fb_; }
        const BonaPiece* pieceListFw() const { return piece_list_fw_; }

        // 指定されたpiece_noの駒をExtBonaPiece型に変換して返す。
        ExtBonaPiece extBonaPiece(PieceNo piece_no) const
        {
            assert(isOK(piece_no));
            ExtBonaPiece bp;
            bp.fb = piece_list_fb_[piece_no];
            bp.fw = piece_list_fw_[piece_no];
            return bp;
        }

        // 盤上のsqの升にpiece_noのpcの駒を配置したときの駒番号に対するBonaPieceを設定する。
        void putPiece(PieceNo piece_no, Square sq, Piece pc) 
        {
            setPiece(piece_no, BonaPiece(BP_BOARD_ID[pc].fb + sq), BonaPiece(BP_BOARD_ID[pc].fw + inverse(sq)));
        }

        // c側の手駒ptのi+1枚目の駒のPieceNoを設定する。(1枚目の駒のPieceNoを設定したいならi==0にして呼び出すの意味)
        void putPiece(PieceNo piece_no, Turn c, PieceType pt, int i)
        {
            setPiece(piece_no, BonaPiece(BP_HAND_ID[c][pt].fb + i), BonaPiece(BP_HAND_ID[c][pt].fw + i));
        }

        // あるBonaPieceに対応するPieceNoを返す。
        PieceNo pieceNoOf(BonaPiece bp) const
        { 
            assert(isOK(bp));
            PieceNo ret = piece_no_list_[bp];
            assert(ret != (PieceNo)-1);
            return ret;
        }

        // pieceListを初期化
        void clear()
        {
            for (int i = 0; i < PIECE_NO_NB; i++)
                piece_list_fb_[i] = piece_list_fw_[i] = BONA_PIECE_ZERO;

            memset(piece_no_list_, -1, sizeof(piece_no_list_));
        }
    protected:

        // piece_noの駒のBonaPieceがfb,fwであることを設定する。
        void setPiece(PieceNo piece_no, BonaPiece fb, BonaPiece fw)
        {
            assert(isOK(piece_no));
            assert(isOK(fb));
            piece_list_fb_[piece_no] = fb;
            piece_list_fw_[piece_no] = fw;
            piece_no_list_[fb] = piece_no;
        }

        // 駒リスト。駒番号(PieceNo)いくつの駒がどこにあるのか(BonaPiece)を示す。FV38などで用いる。
        ALIGNAS(32) BonaPiece piece_list_fb_[PIECE_NO_NB];
        ALIGNAS(32) BonaPiece piece_list_fw_[PIECE_NO_NB];

        // あるBonaPieceに対して、その駒番号(PieceNo)を保持している配列
        PieceNo piece_no_list_[fe_end2];
    };

#endif // USE_EVAL

} // namespace Eval
