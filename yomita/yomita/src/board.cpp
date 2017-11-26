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

#include "config.h"

#include <sstream>
#include <algorithm>
#include <stddef.h>

#include "tt.h"
#include "move.h"
#include "board.h"
#include "thread.h"

namespace Zobrist
{
    const Key turn = 1;
    const Key zero = 0;
    Key psq[SQ_MAX][PIECE_MAX];
    Key hand[TURN_MAX][HAND_MAX];

    void init()
    {
        PRNG rng(20160503);

        for (Piece p = EMPTY; p < PIECE_MAX; ++p)
            for (auto sq : Squares)
                psq[sq][p] = rng.rand<Key>() & ~turn;

        for (auto t : Turns)
            for (Piece p = EMPTY; p < (Piece)HAND_MAX; ++p)
                hand[t][p] = rng.rand<Key>() & ~turn;
    }
}

// usi用。sfen形式の文字列から盤面を生成する。
void Board::init(std::string sfen, Thread* th)
{
    const std::string piece_to_char(".BRPLNSGK........brplnsgk");

    // sfen文字を入力ストリームに。
    std::istringstream ss(sfen);
    ss >> std::noskipws;

    bool promotion = false;
    Piece p;
    char token;

    // メンバをクリア
    clear();

    // PieceListを更新する上で、どの駒がどこにあるかを設定しなければならないが、
    // それぞれの駒をどこまで使ったかのカウンター
    PieceNo piece_no_count[KING] =
    {
        PIECE_NO_ZERO, PIECE_NO_BISHOP, PIECE_NO_ROOK, PIECE_NO_PAWN, PIECE_NO_LANCE,
        PIECE_NO_KNIGHT, PIECE_NO_SILVER, PIECE_NO_GOLD
    };

#ifdef USE_EVAL
    eval_list_.clear();
#endif

    // sfen文字から一文字読み込み、スペースにぶつかるまで。
    // 駒の配置。
    for (Square sq = SQ_ZERO; (ss >> token) && !isspace(token);)
    {
        if (isdigit(token)) // 数字ならEMPTYなので、数字分だけ読み飛ばす。
            sq += Square(token - '0');

        else if (token == '/') // '/'があると次の段。この場合は特に何かする必要はなし。
            ;

        else if (token == '+') // 成り
            promotion = true;

        else if ((p = Piece(piece_to_char.find(token))) != (Piece)std::string::npos)
        {
            PieceNo piece_no = (p == B_KING) ? PIECE_NO_BKING : // 先手玉
                (p == W_KING) ? PIECE_NO_WKING : // 後手玉
                piece_no_count[nativeType(p)]++; // それ以外
#ifdef USE_BYTEBOARD
            pieceno_to_sq_[piece_no] = sq;
            sq_to_pieceno_[sq] = piece_no;
#endif
            setPiece(promotion ? promotePiece(p) : p, sq, piece_no);
            promotion = false;
            ++sq;
        }
    }
#ifdef USE_BITBOARD
    bb_gold_ = bbType(GOLD, PRO_PAWN, PRO_LANCE, PRO_KNIGHT, PRO_SILVER);
#endif
    // ここで駒の配置はOK。次は手番
    ss >> token;
    assert(token == 'b' || token == 'w'); // ここでtokenはbかwなはず
    turn_ = token == 'b' ? BLACK : WHITE;
    ss >> token; // spaceを消費

    // 次は持ち駒
    for (int p_num = 0; (ss >> token) && !isspace(token);)
    {
        if (token == '-')
            memset(hand_, 0, sizeof(hand_));

        else if (isdigit(token))
            p_num = p_num * 10 + token - '0';

        else if ((p = Piece(piece_to_char.find(token))) != (Piece)std::string::npos)
        {
            p_num = std::max(1, p_num);
            hand_[turnOf(p)].set(nativeType(p), p_num);

            // FV38などではこの個数分だけpieceListに突っ込まないといけない。
            for (int i = 0; i < p_num; ++i)
            {
                PieceType rpc = nativeType(p);
                PieceNo piece_no = piece_no_count[rpc]++;
                assert(isOK(piece_no));
#ifdef USE_BYTEBOARD
                pieceno_to_sq_[piece_no] = SQ_MAX;
#endif
#ifdef USE_EVAL
                eval_list_.putPiece(piece_no, turnOf(p), rpc, i);
#endif
            }

            p_num = 0;
        }
    }

    // ハッシュ値と駒割の計算
    setState(st_);
    this_thread_ = th;
    assert(th != nullptr);
#ifdef USE_EVAL
    Eval::computeAll(*this);
#ifdef USE_PROGRESS
    Progress::computeAll(*this);
#endif
#endif
#ifdef USE_BYTEBOARD
#ifdef CALC_MOVE_DIFF
    __m256i tmp[2];

    for (int i = 0; i < 40; i++)
    {
        move_cache_[i] = _mm256_setzero_si256();
        Square sq = pieceSquare(i);

        if (sq != SQ_MAX)
        {
            Piece p = piece(sq);
            __m256i* rp = generateMoveFrom(bb_, sq, p, tmp);
            stm_piece_[turnOf(p)] |= 1ULL << i;
            assert(rp != tmp);
            move_cache_[i] = tmp[0];

            if (rp - tmp == 2)
            {
                assert(i >= PIECE_NO_BISHOP && i < PIECE_NO_KING);
                move_cache_[i + 6] = tmp[1];
            }
        }
    }

    dirty_flag_ = 0;
#else
    for (int i = 0; i < 40; i++)
    {
        Square sq = pieceSquare(i);

        if (sq != SQ_MAX)
            stm_piece_[turnOf(piece(sq))] |= 1ULL << i;
    }
#endif
#endif
    ss >> ply_;

    if (!verify())
        std::cout << "init error?" << std::endl;
}

// メンバをすべて0クリアする。
void Board::clear() { memset(this, 0, sizeof(Board)); st_ = &start_state_; }

void Board::setPiece(const Piece piece, const Square sq, PieceNo piece_no)
{
    board_[sq] = piece;
#ifdef USE_BYTEBOARD
    bb_.setPieceBit(sq, toPieceBit(piece));
#endif
    if (typeOf(piece) == PAWN)
        st_->nifu_flags[turnOf(piece)] ^= 1 << fileOf(sq);
#ifdef USE_BITBOARD
    if (piece != EMPTY)
    {
        bb_type_[typeOf(piece)] ^= sq;
        bb_turn_[turnOf(piece)] ^= sq;
        bb_type_[OCCUPIED] ^= sq;
    }
#endif
#ifdef USE_EVAL
    eval_list_.putPiece(piece_no, sq, piece);
#endif

    // 玉の位置の初期化
    if (typeOf(piece) == KING)
        king_[turnOf(piece)] = sq;
}

std::ostream& operator << (std::ostream &os, const Hand& h)
{
    os << "持ち駒:";

    if (!h)
        os << "なし";
    else
        for (auto pt : HandPiece)
            if (h.exists(pt))
                os << pretty(pt) << h.count(pt);
    return os;
}

std::ostream& operator << (std::ostream &os, const Board& b)
{
    os << "手数:" << b.ply_ << std::endl;

    // 持ち駒表示
    os << b.hand(WHITE) << std::endl;

    for (auto rank : Ranks)
    {
        for (auto file : Files)
        {
            Piece p = b.board_[sqOf(file, rank)];

            if (turnOf(p) == BLACK)
                os << " ";

            os << p;
        }

        os << std::endl;
    }

    os << b.hand(BLACK) << std::endl;
    os << (b.turn() == BLACK ? "先手番" : "後手番") << std::endl;
    os << "sfen = " << b.sfen() << "\nkey = " << b.key() << std::endl;

    return os;
}

// つまされているかどうかの判定。
bool Board::isMate() const
{
    return MoveList<LEGAL>(*this).size() == 0;
}

// 千日手、連続王手の千日手、優等局面、劣等局面を分類する。
RepetitionType Board::repetitionType(int check_max_ply) const
{
    const int start = 4;
    int i = start;

    const int ply = std::min(st_->plies_from_null, check_max_ply);

    // 4手かけないと千日手には絶対にならない。
    if (i <= ply)
    {
        // 2手前を見る。
        StateInfo* stp = st_->previous->previous;

        do {
            // さらに2手前を見る
            stp = stp->previous->previous;

            if (stp->key() == st_->key()) // 同一局面だった
            {
                assert(stp->boardKey() == st_->boardKey()
                    && stp->handKey() == st_->handKey());

                // 連続王手だったら、負け。2回しか繰り返してないけど負け。
                // どうせ正確な判定は将棋所がやってくれるので、読みの中で生じるrepetitionは2回目で判定する。
                if (i <= st_->continue_check[turn()])
                    return REPETITION_LOSE;

                // 連続王手をかけられてたら勝ち。
                if (i <= st_->continue_check[~turn()])
                    return REPETITION_WIN;

                // 王手じゃなかったら普通の引き分け
                return REPETITION_DRAW;
            }
            else if (stp->board_key == st_->board_key) // 盤面だけ同一局面だった
            {
                assert(hand(turn()) != stp->hand);

                if (st_->hand.isSuperior(stp->hand))
                    return REPETITION_SUPERIOR;

                if (stp->hand.isSuperior(st_->hand))
                    return REPETITION_INFERIOR;
            }
        } while ((i += 2) <= ply);
    }

    return NO_REPETITION;
}

#ifdef USE_BITBOARD
// 先手後手にかかわらずsqに移動可能な駒のoccupied bitboardを返す。
Bitboard Board::attackers(const Square sq, const Bitboard& occupied) const
{
    // ちょっとだけ高速化するアイデア
    // rookAttackを呼び出しているので、そのときに内部でattacks<FILE>を求めているのに、
    // その前にlanceAttackでattacks<FILE>を求めているのはもったいない。
    const Bitboard file_attack = attacks<DIRECT_FILE>(sq, occupied);
    const Bitboard b_lance_attack = file_attack & frontMask(BLACK, sq);
    const Bitboard w_lance_attack = file_attack & frontMask(WHITE, sq);
    const Bitboard rook_attack = file_attack | attacks<DIRECT_RANK>(sq, occupied);

    return (((pawnAttack(BLACK, sq) & bbType(PAWN))
        | (b_lance_attack & bbType(LANCE))
        | (knightAttack(BLACK, sq) & bbType(KNIGHT))
        | (silverAttack(BLACK, sq) & bbType(SILVER))
        | (goldAttack(BLACK, sq) & bbGold())
        ) & bbTurn(WHITE))
        | (((pawnAttack(WHITE, sq) & bbType(PAWN))
            | (w_lance_attack & bbType(LANCE))
            | (knightAttack(WHITE, sq) & bbType(KNIGHT))
            | (silverAttack(WHITE, sq) & bbType(SILVER))
            | (goldAttack(WHITE, sq) & bbGold())
            ) & bbTurn(BLACK))
        | (bishopAttack(sq, occupied) & bbType(BISHOP, HORSE))
        | (rook_attack & bbType(ROOK, DRAGON))
        | (kingAttack(sq) & bbType(KING, HORSE, DRAGON));
}

template Bitboard Board::attackers<true>(const Turn t, const Square sq, const Bitboard& occupied) const;
template Bitboard Board::attackers<false>(const Turn t, const Square sq, const Bitboard& occupied) const;

// sqに移動可能な手番側の駒のBitboardを返す。
template <bool ExceptKing> Bitboard Board::attackers(const Turn t, const Square sq, const Bitboard& occupied) const
{
    const Turn inv = ~t;
    const Bitboard file_attack = attacks<DIRECT_FILE>(sq, occupied);
    const Bitboard e_lance_attack = file_attack & frontMask(inv, sq);
    const Bitboard rook_attack = file_attack | attacks<DIRECT_RANK>(sq, occupied);
    const Bitboard skd = ExceptKing ? bbType(SILVER, DRAGON) : bbType(SILVER, KING, DRAGON);
    const Bitboard kh = ExceptKing ? bbType(HORSE) : bbType(KING, HORSE);

    return ((pawnAttack(inv, sq) & bbType(PAWN))
        | (e_lance_attack & bbType(LANCE))
        | (knightAttack(inv, sq) & bbType(KNIGHT))
        | (silverAttack(inv, sq) & skd)
        | (goldAttack(inv, sq) & (kh | bbGold()))
        | (bishopAttack(sq, occupied) & (bbType(BISHOP, HORSE)))
        | (rook_attack & (bbType(ROOK, DRAGON)))
        ) & bbTurn(t);
}

// t側の駒が利いているかどうかをboolで返す。
bool Board::existAttacker(const Turn t, const Square sq, const Bitboard& occupied) const
{
    const Turn inv = ~t;
    const Bitboard file_attack = attacks<DIRECT_FILE>(sq, occupied);
    const Bitboard e_lance_attack = file_attack & frontMask(inv, sq);
    const Bitboard rook_attack = file_attack | attacks<DIRECT_RANK>(sq, occupied);
    const Bitboard skd = bbType(SILVER, KING, DRAGON);
    const Bitboard kh = bbType(KING, HORSE);

    return ((pawnAttack(inv, sq) & bbType(PAWN))
        | (e_lance_attack & bbType(LANCE))
        | (knightAttack(inv, sq) & bbType(KNIGHT))
        | (silverAttack(inv, sq) & skd)
        | (goldAttack(inv, sq) & (kh | bbGold()))
        | (bishopAttack(sq, occupied) & (bbType(BISHOP, HORSE)))
        | (rook_attack & (bbType(ROOK, DRAGON)))
        ) & bbTurn(t);
}

bool Board::existAttacker1(const Turn t, const Square sq) const { return existAttacker(t, sq, bbOccupied()); }

bool Board::inCheck1() const { return bbCheckers(); }

void Board::xorBBs(const PieceType pt, const Square sq, const Turn t)
{
    bb_type_[OCCUPIED] ^= sq;
    bb_type_[pt] ^= sq;
    bb_turn_[t] ^= sq;
#ifdef USE_BYTEBOARD
    bb_.setPieceBit(sq, bb_.pieceBit(sq) ^ toPieceBit(pt | t));
#endif
}

template <bool NotAlwaysDrop>
bool Board::legal1(const Move move) const
{
    if (!NotAlwaysDrop && isDrop(move))
    {
        const Square dir = turn() == BLACK ? SQ_U : SQ_D;
        const Square king_square = kingSquare(~turn());

        // 打ち歩詰めの判定
        if (movedPieceType(move) == PAWN
            && toSq(move) + dir == king_square
            && isPawnDropCheckMate(turn(), toSq(move), king_square))
            return false;

        // 他の駒打ちはpseudoLegalで正しいことを確認済み
        return true;
    }

    assert(!isDrop(move));

    const Turn self = turn();
    const Square from = fromSq(move);

    // 以前非常に悩まされていたassert。今は引っかからなくなった。
    assert(capturePieceType(move) != KING);
    assert(piece(from) == movedPiece(move));

    // 玉の移動先に相手の駒の利きがあれば、合法手ではないのでfalse
    if (typeOf(piece(from)) == KING)
        return !(existAttacker(~self, toSq(move), bbOccupied() & ~mask(from)));

    // 玉以外の駒の移動 fromにある駒がtoに移動したとき自分の玉に王手がかかるかどうか。
    return !(isDiscoveredCheck(from, toSq(move), kingSquare(self), st_->blockers_for_king[self]));
}

template bool Board::legal1<false>(const Move move) const;
template bool Board::legal1<true>(const Move move) const;

// 任意のmoveに対して、Legalかどうかを判定する。
// moveが32bitなので、比較的単純なチェックですんでいる。
bool Board::pseudoLegal1(const Move move) const
{
    const Turn self = turn();
    const Turn enemy = ~self;
    const Square to = toSq(move);

    // 置換表が手番ビットを考慮しているので、たとえ競合やハッシュミスが起きてもこの条件に引っかかることはあり得ない。
    assert(turnOf(move) == self);

    if (isDrop(move))
    {
        const PieceType hp_from = movedPieceType(move);
        assert(hp_from >= BISHOP && hp_from < KING);

        // 打つ先のマスが空ではない、打つ駒を持っていない
        if (!hand(self).exists(hp_from) || piece(to) != EMPTY)
            return false;

        Bitboard checkers = bbCheckers();

        // ※レアケース
        if (checkers)
        {
            // 王手されている && 駒打ち　== 合駒のはず。
            const Square check_square = checkers.firstOne();

            // 王手のうちひとつを抽出したが、まだ王手が残っている == 両王手
            // 両王手に対して持ち駒を打つ手を生成することはできない。
            if (checkers)
                return false;

            // 玉と、王手した駒との間に駒を打っていない。
            if (!(mask(to) & bbBetween(check_square, kingSquare(self))))
                return false;
        }

        // 二歩
        if (hp_from == PAWN && (bbPiece(PAWN, self) & fileMask(to)))
            return false;
    }

    // 駒を動かす手
    else
    {
        // 取った駒がtoにある駒である
        // ※レアケース。ハッシュミスが起こるとこの条件に引っかかることもありえる。
        // 置換表のサイズをケチると結構簡単に起こりえる。
        if (capturePiece(move) != piece(to))
            return false;

        // 移動先に自分の駒がある
        // ※これも↑と同様の理由から必要。
        if (bbTurn(self) & to)
            return false;

        const Square from = fromSq(move);
        const Piece pc_from = movedPiece(move);

        // 成れない駒を成る手は生成しないので、引っかからないはず。
        assert(!isPromote(move) || typeOf(pc_from) < GOLD);

        // fromにある駒がpt_fromではない
        if (piece(from) != pc_from)
            return false;

        // fromの駒が飛び駒で、かつtoに利いていない。飛び駒でなければ必ずtoに利いている。
        if (isSlider(pc_from) && attackAll(pc_from, from, bbOccupied()).andIsFalse(mask(to)))
            return false;

        Bitboard checkers = bbCheckers();

        if (checkers)
        {
            if (typeOf(pc_from) != KING)// 王以外の駒を移動させたとき
            {
                Bitboard target = checkers;
                const Square check_square = target.firstOne();

                // 両王手なので玉が動かなければならない。
                if (target)
                    return false;

                target = bbBetween(check_square, kingSquare(self)) | checkers;

                // 移動合いもしくは王手駒を取る手以外はだめ。
                if (!(target & to))
                    return false;
            }
        }
    }

    return true;
}

bool Board::givesCheck1(Move m) const
{
    const Square to = toSq(m);

    if (isDrop(m))
    {
        // 駒打ちなら打った駒が王手になるかを判定してそのまま帰る。
        return st_->check_sq[movedPieceType(m)] & to;
    }
    else
    {
        const Square from = fromSq(m);
        const PieceType pt = movedPieceTypeTo(m);

        // 直接王手
        if (st_->check_sq[pt] & to)
            return true;

        // fromにある駒を動かすと相手玉に対して王手になる
        if ((st_->blockers_for_king[~turn()] & from)
            && !isAligned(kingSquare(~turn()), from, to))
            return true;
    }

    return false;
}

// 入玉勝ちかどうかを判定
bool Board::isDeclareWin1() const
{
    // 条件
    // 宣言する者の玉が入玉している。
    //「宣言する者の敵陣にいる駒」と「宣言する者の持ち駒」を対象に前述の点数計算を行ったとき、宣言する者が先手の場合28点以上、後手の場合27点以上ある。
    // 宣言する者の敵陣にいる駒は、玉を除いて10枚以上である。
    // 宣言する者の玉に王手がかかっていない。
    // 切れ負け将棋の場合、宣言する者の持ち時間が切れていない。
    // cf. https://ja.wikipedia.org/wiki/%E5%85%A5%E7%8E%89

    // 宣言する者の玉に王手がかかっていない。
    // この条件が一番手軽に判定できるので最初にやる。
    if (inCheck())
        return false;

    // 宣言する者の玉が入玉している。
    const Turn us = turn();
    const Index T_HIGH = us == BLACK ? HIGH : LOW;
    const Bitboard enemy_field = enemyMask(us);

    if (!(bbPiece(KING, us).b(T_HIGH) & enemy_field.b(T_HIGH)))
        return false;

    // 宣言する者の敵陣にいる駒は、玉を除いて10枚以上である。
    if (popCount(bbTurn(us).b(T_HIGH) & enemy_field.b(T_HIGH)) < 11)
        return false;

    //「宣言する者の敵陣にいる駒」と「宣言する者の持ち駒」を対象に前述の点数計算を行ったとき、宣言する者が先手の場合28点以上、後手の場合27点以上ある。
    const uint64_t big = bbType(ROOK, DRAGON, BISHOP, HORSE).b(T_HIGH) & enemy_field.b(T_HIGH) & bbTurn(us).b(T_HIGH);
    const uint64_t small = (bbType(PAWN, LANCE, KNIGHT, SILVER).b(T_HIGH) | bbGold().b(T_HIGH)) & enemy_field.b(T_HIGH) & bbTurn(us).b(T_HIGH);
    const Hand h = hand(us);
    const int score = (popCount(big) + h.count(ROOK) + h.count(BISHOP)) * 5
        + popCount(small)
        + h.count(PAWN) + h.count(LANCE) + h.count(KNIGHT) + h.count(SILVER) + h.count(GOLD);

    if (score < (us == BLACK ? 28 : 27))
        return false;

    return true;
}

namespace
{

    // toにある駒と盤面を渡すと、次にどの駒でtoにある駒を取るのかを返す。
    // 駒を見つけたら、occupied bitboardのその駒の位置を0にする。
    // attackersはその駒がいなくなることによってtoに利く駒の位置を1にする。
    PieceType nextAttacker(const Board& b, const Square to, const Bitboard& stm_attackers,
        Bitboard& occupied, Bitboard& attackers, const Turn turn, Square& from, bool& promote)
    {
        Bitboard bb;
        PieceType pt;

        if ((bb = (stm_attackers & b.bbType(PAWN))) != 0) pt = PAWN;
        else if ((bb = (stm_attackers & b.bbType(LANCE))) != 0) pt = LANCE;
        else if ((bb = (stm_attackers & b.bbType(KNIGHT))) != 0) pt = KNIGHT;
        else if ((bb = (stm_attackers & b.bbType(PRO_PAWN))) != 0) pt = PRO_PAWN;
        else if ((bb = (stm_attackers & b.bbType(PRO_LANCE))) != 0) pt = PRO_LANCE;
        else if ((bb = (stm_attackers & b.bbType(PRO_KNIGHT))) != 0) pt = PRO_KNIGHT;
        else if ((bb = (stm_attackers & b.bbType(SILVER))) != 0) pt = SILVER;
        else if ((bb = (stm_attackers & b.bbType(PRO_SILVER))) != 0) pt = PRO_SILVER;
        else if ((bb = (stm_attackers & b.bbType(GOLD))) != 0) pt = GOLD;
        else if ((bb = (stm_attackers & b.bbType(BISHOP))) != 0) pt = BISHOP;
        else if ((bb = (stm_attackers & b.bbType(HORSE))) != 0) pt = HORSE;
        else if ((bb = (stm_attackers & b.bbType(ROOK))) != 0) pt = ROOK;
        else if ((bb = (stm_attackers & b.bbType(DRAGON))) != 0) pt = DRAGON;
        else
        {
            promote = false;
            return KING;
        }

        // その駒がいなくなることによって通る利きをattackersにセットする。
        from = bb.firstOne<false>();
        occupied ^= from;

        // 実際に移動した方向を基にattackersを更新
        switch (relation(from, to))
        {
        case DIRECT_DIAG1: case DIRECT_DIAG2: attackers |= (bishopAttack(to, occupied) & b.bbType(BISHOP, HORSE)); break;
        case DIRECT_RANK: attackers |= attacks<DIRECT_RANK>(to, occupied) & b.bbType(ROOK, DRAGON); break;
        case DIRECT_FILE:
        {
            const Bitboard file_attack = attacks<DIRECT_FILE>(to, occupied);
            const Bitboard b_lances = file_attack & frontMask(WHITE, to) & b.bbPiece(LANCE, BLACK);
            const Bitboard w_lances = file_attack & frontMask(BLACK, to) & b.bbPiece(LANCE, WHITE);
            const Bitboard rooks = file_attack & b.bbType(ROOK, DRAGON);
            attackers |= b_lances | w_lances | rooks;
            break;
        }
        case DIRECT_MISC: assert(!(bishopAttackToEdge(from) & to) && !(rookAttackToEdge(from) & to)); break;
        default: UNREACHABLE;
        }

        promote = (canPromote(turn, to) || canPromote(turn, from)) && !isNoPromotable(pt);
        return pt;
    }
}

// mのSEE >= s を返す。s以上であることがわかった時点でこの関数を抜けるので、通常のSEEよりも高速。
bool Board::seeGe1(const Move m, const Score s) const
{
    assert(isOK(m));
    Score balance = captureScore(capturePieceType(m));

    if (isPromote(m))
        balance += promoteScore(movedPieceType(m));

    // 今取った駒がただ取りだとしても閾値を超えないのならこの後何を指しても無駄。
    if (balance < s)
        return false;

    // この指し手で移動した駒が、次に相手によって取られる駒である。
    PieceType next_victim = movedPieceTypeTo(m);

    // この指し手が玉を動かす手で合法なら、取り返されることはない。
    if (next_victim == KING)
        return true;

    // next_victimを取り返す側の手番が相手ならtrue, 自分ならfalse。
    // trueであればbalance >= s、falseであればbalance < sでなければならない。
    bool relative_turn = true, promote;
    Turn stm = ~turn();
    Square to = toSq(m), from = isDrop(m) ? SQ_MAX : fromSq(m);
    Bitboard occ = bbOccupied() ^ from;
    Bitboard all_attackers = attackers(to, occ);

    // 利用可能な敵の駒は、ピンされていない駒か、toと敵玉のライン上に存在する駒である。
    Bitboard available[TURN_MAX] = {
        ~state()->blockers_for_king[BLACK] | bbLine(to, kingSquare(BLACK)),
        ~state()->blockers_for_king[WHITE] | bbLine(to, kingSquare(WHITE))
    };

    while (true)
    {
        // fromに居る駒が敵玉をpinしているなら、fromと敵玉の間にいる駒は利用可能。
        if (state()->pinners_for_king[stm] & from)
            available[stm] |= bbLine(from, kingSquare(stm));

        // 手番側のattackers
        Bitboard stm_attackers = all_attackers & bbTurn(stm) & available[stm];

        // 駒の利きがないなら、そこで取り合いは終わり。このときは取り返せなかったときの
        // balance >= sの論理を返せば良い。
        if (!stm_attackers)
            return relative_turn;

        // 次に相手がどの駒で取るのか。
        Score c = captureScore(next_victim);
        next_victim = nextAttacker(*this, to, stm_attackers, occ, all_attackers, stm, from, promote);
        all_attackers &= occ;
        stm = ~stm;

        if (promote)
        {
            c += promoteScore(nativeType(next_victim));
            next_victim = promotePieceType(next_victim);
        }

        balance += relative_turn ? -c : c;

        // 今victimを取り返したのに、（手番側から見て）balanceが閾値を超えない。
        if (relative_turn == (balance >= s))
            return relative_turn;

        // 玉で取る手が発生したら、それが合法であれば取り返せるし、合法でなければ取り返せない。
        if (next_victim == KING)
            return relative_turn == bool(all_attackers & bbTurn(stm));

        relative_turn = !relative_turn;
    };
}
// ピンされている駒(return)、している駒(pinners)を返す。tはsniperのturn, sqはking_sq
// BlockersOnlyはBlockersだけを求めたいときにtrueにする。
template <bool BlockersOnly>
Bitboard Board::sliderBlockers(Turn t, Square sq, Bitboard* pinners) const
{
    Bitboard result = allZeroMask();

    if (!BlockersOnly)
        *pinners = result;

    // 間接的にsqに聞いている駒
    Bitboard snipers = ((bbType(ROOK, DRAGON) & rookAttackToEdge(sq))
        | (bbType(BISHOP, HORSE) & bishopAttackToEdge(sq))
        | (bbType(LANCE) & lanceAttackToEdge(~t, sq))) & bbTurn(t);

    while (snipers)
    {
        Square snipers_sq = snipers.firstOne();
        Bitboard b = bbBetween(sq, snipers_sq) & bbOccupied();

        // snipersとsqの間にある駒の数が一個
        if (b.count() == 1)
        {
            result |= b;

            if (!BlockersOnly && (b & bbTurn(~t)))
                *pinners |= snipers_sq;
        }
    }

    return result;
}

// BlockersOnlyはBlockersだけを求めたいときにtrueにする。
template Bitboard Board::sliderBlockers<true>(Turn t, Square sq, Bitboard* pinners) const;
template Bitboard Board::sliderBlockers<false>(Turn t, Square sq, Bitboard* pinners) const;
#endif

bool Board::operator == (const Board & b) const
{
    for (auto t : Turns)
    {
#ifdef USE_BITBOARD
        if (bbTurn(t) != b.bbTurn(t))
            return false;
#endif
        if (kingSquare(t) != b.kingSquare(t))
            return false;

        if (hand(t) != b.hand(t))
            return false;
    }
#ifdef USE_BITBOARD
    for (int i = 0; i < PIECETYPE_MAX; i++)
        if (bbType(PieceType(i)) != b.bbType(PieceType(i)))
            return false;
#endif
    for (auto sq : Squares)
        if (piece(sq) != b.piece(sq))
            return false;

    if (state()->boardKey() != b.state()->boardKey() || state()->material != b.state()->material)
        return false;

    return true;
}

Board& Board::operator = (const Board& b)
{
    std::memcpy(this, &b, sizeof(Board));
    std::memcpy(&start_state_, st_, sizeof(StateInfo));
    st_ = &start_state_;
    assert(b.verify());
    return *this;
}

// sfen文字列を出力する。
std::string Board::sfen() const
{
    const std::string piece_to_char(".BRPLNSGK........brplnsgk");
    std::ostringstream ss;
    int empty_cnt;

    // 盤面を調べる
    for (Rank r : Ranks)
    {
        for (File f = FILE_9; f <= FILE_1; ++f)
        {
            for (empty_cnt = 0; f <= FILE_1 && piece(sqOf(f, r)) == EMPTY; ++f)
                ++empty_cnt;

            if (empty_cnt)
                ss << empty_cnt;

            if (f <= FILE_1)
                ss << toUSI(piece(sqOf(f, r)));
        }

        if (r < RANK_9)
            ss << '/';
    }

    ss << (turn() == WHITE ? " w " : " b ");

    // 持ち駒
    bool found = false;

    for (Turn t : Turns)
        for (PieceType p : HandPiece)
        {
            int n = hand(t).count(p);

            if (n != 0)
            {
                found = true;

                if (n != 1)
                    ss << n;

                ss << piece_to_char[p | t];
            }
        }

    // 持ち駒がないなら-
    ss << (found ? " " : "- ");
    ss << ply_;

    return ss.str();
}

// 局面のハッシュ値、駒割をstに設定する
void Board::setState(StateInfo* st) const
{
#ifdef USE_BITBOARD
    // この局面で自玉に王手している敵の駒
    st->checkers = attackers<true>(~turn(), kingSquare(turn()));
#endif
    st->hand = hand(turn());
    st->material = SCORE_ZERO;
    st->board_key = turn() == BLACK ? Zobrist::zero : Zobrist::turn;
    st->hand_key = Zobrist::zero;
    setCheckInfo(st);

    for (auto sq : Squares)
    {
        if (piece(sq))
        {
            assert(isOK(sq));
            assert(piece(sq) > EMPTY && piece(sq) <= W_PRO_SILVER);
            st->material += pieceScore(piece(sq));
            st->board_key += Zobrist::psq[sq][piece(sq)];
        }
    }

    for (auto t : Turns)
        for (auto hp : HandPiece)
        {
            int count = hand(t).count(hp);
            st->material += (t == BLACK ? 1 : -1) * Score(count * pieceScore(hp));
            st->hand_key += Zobrist::hand[t][hp] * count;
        }
}

void Board::doMove(const Move move, StateInfo& new_st)
{
    doMove(move, new_st, givesCheck(move));
}

Key Board::afterKey(const Move m) const
{
    const Turn self = turn();
    const Turn enemy = ~self;
    const Square to = toSq(m);
    const Piece pc = movedPiece(m);

    assert(turnOf(m) == self);
    assert(!isCapture(m) || turnOf(capturePiece(m)) == enemy);

    Key k = st_->board_key ^ Zobrist::turn;
    Key h = st_->hand_key;

    if (isDrop(m)) // 持ち駒を打つ手
    {
        // keyの更新
        h -= Zobrist::hand[self][typeOf(pc)];
        k += Zobrist::psq[to][pc];
    }
    else // 盤上の指し手
    {
        assert(!isCapture(m) || board_[to]);

        k -= Zobrist::psq[fromSq(m)][pc];
        k += Zobrist::psq[to][movedPieceTo(m)];

        // 駒を取ったとき
        if (isCapture(m))
        {
            const Piece pc_cap = capturePiece(m);
            assert(typeOf(pc_cap) != KING);

            // 取られた駒が盤上から消える
            k -= Zobrist::psq[to][pc_cap];
            h += Zobrist::hand[self][nativeType(pc_cap)];
        }
    }

    return k + h;
}

// 指し手で局面を進める
void Board::doMove(const Move move, StateInfo& new_st, bool gives_check)
{
    assert(verify());
    assert(move != MOVE_NONE);

    // 現局面のhash_key
    Key k = st_->board_key ^ Zobrist::turn;
    Key h = st_->hand_key;
    Score material = st_->material;

    std::memcpy(&new_st, st_, offsetof(struct StateInfo, material));
    new_st.previous = st_;
    st_ = &new_st;

    const Turn self = turn();
    const Turn enemy = ~self;
    const Square to = toSq(move);
    const Piece pc = movedPiece(move);

    assert(self == turnOf(pc));

#ifdef USE_EVAL
    // 評価値の計算をまだ済ませていないフラグをセット。
    st_->sum.setNotEvaluated();
    auto& dp = st_->dirty_piece;
#ifdef USE_PROGRESS
    st_->progress.setNoProgress();
#endif
#endif
    if (isDrop(move)) // 持ち駒を打つ手
    {
        const PieceType pt_to = typeOf(pc);
#ifdef USE_EVAL
        // 評価関数の差分関係
        PieceNo piece_no = pieceNoOf(self, pt_to);
        dp.dirty_num = 1;
        dp.piece_no[0] = piece_no;
        dp.pre_piece[0] = eval_list_.extBonaPiece(piece_no);
        eval_list_.putPiece(piece_no, to, pc);
        dp.now_piece[0] = eval_list_.extBonaPiece(piece_no);
#ifdef USE_BYTEBOARD
        pieceno_to_sq_[piece_no] = to;
        sq_to_pieceno_[to] = piece_no;
        stm_piece_[turnOf(move)] |= 1ULL << piece_no;
#endif
#endif
        // 持ち駒を減らす。
        hand_[self].sub(pt_to);
#ifdef USE_BITBOARD
        // 打ったことで変わるBitboardの設定
        bb_type_[OCCUPIED] ^= to;
        bb_type_[pt_to] ^= to;
        bb_turn_[self] ^= to;
#endif
        // 盤面に駒を置く。
        board_[to] = pc;
#ifdef USE_BYTEBOARD
        bb_.setPieceBit(to, toPieceBit(pc));
#endif
        if (pt_to == PAWN)
            st_->nifu_flags[self] ^= 1 << fileOf(to);

        // keyの更新
        h -= Zobrist::hand[self][pt_to];
        k += Zobrist::psq[to][pc];

        if (gives_check) // 王手している駒のbitboardを更新する
        {
#ifdef USE_BITBOARD
            st_->checkers = mask(to);
#endif
            st_->continue_check[self] += 2;
        }
        else
        {
#ifdef USE_BITBOARD
            st_->checkers = allZeroMask();
#endif
            st_->continue_check[self] = 0;
        }
#ifdef USE_BYTEBOARD
#ifdef CALC_MOVE_DIFF
        calcMoveDiff<true, false>(SQ_MAX, to);
#endif
#endif
    }
    else // 盤上の指し手
    {
        assert(!isCapture(move) || board_[to]);
        const Square from = fromSq(move);
        const Piece pc_to = movedPieceTo(move);
        const PieceType pt_from = typeOf(pc);
        const PieceType pt_to = typeOf(pc_to);
#ifdef USE_BITBOARD
        bb_type_[pt_from] ^= from;
        bb_type_[pt_to] ^= to;
        bb_turn_[self] ^= mask(from) ^ mask(to);
#endif
        board_[from] = EMPTY;
        board_[to] = pc_to;
#ifdef USE_BYTEBOARD
        bb_.setPieceBit(from, PB_EMPTY);
        bb_.setPieceBit(to, toPieceBit(pc_to));
#endif
        k -= Zobrist::psq[from][pc];
        k += Zobrist::psq[to][pc_to];

        if (isPromote(move))
        {
            material += promoteScore(pc);

            if (pt_to == PRO_PAWN)
                st_->nifu_flags[self] ^= 1 << fileOf(to);
        }

        // 駒を取ったとき
        if (isCapture(move))
        {
            const Piece pc_cap = capturePiece(move);
            const PieceType npt_cap = nativeType(pc_cap);
#ifdef USE_EVAL
            // 評価関数の差分関係
            PieceNo piece_no = pieceNoOf(pc_cap, to);
            dp.dirty_num = 2; // 動いた駒は2個
            dp.piece_no[1] = piece_no;
            dp.pre_piece[1] = eval_list_.extBonaPiece(piece_no);
            eval_list_.putPiece(piece_no, self, npt_cap, hand(self).count(npt_cap));
            dp.now_piece[1] = eval_list_.extBonaPiece(piece_no);
#ifdef USE_BYTEBOARD
            pieceno_to_sq_[piece_no] = SQ_MAX;
            stm_piece_[turnOf(pc_cap)] ^= 1ULL << piece_no;
#endif
#endif
#ifdef USE_BITBOARD
            bb_type_[typeOf(pc_cap)] ^= to;
            bb_turn_[enemy] ^= to;
#endif
            if (typeOf(pc_cap) == PAWN)
                st_->nifu_flags[enemy] ^= 1 << fileOf(to);

            // 駒を取ったので増やす．
            hand_[self].add(npt_cap);

            // 取られた駒が盤上から消える
            k -= Zobrist::psq[to][pc_cap];
            h += Zobrist::hand[self][npt_cap];
            material -= captureScore(pc_cap);
        }

#ifdef USE_EVAL
        else
            dp.dirty_num = 1;// 動いた駒は１個

                             // 移動元にあった駒のpiece_noを得る
        PieceNo piece_no2 = pieceNoOf(pc, from);
        dp.piece_no[0] = piece_no2;
        dp.pre_piece[0] = eval_list_.extBonaPiece(piece_no2);
        eval_list_.putPiece(piece_no2, to, pc_to);
        dp.now_piece[0] = eval_list_.extBonaPiece(piece_no2);
#ifdef USE_BYTEBOARD
        pieceno_to_sq_[piece_no2] = to;
        sq_to_pieceno_[to] = piece_no2;
#endif
#endif
#ifdef USE_BITBOARD
        // occupiedの更新
        bb_type_[OCCUPIED] = bbTurn(BLACK) ^ bbTurn(WHITE);
#endif
        if (pt_to == KING)
            king_[self] = to;

        // 王手している駒のbitboardを更新する
        if (gives_check)
        {
#ifdef USE_BITBOARD
            const StateInfo* prev = st_->previous;

            // 直接王手のときの王手駒
            st_->checkers = prev->check_sq[pt_to] & to;

            // 空き王手
            const Square ksq = kingSquare(enemy);

            if (isDiscoveredCheck(from, to, ksq, prev->blockers_for_king[enemy]))
            {
                switch (relation(from, ksq))
                {
                case DIRECT_MISC:
                    assert(false);
                    break;
                case DIRECT_FILE: // fromの位置から調べるのがミソ 敵玉と空き王手している駒にぶつかる
                    st_->checkers |= attacks<DIRECT_FILE>(from, bbOccupied()) & bbTurn(self); break;
                case DIRECT_RANK:
                    st_->checkers |= attacks<DIRECT_RANK>(from, bbOccupied()) & bbTurn(self); break;
                case DIRECT_DIAG1: case DIRECT_DIAG2:
                    st_->checkers |= bishopAttack(ksq, bbOccupied()) & bbPiece(BISHOP, HORSE, self); break;
                default:
                    UNREACHABLE;
                }
            }
#endif
            st_->continue_check[self] += 2;
        }
        else
        {
#ifdef USE_BITBOARD
            st_->checkers = allZeroMask();
#endif
            st_->continue_check[self] = 0;
        }
#ifdef USE_BYTEBOARD
#ifdef CALC_MOVE_DIFF
        calcMoveDiff<false, false>(from, to);
#endif
#endif
    }
#ifdef USE_BITBOARD
    bb_gold_ = bbType(GOLD, PRO_PAWN, PRO_LANCE, PRO_KNIGHT, PRO_SILVER);
    assert(st_->checkers == attackers<true>(self, kingSquare(enemy)));
#endif
    st_->board_key = k;
    st_->hand_key = h;
    st_->material = material;
    st_->hand = hand(enemy);
    turn_ = enemy;

    // increment ply counters.
    ++st_->plies_from_null;
    this_thread_->nodes.fetch_add(1, std::memory_order_relaxed);
    ++ply_;

    setCheckInfo(st_);
    assert(verify());
}

// 指し手で局面を戻す。
void Board::undoMove(const Move move)
{
    assert(verify());
    assert(move != MOVE_NONE);

    const Turn enemy = turn();
    const Turn self = ~enemy;
    const Square to = toSq(move);

    turn_ = self;

    assert(board_[to] == movedPieceTo(move));

#ifdef USE_EVAL
    // 移動後の駒
    auto moved_after_pc = board_[to];

    // 移動元のpiece_no == いまtoの場所にある駒のpiece_no
    PieceNo piece_no = pieceNoOf(moved_after_pc, to);
#endif

    if (isDrop(move)) // 持ち駒を打つ手
    {
        // 打った駒
        const PieceType pt_to = movedPieceType(move);
#ifdef USE_EVAL
        eval_list_.putPiece(piece_no, self, pt_to, hand(self).count(pt_to));
#ifdef USE_BYTEBOARD
        pieceno_to_sq_[piece_no] = SQ_MAX;
        stm_piece_[turnOf(move)] ^= 1ULL << piece_no;
#endif
#endif
#ifdef USE_BITBOARD
        // bitboardを更新
        bb_type_[pt_to] ^= to;
        bb_turn_[self] ^= to;
#endif
        // 打った場所を空にする。
        board_[to] = EMPTY;
#ifdef USE_BYTEBOARD
        bb_.setPieceBit(to, PB_EMPTY);
#endif
        // 持ち駒を増やす。
        hand_[self].add(pt_to);
#ifdef USE_BYTEBOARD
#ifdef CALC_MOVE_DIFF
        calcMoveDiff<true, true>(SQ_MAX, to);
#endif
#endif
    }
    else // 盤の上の指し手
    {
        const Square from = fromSq(move);
        const Piece pc_from = movedPiece(move);
        const PieceType pt_from = typeOf(pc_from);
        const PieceType pt_to = typeOf(piece(to));

        if (pt_to == KING)
            king_[self] = from;

        if (isCapture(move))
        {
            const Piece pc_cap = capturePiece(move);
#ifdef USE_EVAL
            PieceNo piece_no2 = pieceNoOf(self, nativeType(pc_cap));
            eval_list_.putPiece(piece_no2, to, pc_cap);
#ifdef USE_BYTEBOARD
            pieceno_to_sq_[piece_no2] = to;
            sq_to_pieceno_[to] = piece_no2;
            stm_piece_[turnOf(pc_cap)] |= 1ULL << piece_no2;
#endif
#endif
#ifdef USE_BITBOARD
            bb_type_[typeOf(pc_cap)] ^= to;
            bb_turn_[enemy] ^= to;
#endif
            board_[to] = pc_cap;
#ifdef USE_BYTEBOARD
            bb_.setPieceBit(to, toPieceBit(pc_cap));
#endif
            hand_[self].sub(nativeType(pc_cap));
        }
        else
        {
            board_[to] = EMPTY;
#ifdef USE_BYTEBOARD
            bb_.setPieceBit(to, PB_EMPTY);
#endif
        }

#ifdef USE_EVAL
        eval_list_.putPiece(piece_no, from, pc_from);
#ifdef USE_BYTEBOARD
        pieceno_to_sq_[piece_no] = from;
        sq_to_pieceno_[from] = piece_no;
#endif
#endif
#ifdef USE_BITBOARD
        bb_type_[pt_to] ^= to;
        bb_type_[pt_from] ^= from;
        bb_turn_[self] ^= mask(from) ^ mask(to);
#endif
        board_[from] = pc_from;
#ifdef USE_BYTEBOARD
        bb_.setPieceBit(from, toPieceBit(pc_from));
#ifdef CALC_MOVE_DIFF
        calcMoveDiff<false, true>(from, to);
#endif
#endif
    }
#ifdef USE_BITBOARD
    bb_type_[OCCUPIED] = bbTurn(BLACK) ^ bbTurn(WHITE);
    bb_gold_ = bbType(GOLD, PRO_PAWN, PRO_LANCE, PRO_KNIGHT, PRO_SILVER);
#endif
    st_ = st_->previous;
    ply_--;
    assert(verify());
}

// パスする。
void Board::doNullMove(StateInfo& new_st)
{
    assert(!inCheck());
    assert(&new_st != st_);
    std::memcpy(&new_st, st_, sizeof(StateInfo));
    new_st.previous = st_;
    st_ = &new_st;
    st_->board_key ^= Zobrist::turn;
    st_->plies_from_null = 0;
    prefetch(GlobalTT.firstEntry(st_->key()));
    turn_ = ~turn_;
    st_->hand = hand(turn());
    setCheckInfo<true>(st_);
    assert(verify());
}

// パスを元に戻す
void Board::undoNullMove()
{
    assert(!inCheck());
    st_ = st_->previous;
    turn_ = ~turn_;
}
#ifdef USE_BITBOARD
// t側がsqにある駒を取れるならばtrueを返す。
// pinも考慮するので、bb_discoveredにはpinnedPieces(自分の王手駒と敵玉の間にいてpinされている敵の駒)を渡す。
bool Board::canPieceCapture(const Turn t, const Square sq, const Square king_square, const Bitboard& bb_discovered) const
{
    Bitboard bb_from = attackers<true>(t, sq);

    if (bb_from & ~bb_discovered)
        return true;

    bb_from &= bb_discovered;

    while (bb_from)
        if (!isDiscoveredCheck(bb_from.firstOne(), sq, king_square, bb_discovered))
            return true;

    // 玉以外の駒で、打った駒を取れない。
    return false;
}

// bb_discoveredを遅延評価するバージョン
bool Board::canPieceCapture1(const Turn t, const Square sq, const Square king_square) const
{
    Bitboard bb_from = attackers<true>(t, sq);

    if (bb_from)
    {
        // 自分の王手駒と敵玉の間にいてpinされている敵の駒のbitboard
        const Bitboard bb_discovered = st_->blockers_for_king[t];


        if (bb_from & ~bb_discovered)
            return true;

        bb_from &= bb_discovered;

        do {
            if (!isDiscoveredCheck(bb_from.firstOne(), sq, king_square, bb_discovered))
                return true;
        } while (bb_from);
    }

    // 玉以外の駒で、打った駒を取れない。
    return false;
}

// sqから王手されているenemy側の玉が逃げられるかどうかを返す。
// bbはsqに置かれた王手駒の利きを表しており、敵玉はbbが1になっているマスには動けない。
template <Index TK>
bool Board::canKingEscape(const Square ksq, const Turn self, const Square sq, const Bitboard& bb) const
{
    const Turn enemy = ~self;
    uint64_t bb_kingmove = ~bb.b(TK) & ~bbTurn(enemy).b(TK) & kingAttack(ksq).b(TK);

    // 王手駒を王で取る手はいったん無効。(canPieceCaptureで調べるので）
    bb_kingmove &= ~mask(sq).b(TK);

    if (bb_kingmove)
    {
        // 玉を取り除き、王手駒を加えて利きを調べる。
        Bitboard occ = bbOccupied();
        occ |= sq;
        occ &= ~mask(ksq);

        do {
            if (!existAttacker(self, firstOne<TK>(bb_kingmove), occ))
                return true;
        } while (bb_kingmove);
    }

    return false;
}
template bool Board::canKingEscape<LOW>(const Square ksq, const Turn self, const Square sq, const Bitboard& bb) const;
template bool Board::canKingEscape<HIGH>(const Square ksq, const Turn self, const Square sq, const Bitboard& bb) const;
bool Board::isPawnDropCheckMate1(const Turn self, const Square sq, const Square king_square) const
{
    const Turn enemy = ~self;

    // 玉以外の駒で、打たれた歩を取れるなら、打ち歩詰めではない。
    if (canPieceCapture1(enemy, sq, king_square))
        return false;

    const Bitboard occupied = bbOccupied() ^ sq;

    // 玉が動ける場所(駒の利きは考慮せず、ただ単に敵側の駒がないところ)
    // これ、sqがHIGH, LOWかわかれば64bit演算にできる。(が、HIGH, LOWを求めるコストも考えるとこのままでいいかも。）
    Bitboard bb_king_move = bbTurn(enemy).notThisAnd(kingAttack(king_square));

    assert(bb_king_move);

    // 少なくとも歩をとる方向には玉が動けるはずなので、do whileを使用。
    do {
        if (!existAttacker(self, bb_king_move.firstOne(), occupied)) // 相手玉の移動先に自分の駒がないのなら、打ち歩詰めではない
            return false;
    } while (bb_king_move);

    return true;
}

// 1手詰め判定とヘルパー関数は、Aperyを参考にしています。
// この局面が自分の手番だとして、1手で相手玉が詰むかどうか。詰むならその指し手を返す。
// 詰まないならMOVE_NONEを返す。ksqは敵玉の位置。
// RBBを生かして、なるべく64bit演算で済むように工夫している。
template <Turn T, Index TK> Move Board::mate1ply(const Square ksq)
{
    // 自玉に王手がかかっていたらここでは相手玉の1手詰めは調べない。そういうときにこの関数を読んではならない。
    // 厳密には先手玉に王手がかかっていても王手駒をとりかえせば1手詰めだったりするが、それはここでは調べないことにする。
    assert(!inCheck());

    const Turn self = T;
    const Turn enemy = ~T;
    const Square tsouth = T == BLACK ? SQ_D : SQ_U;
    const Index T_HIGH = T == BLACK ? HIGH : LOW;
    const Index T_LOW = T == BLACK ? LOW : HIGH;

    // 駒打ちから。

    // 駒が打てる場所。IndexはTK。
    const uint64_t drop64 = bbEmpty().b(TK);
    const Hand h = hand(self);

    // 自分の駒でピンしている相手の駒
    const Bitboard dc_between_enemy = st_->blockers_for_king[enemy];

    if (h.exists(ROOK))
    {
        // 飛車による近接王手なので、調べる範囲はTKでよい。
        uint64_t to64 = drop64 & rookStepAttack(ksq).b(TK);

        while (to64)
        {
            const Square to = firstOne<TK>(to64);

            // 自分の駒の利きがあって
            // 相手玉では取れなくて
            // 他の駒でも取れないなら詰み！
            if (existAttacker(self, to)
                && !canKingEscape<TK>(ksq, self, to, rookAttackToEdge(to))
                && !canPieceCapture(enemy, to, ksq, dc_between_enemy))
                return makeDrop(self == BLACK ? B_ROOK : W_ROOK, to);
        }
    }
    else if (h.exists(LANCE) && isBehind(enemy, RANK_1, ksq))
    {
        // 近接王手なので玉の真下（手番側から見て）
        const Square to = ksq + tsouth;

        if (piece(to) == EMPTY
            && existAttacker(self, to)
            && !canKingEscape<TK>(ksq, self, to, lanceAttackToEdge(self, to))
            && !canPieceCapture(enemy, to, ksq, dc_between_enemy))
            return makeDrop(self == BLACK ? B_LANCE : W_LANCE, to);
    }

    if (h.exists(BISHOP))
    {
        uint64_t to64 = drop64 & bishopStepAttack(ksq).b(TK);

        while (to64)
        {
            const Square to = firstOne<TK>(to64);

            if (existAttacker(self, to)
                && !canKingEscape<TK>(ksq, self, to, bishopAttackToEdge(to))
                && !canPieceCapture(enemy, to, ksq, dc_between_enemy))
                return makeDrop(self == BLACK ? B_BISHOP : W_BISHOP, to);
        }
    }

    if (h.exists(GOLD))
    {
        uint64_t to64;

        // 飛車を玉のお尻から打って詰まなかったなら金を打っても詰まない
        if (h.exists(ROOK))
            to64 = drop64 & (goldAttack(enemy, ksq).b(TK) ^ pawnAttack(self, ksq).b(TK));
        else
            to64 = drop64 & goldAttack(enemy, ksq).b(TK);

        while (to64)
        {
            const Square to = firstOne<TK>(to64);

            if (existAttacker(self, to)
                && !canKingEscape<TK>(ksq, self, to, goldAttack(self, to))
                && !canPieceCapture(enemy, to, ksq, dc_between_enemy))
                return makeDrop(self == BLACK ? B_GOLD : W_GOLD, to);
        }
    }

    if (h.exists(SILVER))
    {
        uint64_t to64;

        if (h.exists(GOLD))
        {
            // 金と角で詰まなかったなら銀でも詰まない。
            if (h.exists(BISHOP))
                goto SilverDropEnd;

            // 斜め後ろから打つ手だけ調べる。
            to64 = drop64 & (silverAttack(enemy, ksq).b(TK) & frontMask(self, ksq).b(TK));
        }
        else
        {
            // 角で調べたので、斜め後ろから打つ手を省く。
            if (h.exists(BISHOP))
                to64 = drop64 & (goldAttack(enemy, ksq).b(TK) & silverAttack(enemy, ksq).b(TK));
            else
                to64 = drop64 & silverAttack(enemy, ksq).b(TK);
        }

        while (to64)
        {
            const Square to = firstOne<TK>(to64);

            if (existAttacker(self, to)
                && !canKingEscape<TK>(ksq, self, to, silverAttack(self, to))
                && !canPieceCapture(enemy, to, ksq, dc_between_enemy))
                return makeDrop(self == BLACK ? B_SILVER : W_SILVER, to);
        }
    }

SilverDropEnd:

    if (h.exists(KNIGHT))
    {
        uint64_t to64 = drop64 & knightAttack(enemy, ksq).b(TK);

        while (to64)
        {
            const Square to = firstOne<TK>(to64);

            if (!canKingEscape<TK>(ksq, self, to, allZeroMask())
                && !canPieceCapture(enemy, to, ksq, dc_between_enemy))
                return makeDrop(self == BLACK ? B_KNIGHT : W_KNIGHT, to);
        }
    }

    // 駒移動

    // targetは、自分の駒がない敵玉の8近傍
    const uint64_t target64 = bbTurn(self).notThisAnd(kingAttack(ksq)).b(TK);
    const Bitboard pinned = st_->blockers_for_king[self];
    const Bitboard dc_between_self = st_->blockers_for_king[enemy];

    // 竜
    {
        Bitboard bb_from = bbPiece(DRAGON, self);

        while (bb_from)
        {
            const Square from = bb_from.firstOne();

            uint64_t to64 = target64 & dragonAttack(from, bbOccupied()).b(TK);

            if (to64)
            {
                // 竜を消す。
                xorBBs(DRAGON, from, self);
                const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                do {
                    const Square to = firstOne<TK>(to64);

                    // 王手した場所に竜以外の自分の利きがあるか。
                    if (existAttacker(self, to))
                    {
                        // 玉が逃げる場所がなくて
                        // 両王手または取り返すこともできなくて
                        // 竜の移動でこっちの玉が素抜かれなければ詰み！
                        // 両王手のときはもう一枚王手駒があるので、そのとき逃げる場所も考慮すれば詰み発見率があがるかも（香車か角）
                        if (!canKingEscape<TK>(ksq, self, to, dragonAttack(to, bbOccupied() ^ ksq))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(DRAGON, from, self);
                            return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_DRAGON : W_DRAGON, *this);
                        }
                    }
                } while (to64);

                xorBBs(DRAGON, from, self);
            }
        }
    }

    // 飛車
    {
        Bitboard bb_from = bbPiece(ROOK, self);
        uint64_t from123 = bb_from.b(T_HIGH) & ENEMY_MASK[T];

        // 敵陣からの移動
        while (from123)
        {
            const Square from = firstOne<T_HIGH>(from123);
            uint64_t to64 = target64 & rookAttack(from, bbOccupied()).b(TK);

            if (to64)
            {
                xorBBs(ROOK, from, self);
                const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                do {
                    const Square to = firstOne<TK>(to64);

                    // 成りで王手しているのでdragonAttack。
                    if (existAttacker(self, to)
                        && !canKingEscape<TK>(ksq, self, to, dragonAttack(to, bbOccupied() ^ ksq))
                        && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                            || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                        && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                    {
                        xorBBs(ROOK, from, self);
                        return makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_ROOK : W_ROOK, *this);
                    }

                } while (to64);

                xorBBs(ROOK, from, self);
            }
        }

        uint64_t from4_9 = bb_from.b(T_LOW) & SELF_MASK[T];

        // 敵陣ではないところからの移動
        while (from4_9)
        {
            const Square from = firstOne<T_LOW>(from4_9);
            uint64_t to64 = target64 & rookAttack(from, bbOccupied()).b(TK) & (rookStepAttack(ksq).b(TK) | enemyMask(self).b(TK));

            if (to64)
            {
                xorBBs(ROOK, from, self);
                const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                uint64_t to64_123 = to64 & enemyMask(self).b(TK);

                if (to64_123)
                {
                    to64 &= ~to64_123;

                    do {
                        const Square to = firstOne<TK>(to64_123);

                        if (existAttacker(self, to)
                            && !canKingEscape<TK>(ksq, self, to, dragonAttack(to, bbOccupied() ^ ksq))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(ROOK, from, self);
                            return makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_ROOK : W_ROOK, *this);
                        }
                    } while (to64_123);
                }

                while (to64)
                {
                    const Square to = firstOne<TK>(to64);
                    if (existAttacker(self, to)
                        && !canKingEscape<TK>(ksq, self, to, rookAttackToEdge(to))
                        && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                            || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                        && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                    {
                        xorBBs(ROOK, from, self);
                        return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_ROOK : W_ROOK, *this);
                    }
                }
                xorBBs(ROOK, from, self);
            }
        }
    }

    // 馬
    {
        Bitboard bb_from = bbPiece(HORSE, self);

        while (bb_from)
        {
            const Square from = bb_from.firstOne();

            uint64_t to64 = target64 & horseAttack(from, bbOccupied()).b(TK);

            if (to64)
            {
                // 竜を消す。
                xorBBs(HORSE, from, self);
                const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                do {
                    const Square to = firstOne<TK>(to64);

                    // 王手した場所に竜以外の自分の利きがあるか。
                    if (existAttacker(self, to))
                    {
                        if (!canKingEscape<TK>(ksq, self, to, horseAttackToEdge(to))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(HORSE, from, self);
                            return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_HORSE : W_HORSE, *this);
                        }
                    }
                } while (to64);

                xorBBs(HORSE, from, self);
            }
        }
    }

    // 角
    {
        Bitboard bb_from = bbPiece(BISHOP, self);
        uint64_t from123 = bb_from.b(T_HIGH) & ENEMY_MASK[self];

        // 敵陣からの移動
        if (from123)
        {
            do {
                const Square from = firstOne<T_HIGH>(from123);
                uint64_t to64 = target64 & bishopAttack(from, bbOccupied()).b(TK);

                if (to64)
                {
                    xorBBs(BISHOP, from, self);
                    const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                    do {
                        const Square to = firstOne<TK>(to64);

                        if (existAttacker(self, to)
                            && !canKingEscape<TK>(ksq, self, to, horseAttackToEdge(to))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(BISHOP, from, self);
                            return makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_BISHOP : W_BISHOP, *this);
                        }

                    } while (to64);

                    xorBBs(BISHOP, from, self);
                }
            } while (from123);
        }

        uint64_t from4_9 = bb_from.b(T_LOW) & SELF_MASK[T];

        // 敵陣ではないところからの移動
        while (from4_9)
        {
            const Square from = firstOne<T_LOW>(from4_9);
            uint64_t to64 = target64 & bishopAttack(from, bbOccupied()).b(TK) & (bishopStepAttack(ksq).b(TK) | enemyMask(self).b(TK));

            if (to64)
            {
                xorBBs(BISHOP, from, self);
                const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                uint64_t to64_123 = to64 & enemyMask(self).b(TK);

                if (to64_123)
                {
                    to64 &= ~to64_123;

                    do {
                        const Square to = firstOne<TK>(to64_123);

                        if (existAttacker(self, to)
                            && !canKingEscape<TK>(ksq, self, to, horseAttackToEdge(to))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(BISHOP, from, self);
                            return makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_BISHOP : W_BISHOP, *this);
                        }
                    } while (to64_123);
                }

                while (to64)
                {
                    const Square to = firstOne<TK>(to64);
                    if (existAttacker(self, to)
                        && !canKingEscape<TK>(ksq, self, to, bishopAttackToEdge(to))
                        && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                            || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                        && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                    {
                        xorBBs(BISHOP, from, self);
                        return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_BISHOP : W_BISHOP, *this);
                    }
                }
                xorBBs(BISHOP, from, self);
            }
        }
    }

    // 金、成金
    {
        uint64_t from64 = bbGold().b(TK) & bbTurn(self).b(TK) & goldCheck(self, ksq).b(TK);

        while (from64)
        {
            const Square from = firstOne<TK>(from64);
            uint64_t to64 = target64 & goldAttack(self, from).b(TK) & goldAttack(enemy, ksq).b(TK);

            if (to64)
            {
                const Piece p = piece(from);
                xorBBs(typeOf(p), from, self);
                bb_gold_ ^= from;
                const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                do {
                    const Square to = firstOne<TK>(to64);

                    if (existAttacker(self, to)
                        && !canKingEscape<TK>(ksq, self, to, goldAttack(self, to))
                        && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                            || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                        && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                    {
                        xorBBs(typeOf(p), from, self);
                        bb_gold_ ^= from;
                        return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, p, *this);
                    }

                } while (to64);

                xorBBs(typeOf(p), from, self);
                bb_gold_ ^= from;
            }
        }
    }

    // 銀
    {
        uint64_t from64 = bbPiece(SILVER, self).b(TK) & silverCheck(self, ksq).b(TK);

        if (from64)
        {
            // 成らなくても王手になる場所
            uint64_t chk = silverAttack(enemy, ksq).b(TK);

            // 成って王手になる場所
            uint64_t pro_chk = goldAttack(enemy, ksq).b(TK) & enemyMaskPlus1(self).b(TK);

            do {
                const Square from = firstOne<TK>(from64);
                uint64_t to64 = target64 & silverAttack(self, from).b(TK);
                uint64_t to_pro = to64 & pro_chk;
                to64 &= chk;

                if (to64 || to_pro)
                {
                    xorBBs(SILVER, from, self);
                    const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                    while (to_pro)
                    {
                        const Square to = firstOne<TK>(to_pro);

                        if ((canPromote(self, from) || canPromote(self, to))
                            && existAttacker(self, to)
                            && !canKingEscape<TK>(ksq, self, to, goldAttack(self, to))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(SILVER, from, self);
                            return makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_SILVER : W_SILVER, *this);
                        }
                    }

                    // 玉の前方に移動するとき、なりで詰まないならならずでも詰まない。
                    //to &= ~frontMask(enemy, rankOf(ksq));

                    while (to64)
                    {
                        const Square to = firstOne<TK>(to64);

                        if (existAttacker(self, to)
                            && !canKingEscape<TK>(ksq, self, to, silverAttack(self, to))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(SILVER, from, self);
                            return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_SILVER : W_SILVER, *this);
                        }
                    }
                    xorBBs(SILVER, from, self);
                }
            } while (from64);
        }
    }

    // 桂馬
    {
        // 盤上の桂馬はLOW盤面にしか存在できないので、T_LOWを使用。
        uint64_t from64 = bbPiece(KNIGHT, self).b(T_LOW) & knightCheck(self, ksq).b(T_LOW);

        if (from64)
        {
            // 桂馬を動かして王手できる場所に玉がいるということは、相手玉はT_HIGH盤面にいる。
            assert(TK == T_HIGH);

            // 成って王手できる場所
            uint64_t chk_pro = goldAttack(enemy, ksq).b(T_HIGH) & enemyMask(self).b(T_HIGH);

            // 成りでなくても王手できる場所
            uint64_t chk = knightAttack(enemy, ksq).b(T_HIGH);

            do {
                const Square from = firstOne<T_LOW>(from64);
                uint64_t to64 = ~bbTurn(self).b(T_HIGH) & knightAttack(self, from).b(T_HIGH);
                uint64_t to_pro = to64 & chk_pro;
                to64 &= chk;

                if (to64 || to_pro)
                {
                    xorBBs(KNIGHT, from, self);
                    const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                    while (to_pro)
                    {
                        const Square to = firstOne<T_HIGH>(to_pro);

                        if (existAttacker(self, to)
                            && !canKingEscape<T_HIGH>(ksq, self, to, goldAttack(self, to))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(KNIGHT, from, self);
                            return makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_KNIGHT : W_KNIGHT, *this);
                        }
                    }


                    while (to64)
                    {
                        const Square to = firstOne<T_HIGH>(to64);

                        if (!canKingEscape<T_HIGH>(ksq, self, to, allZeroMask())
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(KNIGHT, from, self);
                            return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_KNIGHT : W_KNIGHT, *this);
                        }
                    }
                    xorBBs(KNIGHT, from, self);
                }
            } while (from64);
        }
    }

    // 香車
    {
        Bitboard bb_from = bbPiece(LANCE, self) & lanceCheck(self, ksq);
        const Rank TRank2 = self == BLACK ? RANK_2 : RANK_8;

        if (bb_from)
        {
            const uint64_t chk_pro = goldAttack(enemy, ksq).b(TK) & enemyMask(self).b(TK);
            const uint64_t chk = pawnAttack(enemy, ksq).b(TK) & frontMask(enemy, TRank2).b(TK);

            do {
                const Square from = bb_from.firstOne();
                uint64_t to64 = target64 & lanceAttack(self, from, bbOccupied()).b(TK);
                uint64_t to_pro = to64 & chk_pro;
                to64 &= chk;

                if (to64 || to_pro)
                {
                    xorBBs(LANCE, from, self);
                    const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                    while (to_pro)
                    {
                        const Square to = firstOne<TK>(to_pro);

                        if (existAttacker(self, to)
                            && !canKingEscape<TK>(ksq, self, to, goldAttack(self, to))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(LANCE, from, self);
                            return makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_LANCE : W_LANCE, *this);
                        }
                    }


                    if (to64)
                    {
                        assert(!(to64 & (to64 - 1)));
                        const Square to = ksq + tsouth;

                        if (existAttacker(self, to)
                            && !canKingEscape<TK>(ksq, self, to, lanceAttackToEdge(self, to))
                            && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                                || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                            && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                        {
                            xorBBs(LANCE, from, self);
                            return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_LANCE : W_LANCE, *this);
                        }
                    }
                    xorBBs(LANCE, from, self);
                }
            } while (bb_from);
        }
    }

    // 歩
    {
        const Rank krank = rankOf(ksq);

        if (isBehind(enemy, RANK_2, krank))
        {
            uint64_t from64 = bbPiece(PAWN, self).b(TK);
            const Square tnorth = self == BLACK ? SQ_U : SQ_D;

            if (canPromote(self, krank))
            {
                const uint64_t to_pro = target64 & goldAttack(enemy, ksq).b(TK) & enemyMask(self).b(TK);
                uint64_t from_pro = from64 & (self == BLACK ? to_pro << 9 : to_pro >> 9);

                while (from_pro)
                {
                    const Square from = firstOne<TK>(from_pro);
                    const Square to = from + tnorth;

                    xorBBs(PAWN, from, self);
                    const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                    if (existAttacker(self, to)
                        && !canKingEscape<TK>(ksq, self, to, goldAttack(self, to))
                        && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                            || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                        && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                    {
                        xorBBs(PAWN, from, self);
                        return makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_PAWN : W_PAWN, *this);
                    }
                    xorBBs(PAWN, from, self);
                }
            }
            // 不成

            const Square to = ksq + tsouth;
            const Square from = to + tsouth;

            if (from64 & mask(from).b(TK) && !(bbTurn(self).b(TK) & mask(to).b(TK)))
            {
                if (isBehind(self, RANK_2, krank))
                {
                    xorBBs(PAWN, from, self);
                    const Bitboard dc_between_enemy_after = sliderBlockers<true>(self, ksq);

                    if (existAttacker(self, to)
                        && !canKingEscape<TK>(ksq, self, to, allZeroMask())
                        && (isDiscoveredCheck(from, to, ksq, dc_between_self)
                            || !canPieceCapture(enemy, to, ksq, dc_between_enemy_after))
                        && !isDiscoveredCheck(from, to, kingSquare(self), pinned))
                    {
                        xorBBs(PAWN, from, self);
                        return makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_PAWN : W_PAWN, *this);
                    }
                    xorBBs(PAWN, from, self);
                }
            }
        }
    }

    return MOVE_NONE;
}

// mate1plyのラッパー
// 玉が5段目より上にいるならHIGH盤面、5段目を含めて下にいるならLOW盤面を使用する。
Move Board::mate1ply1() const
{
    const Turn t = turn();
    const Square ksq = kingSquare(~t);
    Board* tb = const_cast<Board*>(this);
    // 相手玉はRANK_5を含めずそこよりt側の自陣側にいる。
    const bool is_behind = isBehind(t, RANK_5, ksq);

    Move ret = t == BLACK ? is_behind ? tb->mate1ply<BLACK, LOW>(ksq) : tb->mate1ply<BLACK, HIGH>(ksq)
        : is_behind ? tb->mate1ply<WHITE, HIGH>(ksq) : tb->mate1ply<WHITE, LOW>(ksq);

    assert(verify());
    assert(ret == MOVE_NONE || (pseudoLegal(ret) && legal(ret)));
    return ret;
}

#endif

// @see https://stackoverflow.com/questions/14274225/statement-goto-can-not-cross-pointer-definition
#define GOTO_FAILED {std::cout << "error!" << " failed step is " << failed_step << *this << std::endl; return false;}

// 各種ビットボード、盤面が正しいかどうかをチェックする
bool Board::verify() const
{
    int failed_step = 0;
    StateInfo st;

    // step 0

    // 駒の枚数チェック (+1はEMPTYの分)
    int p_max[PIECETYPE_MAX + 1] = { 0 };

    // 盤面から調べる
    for (auto sq : Squares)
        p_max[typeOf(piece(sq))]++;

    // 持ち駒も調べる
    for (auto hp : HandPiece)
        p_max[hp] += hand(BLACK).count(hp) + hand(WHITE).count(hp);

    if (p_max[PAWN] + p_max[PRO_PAWN] != 18 ||
        p_max[LANCE] + p_max[PRO_LANCE] != 4 ||
        p_max[KNIGHT] + p_max[PRO_KNIGHT] != 4 ||
        p_max[SILVER] + p_max[PRO_SILVER] != 4 ||
        p_max[BISHOP] + p_max[HORSE] != 2 ||
        p_max[ROOK] + p_max[DRAGON] != 2 ||
        p_max[GOLD] != 4 ||
        p_max[KING] != 2)
        GOTO_FAILED;

    failed_step++;

    // step 1
#ifdef USE_BITBOARD
    int p_max_bb[PIECETYPE_MAX] = { 0 };

    // ビットボードから
    for (auto pt : PieceTypes)
        for (auto sq : bbType(pt))
            if (pt == typeOf(piece(sq)))
                p_max_bb[typeOf(piece(sq))]++;
            else
                GOTO_FAILED;
#endif
    failed_step++;

    // step 2
#ifdef USE_BITBOARD
    for (auto hp : HandPiece)
        p_max_bb[hp] += hand(BLACK).count(hp) + hand(WHITE).count(hp);

    if (p_max_bb[PAWN] + p_max_bb[PRO_PAWN] != 18 ||
        p_max_bb[LANCE] + p_max_bb[PRO_LANCE] != 4 ||
        p_max_bb[KNIGHT] + p_max_bb[PRO_KNIGHT] != 4 ||
        p_max_bb[SILVER] + p_max_bb[PRO_SILVER] != 4 ||
        p_max_bb[BISHOP] + p_max_bb[HORSE] != 2 ||
        p_max_bb[ROOK] + p_max_bb[DRAGON] != 2 ||
        p_max_bb[GOLD] != 4 ||
        p_max_bb[KING] != 2)
        GOTO_FAILED;
#endif
    failed_step++;

    // step 3

    // 2歩チェック
    for (auto file : Files)
    {
        int pawn[TURN_MAX] = { 0 };

        for (auto rank : Ranks)
        {
            Piece p = board_[rank * 9 + file];

            if (typeOf(p) == PAWN)
                pawn[turnOf(p)]++;
        }

        if (pawn[BLACK] > 1 || pawn[WHITE] > 1)
            GOTO_FAILED;
    }


#if 1
    failed_step++;

    // step 4

    // 行き所のない駒のチェック
    // 先手の桂香歩
    for (Square sq = SQ_91; sq <= SQ_11; ++sq)
        if (board_[sq] == B_PAWN || board_[sq] == B_LANCE)
            GOTO_FAILED;

    for (Square sq = SQ_91; sq <= SQ_12; ++sq)
        if (board_[sq] == B_KNIGHT)
            GOTO_FAILED;

    failed_step++;

    // step 5

    // 後手の桂香歩
    for (Square sq = SQ_99; sq <= SQ_19; ++sq)
        if (board_[sq] == W_PAWN || board_[sq] == W_LANCE)
            GOTO_FAILED;

    for (Square sq = SQ_98; sq <= SQ_19; ++sq)
        if (board_[sq] == W_KNIGHT)
            GOTO_FAILED;

    failed_step++;

    // step 6
#ifdef USE_BITBOARD
    if (bbTurn(BLACK) & bbTurn(WHITE))
        GOTO_FAILED;
#endif
    failed_step++;

    // step 7
#ifdef USE_BITBOARD
    if ((bbTurn(BLACK) | bbTurn(WHITE)) != bbOccupied())
        GOTO_FAILED;
#endif
    failed_step++;

    // step 8
#ifdef USE_BITBOARD
    // bitboardすべてをxorしたらoccupiedになるかどうか
    if ((bbType(PAWN) ^ bbType(LANCE) ^ bbType(KNIGHT) ^ bbType(SILVER) ^ bbType(BISHOP) ^
        bbType(ROOK) ^ bbType(GOLD) ^ bbType(KING) ^ bbType(HORSE) ^ bbType(DRAGON) ^
        bbType(PRO_PAWN) ^ bbType(PRO_LANCE) ^ bbType(PRO_KNIGHT) ^ bbType(PRO_SILVER)) != bbOccupied())
        GOTO_FAILED;
#endif
    failed_step++;

    // step 9
#ifdef USE_BITBOARD
    for (auto pt1 : PieceTypes)
        for (PieceType pt2 = PieceType(pt1 + 1); pt2 < PIECETYPE_MAX; ++pt2)
            if ((bbType(pt1) & bbType(pt2)))
                GOTO_FAILED;
#endif
    failed_step++;
#endif


    // step 10
#ifdef USE_BITBOARD
    // 相手玉を取れないことを確認
    const Turn self = turn();
    const Turn enemy = ~self;
    const Square king_square = kingSquare(enemy);

    if (attackers(self, king_square))
        GOTO_FAILED;
#endif
    failed_step++;

    // step 11
#ifdef USE_BITBOARD
    for (auto sq : Squares)
    {
        const Piece p = piece(sq);

        if (p == EMPTY)
        {
            if (!(bbEmpty() & sq))
                GOTO_FAILED;

            if (bbTurn(BLACK) & sq || bbTurn(WHITE) & sq || bbOccupied() & sq
                || bbType(PAWN) & sq || bbType(LANCE) & sq || bbType(KNIGHT) & sq
                || bbType(SILVER) & sq || bbType(GOLD) & sq || bbType(BISHOP) & sq || bbType(ROOK) & sq
                || bbType(HORSE) & sq || bbType(DRAGON) & sq || bbType(PRO_PAWN) & sq || bbType(PRO_LANCE) & sq
                || bbType(PRO_KNIGHT) & sq || bbType(PRO_SILVER) & sq)
                GOTO_FAILED;
        }
        else
        {
            if (!(bbPiece(p) & sq))
                GOTO_FAILED;
        }
    }

    // step 12
    if (bb_gold_ != bbType(GOLD, PRO_PAWN, PRO_LANCE, PRO_KNIGHT, PRO_SILVER))
        GOTO_FAILED;
#endif

    failed_step++;

    // step 13 hashkeyのチェック

    setState(&st);

    if (st.boardKey() != st_->boardKey()
        || st.handKey() != st_->handKey()
        || st.material != st_->material
        || st.hand != st_->hand)
    {
        std::cout << "boardKey = " << st_->boardKey() << " good boardKey = " << st.boardKey()
            << "\nhandKey = " << st_->handKey() << " good handKey = " << st.handKey()
            << "\nmaterial = " << st_->material << " good material = " << st.material
            << "\n hand = " << st_->hand << " good hand = " << st.hand << std::endl;
        GOTO_FAILED;
    }

#ifdef USE_EVAL
    failed_step++;

    // step 14 駒番号に対するBonaPieceの整合性のチェック
    auto list_fb = evalList()->pieceListFb();

    for (PieceNo no = PIECE_NO_PAWN; no < PIECE_NO_NB; no++)
        if (evalList()->pieceNoOf(list_fb[no]) != no)
            GOTO_FAILED;

    failed_step++;

#if 0
    // step 15 BonaPieceが本当に存在するかどうかのチェック
    const auto& BHI = Eval::BP_HAND_ID;

    for (PieceNo no = PIECE_NO_PAWN; no < PIECE_NO_NB; no++)
    {
        auto bp = list_fb[no];

        if (bp < Eval::fe_hand_end)
        {
            auto c = BLACK;

            for (PieceType pc = BISHOP; pc < KING; ++pc)
            {
                auto diff = BHI[c][pc].fw - BHI[c][pc].fb;

                if (BHI[c][pc].fb <= bp && bp < BHI[c][pc].fw)
                {
                    if (hand(BLACK).count(pc) < (bp - BHI[c][pc].fb))
                        GOTO_FAILED;
                }
                else if (BHI[c][pc].fw <= bp && bp < BHI[c][pc].fw + diff)
                {
                    if (hand(WHITE).count(pc) < (bp - BHI[c][pc].fw))
                        GOTO_FAILED;
                }
            }
        }
        else
        {
            for (auto pc = B_BISHOP; pc < PIECE_MAX; ++pc)
                if (Eval::BP_BOARD_ID[pc].fb <= bp && bp < Eval::BP_BOARD_ID[pc].fb + SQ_MAX)
                {
                    if (piece(Square(bp - Eval::BP_BOARD_ID[pc].fb)) != pc)
                        GOTO_FAILED;
                }
        }
    }
#endif
#endif

#ifdef USE_BYTEBOARD
    // step16 ByteboardとBoardの整合性チェック
    for (auto sq : Squares)
    {
        auto pb1 = toPieceBit(piece(sq));
        auto pb2 = bb_.pieceBit(sq);

        if (pb1 != pb2)
            GOTO_FAILED;
    }

    // step17 二歩フラグのチェック
    for (auto t : Turns)
        for (File f : Files)
        {
            bool exist_pawn = false;

            for (Rank r : Ranks)
                if (piece(sqOf(f, r)) == (t | PAWN))
                    exist_pawn = true;

            if (exist_pawn != existPawnFile(t, f))
            {
                std::cout << "nifuflag" << std::endl;
                GOTO_FAILED;
            }
        }

    failed_step++;

#if defined USE_EVAL
    // step18
    for (PieceNo i = PIECE_NO_ZERO; i < 40; i++)
    {
        Square sq = pieceSquare(i);

        if (sq == SQ_MAX)
        {
            for (auto t : Turns)
                if (stm_piece_[t] & (1ULL << i))
                {
                    std::cout << "stm_piece_[t] & (1ULL << i)" << std::endl;
                    GOTO_FAILED;
                }
        }
        else
        {
            PieceNo j = sq_to_pieceno_[sq];
            if (i != j)
            {
                std::cout << "pieceSquare" << std::endl;
                GOTO_FAILED;
            }
            Turn t = turnOf(piece(sq));

            if ((stm_piece_[t] & (1ULL << i)) == 0)
            {
                std::cout << "stm_piece_[t] & (1ULL << i)" << std::endl;
                GOTO_FAILED;
            }
            if ((stm_piece_[~t] & (1ULL << i)))
            {
                std::cout << "stm_piece_[~t] & (1ULL << i)" << std::endl;
                GOTO_FAILED;
            }
        }
    }
#endif
#ifdef CALC_MOVE_DIFF
    __m256i tmp[2];

    for (int i = 0; i < 40; i++)
    {
        Square sq = pieceSquare(i);

        if (sq != SQ_MAX && (dirty_flag_ & (1ULL << i)) == 0)
        {
            Piece p = piece(sq);
            __m256i* rp = generateMoveFrom(bb_, sq, p, tmp);
            assert(rp != tmp);
            if (move_cache_[i] != tmp[0])
            {
                std::cout << *this << "sq = " << pretty(sq) << "pieceno = " << i << " failed";
                GOTO_FAILED;
            }

            if (rp - tmp == 2)
            {
                assert(i >= PIECE_NO_BISHOP && i < PIECE_NO_KING);
                if (move_cache_[i + 6] != tmp[1])
                {
                    std::cout << *this << "sq = " << pretty(sq) << "pieceno = " << i + 6 << " failed";
                    GOTO_FAILED;
                };
            }
        }
    }
#endif
#endif
    return true;

}
