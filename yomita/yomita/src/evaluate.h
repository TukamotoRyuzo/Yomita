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

// 評価関数全般に関するヘッダファイル

#include "config.h"
#include "piecescore.h"
#include "square.h"
#include "piece.h"

#ifdef USE_EVAL
#include "evalsum.h"
#endif

class Board;

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
    Score computeEval(const Board& b);
#ifdef LEARN
    // 学習時の初期化
    void evalLearnInit();
    void initGrad();
    void addGrad(Board& b, Turn root_turn, double delta_grad);
    void updateWeights(uint64_t mini_batch_size, uint64_t epoch);
    void saveEval(std::string dir_name);
#endif

#if defined CONVERT_EVAL
    void convertEvalFileToRank();
    void convertEvalRankToFile();
#endif

    // SquareとPieceに対する一意な番号
    enum BonaPiece : uint32_t
    {
        // f = friend(≒先手)の意味。e = enemy(≒後手)の意味

        BONA_PIECE_ZERO = 0, // 無効な駒。駒落ちのときなどは、不要な駒をここに移動させる。

#if defined USE_EVAL_TURN
        // Apery(WCSC26)方式。0枚目の駒があるので少し隙間がある。
        // 定数自体は1枚目の駒のindexなので、KPPの時と同様の処理で問題ない。
        f_hand_pawn = BONA_PIECE_ZERO + 1,//0//0+1
        e_hand_pawn = f_hand_pawn + 19,//19+1
        f_hand_lance = e_hand_pawn + 19,//38+1
        e_hand_lance = f_hand_lance + 5,//43+1
        f_hand_knight = e_hand_lance + 5,//48+1
        e_hand_knight = f_hand_knight + 5,//53+1
        f_hand_silver = e_hand_knight + 5,//58+1
        e_hand_silver = f_hand_silver + 5,//63+1
        f_hand_gold = e_hand_silver + 5,//68+1
        e_hand_gold = f_hand_gold + 5,//73+1
        f_hand_bishop = e_hand_gold + 5,//78+1
        e_hand_bishop = f_hand_bishop + 3,//81+1
        f_hand_rook = e_hand_bishop + 3,//84+1
        e_hand_rook = f_hand_rook + 3,//87+1
        fe_hand_end = e_hand_rook + 2,//90
#elif defined USE_EVAL_NO_TURN
        // --- 手駒
        f_hand_pawn = BONA_PIECE_ZERO + 1,
        e_hand_pawn = f_hand_pawn + 18,
        f_hand_lance = e_hand_pawn + 18,
        e_hand_lance = f_hand_lance + 4,
        f_hand_knight = e_hand_lance + 4,
        e_hand_knight = f_hand_knight + 4,
        f_hand_silver = e_hand_knight + 4,
        e_hand_silver = f_hand_silver + 4,
        f_hand_gold = e_hand_silver + 4,
        e_hand_gold = f_hand_gold + 4,
        f_hand_bishop = e_hand_gold + 4,
        e_hand_bishop = f_hand_bishop + 2,
        f_hand_rook = e_hand_bishop + 2,
        e_hand_rook = f_hand_rook + 2,
        fe_hand_end = e_hand_rook + 2,
#endif

        // Bonanzaのように番号を詰めない。
        // 理由1) 学習のときに相対PPで1段目に香がいるときがあって、それが逆変換において正しく表示するのが難しい。
        // 理由2) 縦型BitboardだとSquareからの変換に困る。

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
        fe_end2 = e_king + SQ_MAX, // 玉も含めた末尾の番号。

        // 末尾は評価関数の性質によって異なるので、BONA_PIECE_NBを定義するわけにはいかない。
    };

    ENABLE_OPERATORS_ON(BonaPiece);

    inline bool isOK(const BonaPiece bp) { return bp > BONA_PIECE_ZERO && bp < fe_end2; }
    inline bool isOK2(const BonaPiece bp) { return bp > BONA_PIECE_ZERO && bp < fe_end; }
    inline bool isOK3(const BonaPiece bp) { return bp > BONA_PIECE_ZERO && bp < fe_gold_end; }

    // BonaPieceの内容を表示する。手駒ならH,盤上の駒なら升目。例) HP3 (3枚目の手駒の歩)
    std::ostream& operator << (std::ostream& os, BonaPiece bp);

    // BonaPieceを後手から見たとき(先手の39の歩を後手から見ると後手の71の歩)の番号とを
    // ペアにしたものをExtBonaPiece型と呼ぶことにする。
    struct ExtBonaPiece { BonaPiece fb, fw;	};

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
        BonaPiece* pieceListFb() const { return const_cast<BonaPiece*>(piece_list_fb_); }
        BonaPiece* pieceListFw() const { return const_cast<BonaPiece*>(piece_list_fw_); }

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
            assert(ret != -1);
            return ret;
        }

        // pieceListを初期化
        void clear()
        {
            for (int i = 0; i < PIECE_NO_NB; i++)
                piece_list_fb_[i] = piece_list_fw_[i] = BONA_PIECE_ZERO;

            memset(piece_no_list_, -1, sizeof(PieceNo) * (int)fe_end2);
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
        BonaPiece piece_list_fb_[PIECE_NO_NB];
        BonaPiece piece_list_fw_[PIECE_NO_NB];

        // あるBonaPieceに対して、その駒番号(PieceNo)を保持している配列
        PieceNo piece_no_list_[fe_end2];
    };

#endif // USE_EVAL

} // namespace Eval
