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
#include <iostream>
#include <string>
#include "hand.h"
#include "types.h"
#include "common.h"
#include "evalsum.h"
#include "bitboard.h"
#include "evaluate.h"
#include "progress.h"
#include "byteboard.h"

struct Thread;
class Board;
enum Move : uint32_t;

namespace Zobrist 
{ 
    void init(); 
}

enum RepetitionType
{
    NO_REPETITION, REPETITION_DRAW, REPETITION_WIN, REPETITION_LOSE, REPETITION_SUPERIOR, REPETITION_INFERIOR
};

#ifdef USE_EVAL
// 評価値の差分計算の管理用
// 前の局面から移動した駒番号を管理するための構造体
struct DirtyPiece
{
    // dirtyになった個数。null moveだと0ということもありうる
    int dirty_num;

    // dirtyになった駒番号
    PieceNo piece_no[2];

    // その駒番号の駒が何から何に変わったのか。[0]が一枚目の駒、[1]が二枚目の駒（二枚目があるなら）
    Eval::ExtBonaPiece pre_piece[2];
    Eval::ExtBonaPiece now_piece[2];
};
#endif

// StateInfoは、undo_move()で局面を戻すときに情報を元の状態に戻すのが面倒なものを詰め込んでおくための構造体
struct StateInfo 
{
    // doMove時にコピーする

    // パスしてからの手数
    int plies_from_null;

    // 連続王手回数
    int continue_check[TURN_MAX];

    // 歩がいる列が1になっているフラグ（泥臭い）
    uint16_t nifu_flags[TURN_MAX];

    // ******ここから下はdoMove時にコピーしない******
    // この局面での評価関数の駒割
    Score material;

#ifdef USE_BITBOARD
    // 現局面で手番側に対して王手をしている駒のbitboard。doMove()で更新される
    Bitboard checkers;

    // Turnは玉の手番を表していて、自分ならpinされている駒、相手なら両王手候補。
    Bitboard blockers_for_king[TURN_MAX];

    // Turn玉をピンしている、Turn側ではない駒
    Bitboard pinners_for_king[TURN_MAX];

    // 自駒の駒種Xによって敵玉が王手となる升のbitboard
    Bitboard check_sq[PIECETYPE_MAX];
#endif

#ifdef USE_BYTEBOARD
    __m256i king_neighbor[TURN_MAX], king_ray[TURN_MAX];
    __m256i checker_knights, checker_no_knights;
    uint32_t slider_blockers[TURN_MAX];
    __m256i reach_sliders;
#endif
#ifdef USE_EVAL
    // 評価値。(次の局面で評価値を差分計算するときに用いる)
    Eval::EvalSum sum;

    // 評価値の差分計算の管理用
    DirtyPiece dirty_piece;

#ifdef USE_PROGRESS
    Progress::ProgressSum progress;
#endif
#endif
    // この局面における手番側の持ち駒。優等局面の判定のために必要
    Hand hand;

    Key board_key;
    Key hand_key;

    // 一つ前の局面に遡るためのポインタ
    // NULL MOVEなどでそこより遡って欲しくないときはnullptrを設定しておく
    StateInfo* previous;

    // この局面のハッシュキー。盤面のハッシュキー + 持ち駒のハッシュキー
    Key key() const { return board_key + hand_key; }

    // 盤面のハッシュキー
    Key boardKey() const { return board_key; }

    // 持ち駒のハッシュキー
    Key handKey() const { return hand_key; }
};

class Board
{
public:

    // c'tors
    Board() { clear(); }
    Board(const Board&) = delete;
    Board(const Board& b, Thread* th) { *this = b; this_thread_ = th; }
    Board(std::string sfen, Thread* th) { init(sfen, th); } 
    Board(Thread* th) : this_thread_(th) {}
    void init(std::string sfen, Thread* th);
    void init(std::string sfen) { init(sfen, thisThread()); }

    // スレッドのセット
    void setThread(Thread* th) { this_thread_ = th; }
    
    // スレッドのゲット
    Thread* thisThread() const { return this_thread_; }

    // ゼロクリア
    void clear();

    // sqにpieceの駒を置くときに必要な情報のセットを行う。
    void setPiece(const Piece piece, const Square sq, PieceNo piece_no);

    // validator
    bool verify() const;

    // sqにある駒を返す。
    Piece piece(const Square sq) const { return board_[sq]; }

    // 持ち駒を返す。
    Hand hand(const Turn t) const { return hand_[t]; }

    // 現局面の手番を返す。
    Turn turn() const { return turn_; }
    
    // 手数をセットする。
    void setPly(const int ply) { ply_ = ply; }

    // 玉の位置を返す。
    Square kingSquare(const Turn t) const { return king_[t]; }

    // 開始局面からの手数を返す。
    int ply() const { return ply_; }

    // 現局面のStateInfoを返す。
    StateInfo* state() const { return st_; }

    // 局面のハッシュ値、駒割をstに設定する
    void setState(StateInfo* st) const;

    // CheckInfoのセットと、敵味方に関係なくpinされている駒のbitboardをセットする。
    template <bool DoNullMove = false> void setCheckInfo(StateInfo* si) const;

#ifdef USE_BITBOARD
    // 手番tのptの駒をsqに置いたときのbitboardの更新を行う。
    void xorBBs(const PieceType pt, const Square sq, const Turn t);

    // 指定したPieceTypeのbitboardを返す
    Bitboard bbType(const PieceType pt) const { assert(pt < PIECETYPE_MAX); return bb_type_[pt];}
    Bitboard bbType(const PieceType pt1, const PieceType pt2) const { return bbType(pt1) | bbType(pt2); }
    Bitboard bbType(const PieceType pt1, const PieceType pt2, const PieceType pt3) const { return bbType(pt1, pt2) | bbType(pt3); }
    Bitboard bbType(const PieceType pt1, const PieceType pt2, const PieceType pt3, const PieceType pt4) const { return bbType(pt1, pt2, pt3) | bbType(pt4); }
    Bitboard bbType(const PieceType pt1, const PieceType pt2, const PieceType pt3, const PieceType pt4, const PieceType pt5) const { return bbType(pt1, pt2, pt3, pt4) | bbType(pt5); }

    // 指定したTurnのbitboardを返す
    Bitboard bbTurn(const Turn t) const { return bb_turn_[t]; }

    // 指定したPieceのbitboardを返す
    Bitboard bbPiece(const Piece p) const { return bbType(typeOf(p)) & bbTurn(turnOf(p)); }
    Bitboard bbPiece(const PieceType pt, const Turn t) const { return bbType(pt) & bbTurn(t); }
    Bitboard bbPiece(const PieceType pt1, const PieceType pt2, const Turn t) { return (bbType(pt1) | bbType(pt2)) & bbTurn(t); }

    // すべての駒のoccupied bitboardを返す
    Bitboard bbOccupied() const { return bb_type_[OCCUPIED]; }

    // 駒がない場所が1になっているビットボードを返す
    Bitboard bbEmpty() const { return bbOccupied() ^ allOneMask(); }

    // 金の動きをする駒のbitboard
    Bitboard bbGold() const { return bb_gold_; }
    Bitboard bbGold(const Turn t) const { return bbGold() & bbTurn(t); }

    // sqに動ける駒のBitboardを作る。occupiedにbbOccupiedを渡すバージョンも定義しておく
    Bitboard attackers(const Square sq, const Bitboard& occupied) const;
    Bitboard attackers(const Square sq) const { return attackers(sq, bbOccupied()); }

    // ExceptKingをtrueにすると玉の利きを除いたattackersになる
    template <bool ExceptKing = false> 
    Bitboard attackers(const Turn t, const Square sq, const Bitboard& occupied) const;
    template <bool ExceptKing = false> 
    Bitboard attackers(const Turn t, const Square sq) const { return attackers<ExceptKing>(t, sq, bbOccupied()); }

    // attackersの戻り値がboolになっているバージョン。ちょっとだけ早い（ことを目指している）
    bool existAttacker(const Turn t, const Square sq, const Bitboard& occ) const;

    // 敵玉と自分の駒の間にpinされている自分の駒を取得する。
    Bitboard discoveredCheckCandidates() const { return st_->blockers_for_king[~turn()] & bbTurn(turn()); }

    // t側の玉に対してpinされているt側の駒を返す。
    Bitboard pinnedPieces(Turn t) const { return st_->blockers_for_king[t] & bbTurn(t); }

    // ptを打つと王手になる場所が1のbitboardを返す。
    Bitboard checkSquares(const PieceType pt) const { return st_->check_sq[pt]; }

    // fromにいる駒がpinされていると仮定して、fromからtoに移動したときking_squareにいる玉に王手がかかるかどうか
    // king_squareは自玉、敵玉どちらでもよいので自殺手かどうか調べるときも使える
    bool isDiscoveredCheck(const Square from, const Square to, const Square king_square, const Bitboard& bb_discovered) const
    {
        return (bb_discovered & from) && !isAligned(from, to, king_square);
    }

    // ピンされている駒(return)、している駒(pinners)を返す。tはsniperのturn, sqはking_sq
    // BlockersOnlyはBlockersだけを求めたいときにtrueにする。
    template <bool BlockersOnly = false> Bitboard sliderBlockers(Turn t, Square s, Bitboard* pinners = nullptr) const;

    // 相手が自分に対して王手している駒のbitboardを返す
    Bitboard bbCheckers() const { return st_->checkers; }

    bool canPieceCapture(const Turn t, const Square sq, const Square king_square, const Bitboard& bb_discovered) const;
    template <Index TK> bool canKingEscape(const Square ksq, const Turn t, const Square, const Bitboard& bb) const;

    // 1手詰めルーチン本体。
    template <Turn T, Index TK> Move mate1ply(const Square ksq);
#endif
    // 局面を指し手moveで一手進める。
    void doMove(const Move move, StateInfo& st, bool gives_check);

    // gives_checkを内部で求めるバージョン。
    void doMove(const Move move, StateInfo& st);

    // 局面を戻す。
    void undoMove(const Move move);

    // NullMove(パス）をする。
    void doNullMove(StateInfo& st);

    // NullMoveした後、元に戻す。
    void undoNullMove();


    // 指し手生成を用いて詰んでいるかどうかをチェックする。
    bool isMate() const;

    // 千日手、連続王手の千日手、優等局面、劣等局面を分類する。
    RepetitionType repetitionType(int max_ply) const;

    // 1手詰めルーチンのラッパー。
    Move mate1ply1() const;
    Move mate1ply2() const;
    Move mate1ply() const;

    // 王手されている局面かどうかを返す。
    bool inCheck1() const;
    bool inCheck2() const;
    bool inCheck() const;

    // mが相手玉に対して王手になるかどうかを返す。
    bool givesCheck1(Move m) const;
    bool givesCheck2(Move m) const;
    bool givesCheck(Move m) const;

    // 静止探索
    bool seeGe1(const Move, const Score) const;
    bool seeGe2(const Move, const Score) const;
    bool seeGe(const Move m, const Score s) const;

    // 宣言勝ちかどうかを返す。
    bool isDeclareWin1() const;
    bool isDeclareWin2() const;
    bool isDeclareWin() const;
    
    // 打ち歩詰めかどうかを判定する。
    bool isPawnDropCheckMate1(const Turn t, const Square sq, const Square king_square) const;
    bool isPawnDropCheckMate2(const Turn t, const Square sq, const Square king_square) const;
    bool isPawnDropCheckMate(const Turn t, const Square sq, const Square king_square) const;

    // 任意のmoveに対して、PseudoLegalかどうかを判定する
    bool pseudoLegal1(const Move move) const;
    bool pseudoLegal2(const Move move) const;
    bool pseudoLegal(const Move move) const;

    // pseudoLegalな手が本当にlegalかどうかをチェックする。
    // pseudoLegalとは、玉が取られてしまうような着手を含めた合法手群のことである
    template <bool NotAlwaysDrop = false> bool legal1(const Move move) const;
    template <bool NotAlwaysDrop = false> bool legal2(const Move move) const;
    template <bool NotAlwaysDrop = false> bool legal(const Move move) const;

    // 1手詰め判定のヘルパー関数。
    bool canPieceCapture1(const Turn t, const Square sq, const Square ksq) const;
    bool canPieceCapture2(const Turn t, const Square sq, const Square ksq) const;
    bool canPieceCapture(const Turn t, const Square sq, const Square ksq) const;

    bool existAttacker1(const Turn t, const Square sq) const;
    bool existAttacker2(const Turn t, const Square sq) const;
    bool existAttacker(const Turn t, const Square sq) const;

#ifdef USE_BYTEBOARD
    template <Turn T> Move mate1ply2(const Square ksq) const;
    bool canPieceCapture2(const Turn t, const Square sq, const Square ksq, uint32_t slider_blockers) const;
    bool canPieceCapture2(const Turn t, const Square sq, const Square ksq, uint32_t slider_blockers, __m256i neighbor, __m256i ray) const;
    template <Turn T> bool canKingEscape2(const Square ksq, const Square sq) const;
    Square pieceSquare(int i) const { return pieceno_to_sq_[i]; }
    const Byteboard& getByteboard() const { return bb_; }
    uint64_t getExistsPiece(Turn t) const { return stm_piece_[t]; }
    template <Turn T, PieceType PT> bool mateCheck(Square to, Square ksq);
    template <Turn T, PieceType PT, bool Promote> bool mateCheck(Square from, Square to, Square ksq);
#endif
#ifdef USE_EVAL
    // 評価関数で使うための、どの駒番号の駒がどこにあるかなどの情報
    const Eval::EvalList* evalList() const { return &eval_list_; }

    // c側の手駒ptの最後の1枚のBonaPiece番号を返す
    Eval::BonaPiece bonaPieceOf(Turn c, PieceType pt) const 
    {
        // c側の手駒ptの枚数
        int ct = hand(c).count(pt);
        assert(ct > 0);
        return (Eval::BonaPiece)(Eval::BP_HAND_ID[c][pt].fb + ct - 1);
    }

    // c側の手駒ptの(最後に加えられた1枚の)PieceNoを返す
    PieceNo pieceNoOf(Turn c, PieceType pt) const { return eval_list_.pieceNoOf(bonaPieceOf(c, pt)); }

    // 盤上のpcの駒のPieceNoを返す
    PieceNo pieceNoOf(Piece pc, Square sq) const { return eval_list_.pieceNoOf((Eval::BonaPiece)(Eval::BP_BOARD_ID[pc].fb + sq)); }

#endif
    Key key() const { return st_->key(); }
    Key afterKey(const Move m) const;

    bool operator == (const Board& b) const;
    Board& operator = (const Board& b);
    std::string sfen() const;

#if defined LEARN
    void setFromPackedSfen(uint8_t data[32]);
    void sfenPack(uint8_t data[32]) const;
#endif
    // 画面出力用
    friend std::ostream& operator << (std::ostream& os, const Board& b);


    // sqの筋にt側の歩があるならtrueを返す。
    bool existPawnFile(Turn t, Square sq) const { return state()->nifu_flags[t] & (1 << fileOf(sq)); }
    bool existPawnFile(Turn t, File f) const { return state()->nifu_flags[t] & (1 << f); }
private:

#ifdef USE_BYTEBOARD
    // 先手後手両方が入ったbyteboard
    Byteboard bb_;

    // 駒位置のキャッシュ
    Square pieceno_to_sq_[PIECE_NO_NB];
    PieceNo sq_to_pieceno_[SQ_MAX];
    uint64_t stm_piece_[TURN_MAX];
#endif
#ifdef USE_BITBOARD
    // 手番側の、駒がいる場所が1になっているビットボード
    Bitboard bb_turn_[TURN_MAX];

    // 各駒の位置が1になっているビットボード。bb_type_[OCCUPIED]は
    // すべての駒種に対して、駒がある場所が1になっているビットボードを表す
    Bitboard bb_type_[PIECETYPE_MAX];
    Bitboard bb_gold_;
#endif
    // 将棋盤81マス
    Piece board_[SQ_MAX];

    // 持ち駒
    Hand hand_[TURN_MAX];

    // 手番
    Turn turn_;

    // 玉の位置
    Square king_[TURN_MAX];

    // 手数
    int ply_;

    // start_state_ はst_が初期状態で指しているStateInfo
    StateInfo start_state_, *st_;

    Thread* this_thread_;

#ifdef USE_EVAL
    // 評価関数で用いる駒のリスト
    Eval::EvalList eval_list_;
#endif 
};