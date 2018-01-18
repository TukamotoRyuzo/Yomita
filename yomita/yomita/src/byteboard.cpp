#include <sstream>

#include "move.h"
#include "board.h"
#include "byteboard.h"

RelationType SQUARE_RELATIONSHIP[SQ_MAX][SQ_MAX];

#ifdef USE_BYTEBOARD
// shuffleに必要なbyteboard。
Byteboard SHUFFLE_NEIGHBOR[SQ_MAX];
Byteboard SHUFFLE_CROSS[SQ_MAX];
Byteboard SHUFFLE_DIAG[SQ_MAX];
Byteboard SHUFFLE_CHECKABLE_LOW[TURN_MAX][SQ_MAX];
Byteboard SHUFFLE_CHECKABLE_HIGH[TURN_MAX][SQ_MAX];

// sqに利いているPieceBit。packされた32byteに対して&することで利いているPieceBitを取得できる。
// NEIGHBORS:近接駒(桂馬含む）
// KNIGHTS:桂馬
// SLIDERS:飛び駒
// NO_KNIGHTS:桂馬以外
__m256i MASK_PACKED_BIT_NEIGHBORS[TURN_MAX];
__m256i MASK_PACKED_BIT_KNIGHTS[TURN_MAX];
__m256i MASK_PACKED_BIT_SLIDERS[TURN_MAX][SQ_MAX];
__m256i MASK_PACKED_BIT_NO_KNIGHTS[TURN_MAX][SQ_MAX];

// sqに対して1手で王手可能なPieceBit。成で王手になる駒も含む。
__m256i MASK_PACKED_BIT_CHECKABLE[RANK_MAX];

// index番目だけを取り出すマスク。32番目はどこも取り出さないダミー。
__m256i MASK_PACKED_BIT_INDICES[32 + 1];

// baseを起点としてpackされた32byteから盤面内に収まっているところだけを取り出すマスク。
__m256i MASK_PACKED_BIT_RAY_ONE[SQ_MAX];
__m256i MASK_PACKED_BIT_RAY_USE[SQ_MAX];

// baseを起点としてpackされた32byteの、下、左、左下、左下方向を取り出すマスク。
__m256i MASK_PACKED_BIT_RAY_SEPARATER[SQ_MAX];

// sqからbaseまでが1になったbitboard。sqは含まない。[base][sq]
__m256i MASK_BITBOARD_SQ_TO_BASE[SQ_MAX][SQ_MAX];

// PB_KINGが32byte分入っているマスク。
const __m256i MASK_PACKED_BIT_KING[TURN_MAX] = { _mm256_set1_epi8(PB_KING), _mm256_set1_epi8(PB_ENEMY | PB_KING) };

// MOVE_PROMOTEが32byte分入っているマスク
const __m256i MASK_MOVE_NOT_PROMOTE = _mm256_set1_epi16((short)0xbfff);

__m256i MASK_PACKED_BIT_NOKNIGHTS_EFFECTABLE[TURN_MAX][12];

// 各駒が打てる場所が1になっているマスク。bitboardみたいなものだが、avx命令とsse命令が混ざると
// 著しくパフォーマンスが落ちるので、avx命令が使えるように256bitで確保する。
__m256i PAWN_DROPABLE_MASK[TURN_MAX][512];
__m256i LANCE_DROPABLE_MASK[TURN_MAX];
__m256i KNIGHT_DROPABLE_MASK[TURN_MAX];

// 指し手配列。
__m256i MOVE16_PIECE_BASE[TURN_MAX][PIECETYPE_MAX][SQ_MAX];

// 4byte毎にSquareが入っている。持ち駒打ちを生成するときに使う。
__m256i MOVE_TO[SQ_MAX];

// 近接駒の利きの並びはDIRの順
// 00000000 00000000 00001111 11111111
// diag2    diag1    rank     file
// 11111111 11111111 11111111 11111111 
uint32_t PIECE_EFFECT[PIECE_MAX][SQ_MAX];

// baseを起点としてpackされた32byteを32bit化したものに対して、sqからbaseまでを取り出すマスク。sqは含まない。[base][sq]
uint32_t MASK_32BIT_SQ_TO_BASE[SQ_MAX][SQ_MAX];

uint32_t MASK_32BIT_CHECKABLE[PIECETYPE_MAX][32];

// self側の駒がtoに置かれたとき、ksqに利いているPieceBit。[self][ksq][to]
PieceBit MASK_PIECEBIT_ATTACK_TO_KSQ[TURN_MAX][SQ_MAX][SQ_MAX];

// baseを起点としてpackされた32byteに対して、sqの場所だけを取り出すマスクを得るためのindex。[base][sq]
uint8_t INDEX_SQ_ON_BASE_NEAR[SQ_MAX][SQ_MAX + 1];
uint8_t INDEX_SQ_ON_BASE_RAY[SQ_MAX][SQ_MAX + 1];

// PIECE_EFFECT、packed slidersのmovemaskのbit番号をSquareに変換するためのテーブル。
Square DESTINATION[32][SQ_MAX];

// PieceをPieceBitに変換するためのテーブル。
const PieceBit PIECE_TO_PIECEBIT[] =
{
    PB_EMPTY, PB_BISHOP, PB_ROOK, PB_PAWN, PB_LANCE, PB_KNIGHT, PB_SILVER, PB_GOLD, PB_KING,
    PB_HORSE, PB_DRAGON , PB_PRO_PAWN, PB_PRO_LANCE, PB_PRO_KNIGHT, PB_PRO_SILVER, PB_EMPTY, PB_ENEMY,
    PB_ENEMY | PB_BISHOP, PB_ENEMY | PB_ROOK, PB_ENEMY | PB_PAWN, PB_ENEMY | PB_LANCE,
    PB_ENEMY | PB_KNIGHT, PB_ENEMY | PB_SILVER, PB_ENEMY | PB_GOLD, PB_ENEMY | PB_KING,
    PB_ENEMY | PB_HORSE, PB_ENEMY | PB_DRAGON, PB_ENEMY | PB_PRO_PAWN, PB_ENEMY | PB_PRO_LANCE,
    PB_ENEMY | PB_PRO_KNIGHT, PB_ENEMY | PB_PRO_SILVER,
};

// targetに対してループを回すためのdefine。結構遅い。
#define FBT(target, to, xxx)\
{ uint64_t mask_ = target.m256i_u64[0]; while (mask_) { to = Square(bsf64(mask_)     ); xxx; mask_ &= mask_ - 1; }\
           mask_ = target.m256i_u64[1]; while (mask_) { to = Square(bsf64(mask_) + 64); xxx; mask_ &= mask_ - 1; }}\

// 12方向を表すSquare。↑↓→←右上左下右下左上先桂←先桂→後桂←後桂→の順番。
const Square DIR[] =
{
    // NEIGHBOR
    SQ_U, SQ_D, SQ_R, SQ_L,
    SQ_RU, SQ_LD, SQ_RD, SQ_LU,
    SQ_U + SQ_LU, SQ_U + SQ_RU,
    SQ_D + SQ_LD, SQ_D + SQ_RD,
};

const Square DIR_CHECKABLE[32] =
{
    SQ_LU + SQ_LU,
    SQ_U + SQ_LU,
    SQ_U + SQ_U, 
    SQ_U + SQ_RU, 
    SQ_RU + SQ_RU, 
    SQ_LU + SQ_L, 
    SQ_LU,
    SQ_U, 
    SQ_RU, 
    SQ_RU + SQ_R,
    SQ_L + SQ_L, 
    SQ_L,
    SQ_R, 
    SQ_R + SQ_R,
    SQ_LD + SQ_L, 
    SQ_LD, 
    SQ_D, 
    SQ_RD,
    SQ_RD + SQ_R,
    SQ_LD + SQ_LD,
    SQ_D + SQ_LD, 
    SQ_D + SQ_D, 
    SQ_D + SQ_RD,
    SQ_RD + SQ_RD,
    SQ_LD + SQ_LD + SQ_D, 
    SQ_LD + SQ_D + SQ_D, 
    SQ_D + SQ_D + SQ_D,
    SQ_RD + SQ_D + SQ_D,
    SQ_RD + SQ_RD + SQ_D,
    SQ_LD + SQ_LD + SQ_D + SQ_D,
    SQ_D + SQ_D + SQ_D + SQ_D,
    SQ_RD + SQ_RD + SQ_D + SQ_D,
};

// ゼロ判定。
inline bool isZero(__m256i mask) { return _mm256_testc_si256(_mm256_setzero_si256(), mask); }

// 何もない場所を返す。
inline __m256i emptyMask(__m256i mask) { return _mm256_cmpeq_epi8(mask, _mm256_setzero_si256()); }

// 32byteの32bitパターン化。Revは0になっているbyteを1にする。
inline uint32_t to32bit(__m256i mask) { return ~_mm256_movemask_epi8(_mm256_cmpeq_epi8(mask, _mm256_setzero_si256())); }
inline uint32_t to32bitRev(__m256i mask) { return _mm256_movemask_epi8(_mm256_cmpeq_epi8(mask, _mm256_setzero_si256())); }

__m256i Byteboard::emptys() const
{
    return _mm256_setr_epi32(to32bitRev(b_[0]), to32bitRev(b_[1]), to32bitRev(b_[2]) & 0x1ffff, 0, 0, 0, 0, 0);
}

__m256i Byteboard::turnField(const Turn t) const
{
    __m256i mask = _mm256_set_epi32(0xffffffff, 0xff000000, 0, 0, 0, 0, 0, 0);
    __m256i ret = t == WHITE ? b_[0] : _mm256_loadu_si256((__m256i*)(&p_[54]));
    return _mm256_andnot_si256(mask, ret);
}

// NEIGHBOR : sqの周り12マスのPieceBitを32byteにpackして返す。
// CROSS : sqから飛の利き上にあるPieceBitを32byteにpackして返す。
// DIAG  : sqから角の利き上にあるPieceBitを32byteにpackして返す。
// RAY   : sqから飛角の利き上にあるPieceBitを32byteにpackして返す。
template <PackType PT>
__m256i Byteboard::pack(Square sq) const
{
    return PT == NEIGHBOR ? packLow(SHUFFLE_NEIGHBOR[sq]) :
        PT == CROSS ? packLow(SHUFFLE_CROSS[sq]) :
        PT == DIAG ? packHigh(SHUFFLE_DIAG[sq]) :
        PT == RAY ? _mm256_permute2x128_si256(packLow(SHUFFLE_CROSS[sq]), packHigh(SHUFFLE_DIAG[sq]), 0x30) :
        PT == CHECKABLE_BLACK ? _mm256_permute2x128_si256(packLow(SHUFFLE_CHECKABLE_LOW[BLACK][sq]), packHigh(SHUFFLE_CHECKABLE_HIGH[BLACK][sq]), 0x30) :
        /*PT == CHECKABLE_WHITE ?*/ _mm256_permute2x128_si256(packLow(SHUFFLE_CHECKABLE_LOW[WHITE][sq]), packHigh(SHUFFLE_CHECKABLE_HIGH[WHITE][sq]), 0x30);
}

// 上位16byteにpackする。下位16byteはゴミ。DIAG用。
__m256i Byteboard::packHigh(const Byteboard& shuffle) const
{
    __m256i tmp = _mm256_shuffle_epi8(b_[0], shuffle.b_[0])
        ^ _mm256_shuffle_epi8(b_[1], shuffle.b_[1])
        ^ _mm256_shuffle_epi8(b_[2], shuffle.b_[2]);

    return tmp ^ _mm256_broadcastsi128_si256(_mm256_extracti128_si256(tmp, 0));
}

// Byteboardから任意の位置にあるPieceBitを最大16マス分packして返す。戻り値の上位16byteはゴミ。
__m256i Byteboard::packLow(const Byteboard& shuffle) const
{
    __m256i tmp = _mm256_shuffle_epi8(b_[0], shuffle.b_[0])
        ^ _mm256_shuffle_epi8(b_[1], shuffle.b_[1])
        ^ _mm256_shuffle_epi8(b_[2], shuffle.b_[2]);

    return tmp ^ _mm256_castsi128_si256(_mm256_extracti128_si256(tmp, 1));
}

Byteboard Byteboard::one()
{
    Byteboard b;
    __m256i allone = _mm256_cmpeq_epi8(_mm256_setzero_si256(), _mm256_setzero_si256());
    b.b_[0] = b.b_[1] = b.b_[2] = allone;
    return b;
}

// baseを起点としてpackされた32byteに対して、sqの場所だけを取り出すマスク
template <PackType PT> inline
__m256i squareMask(Square base, Square sq)
{
    static_assert(PT == NEIGHBOR || PT == RAY, "");
    const auto I = PT == NEIGHBOR ? INDEX_SQ_ON_BASE_NEAR : INDEX_SQ_ON_BASE_RAY;
    return MASK_PACKED_BIT_INDICES[I[base][sq]];
}

// baseを起点としてpackされた32byteに対して、sqの場所だけを取り出すマスク
template <PackType PT> inline
uint64_t square32(Square base, Square sq)
{
    static_assert(PT == NEIGHBOR || PT == RAY, "");
    const auto I = PT == NEIGHBOR ? INDEX_SQ_ON_BASE_NEAR : INDEX_SQ_ON_BASE_RAY;
    return 1ULL << I[base][sq];
}

// 大駒を含むpackされた32byteに対して、sqから大駒の利きを伸ばして一番初めにぶつかる所を取り出すマスクを返す。
// ExtractRouteがtrueならぶつかる所までの途中の場所もマスク化する。
template <PackType PT, bool ExtractRoute = true> inline
__m256i strikeMask(__m256i pack, Square sq)
{
    static_assert(PT != NEIGHBOR, "");

    // mask & -maskで最下位ビット(=一番最初にぶつかる駒）が取り出せるという考えを利用する。
    // 実際にはmaskは1命令では取得できない。しかしmaskを用意する必要はない。~maskで十分。
    __m256i not_mask = _mm256_cmpeq_epi8(_mm256_setzero_si256(), pack);
    __m256i not_mask_plus1 = _mm256_add_epi64(not_mask, MASK_PACKED_BIT_RAY_ONE[sq]);
    __m256i m1 = _mm256_andnot_si256(not_mask, not_mask_plus1);

    // m1をマスク化（0x01しかないので）これが一番初めにぶつかったところを取り出すマスクである。
    m1 = _mm256_cmpgt_epi8(m1, _mm256_setzero_si256());

    // sqから一番初めにぶつかるところまでを取り出すマスクを求める場合
    if (ExtractRoute)
    {
        __m256i m2 = _mm256_andnot_si256(not_mask_plus1, not_mask);
        m2 = _mm256_cmpgt_epi8(_mm256_setzero_si256(), m2);
        m1 |= m2;

        // 実際に使用する部分だけを取り出す。
        if (PT == RAY || PT == DIAG)
            m1 &= MASK_PACKED_BIT_RAY_USE[sq];
    }

    return m1;
}

template <PackType PT> inline
uint32_t strike32(__m256i pack, Square sq)
{
    return _mm256_movemask_epi8(strikeMask<PT>(pack, sq));
}

// t側のPieceBitだけを返す。上位16byteにごみが残っているとmaskの上位16byteにもゴミが乗るので注意。
// ~turnMask & XXX が欲しいときは、_mm256_andnot_si256をうまく活用すること。
inline __m256i turnMask(__m256i mask, Turn t)
{
    // PieceBitを符号付8bit整数とみると、先手のPieceBitは0より大きく後手のPieceBitは0より小さい。
    return t == BLACK ? _mm256_cmpgt_epi8(mask, _mm256_setzero_si256())
        : _mm256_cmpgt_epi8(_mm256_setzero_si256(), mask);
}

// t側のPieceBitではない場所を返す。
template <Turn T> inline
__m256i notTurnMask(__m256i mask)
{
    return (T == BLACK ? _mm256_cmpgt_epi8(_mm256_setzero_si256(), mask)
        : _mm256_cmpgt_epi8(mask, _mm256_setzero_si256())) | _mm256_cmpeq_epi8(mask, _mm256_setzero_si256());
}

// ↑の32bit版
inline uint32_t turn32(__m256i mask, Turn t)
{
    return t == BLACK ? _mm256_movemask_epi8(_mm256_cmpgt_epi8(mask, _mm256_setzero_si256()))
                      : _mm256_movemask_epi8(_mm256_cmpgt_epi8(_mm256_setzero_si256(), mask));
}

// sqに利いているt側の駒の、sqに利いているbitだけを残して返す。
// 戻り値は手番bitが立っておらず正しいPieceBitではない。駒の位置だけ知りたい場合に使う。（これで十分）
template <AttackerType AT, bool ExceptKing = false> inline
__m256i attackBit(Turn t, Square sq, __m256i pack)
{
    static_assert(!ExceptKing || AT == NEIGHBORS || AT == NO_KNIGHTS, "");

    if (ExceptKing)
    {
        __m256i pack2 = _mm256_andnot_si256(_mm256_cmpeq_epi8(MASK_PACKED_BIT_KING[t], pack), pack);

        if (AT == NEIGHBOR)
            return pack2 & turnMask(pack, t) & MASK_PACKED_BIT_NEIGHBORS[t];

        else
        {
            __m256i ret = pack2 & turnMask(pack, t) & MASK_PACKED_BIT_NO_KNIGHTS[t][sq];
            return isZero(ret) ? ret : ret & strikeMask<RAY, false>(pack, sq);
        }
    }

    else
    {
        if (AT == NEIGHBOR)
            return pack & turnMask(pack, t) & MASK_PACKED_BIT_NEIGHBORS[t];

        else if (AT == KNIGHTS)
            return pack & turnMask(pack, t) & MASK_PACKED_BIT_KNIGHTS[t];

        else if (AT == SLIDERS)
        {
            __m256i ret = pack & turnMask(pack, t) & MASK_PACKED_BIT_SLIDERS[t][sq];
            return isZero(ret) ? ret : ret & strikeMask<RAY, false>(pack, sq);
        }

        else
        {
            __m256i ret = pack & turnMask(pack, t) & MASK_PACKED_BIT_NO_KNIGHTS[t][sq];
            return isZero(ret) ? ret : ret & strikeMask<RAY, false>(pack, sq);
        }
    }
}


// sqに効いているt側の駒をAttackerTypeに応じて32byteにpackして返す。ExceptKingがtrueなら玉は除く。
// SLIDERS、KNIGHTSの中には玉が含まれるはずがないのでコンパイルエラーにする。SEEで使う。
template <AttackerType AT, bool ExceptKing = false> inline
__m256i attackers(Turn t, Square sq, __m256i pack)
{
    static_assert(!ExceptKing || AT == NEIGHBORS || AT == NO_KNIGHTS, "");
    __m256i attack_bit = attackBit<AT, ExceptKing>(t, sq, pack);
    __m256i mask = _mm256_cmpgt_epi8(attack_bit, _mm256_setzero_si256());
    return pack & mask;
}

// sqにt側の駒が利いているならtrueを返す。neighbor, rayがあらかじめ求まっている場合はこちらが早い。
template <bool ExceptKing = false> inline
bool existAttacker(Turn t, Square sq, __m256i neighbor, __m256i ray)
{
    return !isZero(attackBit<NEIGHBORS, ExceptKing>(t, sq, neighbor)) || !isZero(attackBit<SLIDERS>(t, sq, ray));
}

template <bool ExceptKing = false> FORCE_INLINE
bool existAttacker(Turn t, Square sq, const Byteboard& bb)
{
    return !isZero(attackBit<NEIGHBORS, ExceptKing>(t, sq, bb.pack<NEIGHBOR>(sq))) || !isZero(attackBit<SLIDERS>(t, sq, bb.pack<RAY>(sq)));
}

// blocker_candidatesからsqとsnipersを結ぶ線上にいるPieceBitだけを残して返す。
inline uint32_t blocker32(__m256i snipers, __m256i blocker_candidates, Square sq)
{
    __m256i m1 = snipers & MASK_PACKED_BIT_RAY_SEPARATER[sq];
    m1 = _mm256_cmpeq_epi64(m1, _mm256_setzero_si256());
    m1 = _mm256_andnot_si256(m1, MASK_PACKED_BIT_RAY_SEPARATER[sq]);
    __m256i m2 = _mm256_andnot_si256(MASK_PACKED_BIT_RAY_SEPARATER[sq], snipers);
    m2 = _mm256_cmpgt_epi64(m2, _mm256_setzero_si256());
    m2 = _mm256_andnot_si256(MASK_PACKED_BIT_RAY_SEPARATER[sq], m2);
    __m256i blockers = blocker_candidates & (m1 | m2);
    assert(!isZero(blockers));
    blockers = _mm256_cmpeq_epi8(blockers, _mm256_setzero_si256());
    return ~_mm256_movemask_epi8(blockers);
}

// ksqと飛び駒の間に１枚だけ駒がある場合に、間の駒(slider_blockers)を32bitにpackして返す。
inline uint32_t sliderBlockers(Square ksq, Turn king_turn, __m256i ray)
{
    const Turn t = ~king_turn;
    __m256i block1 = ray & strikeMask<RAY, false>(ray, ksq); ray ^= block1;
    __m256i block2 = ray & strikeMask<RAY, false>(ray, ksq) & turnMask(ray, t);
    __m256i snipers = block2 & MASK_PACKED_BIT_SLIDERS[t][ksq];
    return isZero(snipers) ? 0 : blocker32(snipers, block1, ksq);
}

// fromにある駒をtoに移動させたとき、ksqに利きが通るならtrueを返す。
// inline展開されるかされないかで3倍くらい速度が違うので必ずinline関数にすること。
template <bool IsKnight = false> inline
bool isDiscoveredCheck(uint32_t slider_blockers, Square from, Square to, Square ksq)
{
    return (slider_blockers & square32<RAY>(ksq, from)) && (IsKnight || !isAligned(from, to, ksq));
}

// pがtoに置かれたときにsqに対して利きを与えるならtrue
inline bool givesEffect(Turn t, Piece p, Square sq, Square to, __m256i reach)
{
    PieceBit pb = toPieceBit(p);

    if (pb & MASK_PIECEBIT_ATTACK_TO_KSQ[t][sq][to])
        if (!isSlider(p) || !isZero(reach & squareMask<RAY>(sq, to)))
            return true;

    return false;
}

template <Turn T> inline
bool givesEffect(PieceBit pb, Square sq, Square to)
{
    return pb & MASK_PIECEBIT_ATTACK_TO_KSQ[T][sq][to];
}

// maskから1ビット取り出してSquareに変換する。Clearがtrueなら見つけたビットを0にする。
template <PackType PT, bool Clear = true> inline
Square pop(uint32_t& mask, Square sq)
{
    static_assert(PT == CHECKABLE_BLACK || PT == CHECKABLE_WHITE || PT == NEIGHBOR || PT == RAY, "");
    int index = bsf64(mask);
    assert(0 <= index);

    if (Clear)
        mask &= mask - 1;

    return PT == CHECKABLE_BLACK ? sq + DIR_CHECKABLE[index]
        : PT == CHECKABLE_WHITE ? sq - DIR_CHECKABLE[index]
        : PT == RAY ? DESTINATION[index][sq]
        : sq + DIR[index];
}

inline bool Board::inCheck2() const { return !isZero(state()->checker_no_knights) || !isZero(state()->checker_knights); }
inline bool Board::existAttacker2(const Turn t, const Square sq) const { return ::existAttacker(t, sq, bb_); }

// 玉以外の駒でsqにある駒が取れるならtrue
bool Board::canPieceCapture2(const Turn t, const Square sq, const Square ksq) const
{
    uint32_t slider_blockers = state()->slider_blockers[t];

    if (!slider_blockers)
        return ::existAttacker<true>(t, sq, bb_);

    // 桂馬と桂馬以外で分ける。
    __m256i attacker = attackBit<KNIGHTS>(t, sq, bb_.pack<NEIGHBOR>(sq));

    if (!isZero(attacker))
    {
        uint32_t mask = to32bit(attacker);

        do {
            if (!::isDiscoveredCheck<true>(slider_blockers, pop<NEIGHBOR, false>(mask, sq), sq, ksq))
                return true;
        } while (mask &= mask - 1);
    }

    attacker = attackBit<NO_KNIGHTS, true>(t, sq, bb_.pack<RAY>(sq));

    if (!isZero(attacker))
    {
        uint32_t mask = to32bit(attacker);

        do {
            if (!::isDiscoveredCheck(slider_blockers, pop<RAY, false>(mask, sq), sq, ksq))
                return true;
        } while (mask &= mask - 1);
    }

    return false;
}

// 玉以外の駒でsqにある駒が取れるならtrue
bool Board::canPieceCapture2(const Turn t, const Square sq, const Square ksq, uint32_t slider_blockers) const
{
    if (!slider_blockers)
        return ::existAttacker<true>(t, sq, bb_);

    // 桂馬と桂馬以外で分ける。
    __m256i attacker = attackBit<KNIGHTS>(t, sq, bb_.pack<NEIGHBOR>(sq));

    if (!isZero(attacker))
    {
        uint32_t mask = to32bit(attacker);

        do {
            if (!::isDiscoveredCheck<true>(slider_blockers, pop<NEIGHBOR, false>(mask, sq), sq, ksq))
                return true;
        } while (mask &= mask - 1);
    }

    attacker = attackBit<NO_KNIGHTS, true>(t, sq, bb_.pack<RAY>(sq));

    if (!isZero(attacker))
    {
        uint32_t mask = to32bit(attacker);

        do {
            if (!::isDiscoveredCheck(slider_blockers, pop<RAY, false>(mask, sq), sq, ksq))
                return true;
        } while (mask &= mask - 1);
    }

    return false;
}

// neighborとrayがあらかじめ求まっている場合。
bool Board::canPieceCapture2(const Turn t, const Square sq, const Square ksq, uint32_t slider_blockers, __m256i neighbor, __m256i ray) const
{
    if (!slider_blockers)
        return ::existAttacker<true>(t, sq, neighbor, ray);

    // 桂馬と桂馬以外で分ける。
    __m256i attacker = attackBit<NO_KNIGHTS, true>(t, sq, ray);

    if (!isZero(attacker))
    {
        uint32_t mask = to32bit(attacker);

        do {
            if (!::isDiscoveredCheck(slider_blockers, pop<RAY, false>(mask, sq), sq, ksq))
                return true;
        } while (mask &= mask - 1);
    }
    
    attacker = attackBit<KNIGHTS>(t, sq, neighbor);

    if (!isZero(attacker))
    {
        uint32_t mask = to32bit(attacker);

        do {
            if (!::isDiscoveredCheck<true>(slider_blockers, pop<NEIGHBOR, false>(mask, sq), sq, ksq))
                return true;
        } while (mask &= mask - 1);
    }

    return false;
}

// self側がsqに歩を打つ手が打ち歩詰めならtrueを返す。
bool Board::isPawnDropCheckMate2(const Turn self, const Square sq, const Square ksq) const
{
    const Turn enemy = ~self;

    // 玉以外の駒で、打たれた歩を取れるなら、打ち歩詰めではない。
    if (canPieceCapture2(enemy, sq, ksq))
        return false;

    // neighborの上位16byteにゴミが入っているがPIECE_EFFECT[KING]の上位16byteが0なので大丈夫。
    auto effect = PIECE_EFFECT[KING][ksq] & ~turn32(state()->king_neighbor[enemy], enemy);
    assert(effect);

    // 相手玉の移動先に自分の駒の利きがある間調べていく。
    const_cast<Byteboard*>(&bb_)->setPieceBit(sq, self == BLACK ? PB_PAWN : PB_PAWN | PB_ENEMY);
    while (::existAttacker(self, pop<NEIGHBOR, false>(effect, ksq), bb_) && (effect &= effect - 1));
    const_cast<Byteboard*>(&bb_)->setPieceBit(sq, PB_EMPTY);

    // 玉が動ける場所が残っていない == 打ち歩詰め。
    return !effect;
}

namespace
{
    // SEEの順番
    constexpr PieceType NEXT_ATTACKER[PIECETYPE_MAX] =
    {
        NO_PIECE_TYPE,  // 空
        HORSE,          // 角
        DRAGON,         // 飛車
        LANCE,          // 歩
        KNIGHT,         // 香
        PRO_PAWN,       // 桂
        PRO_SILVER,     // 銀
        BISHOP,         // 金
        NO_PIECE_TYPE,  // 王
        ROOK,           // 馬
        KING,           // 竜
        PRO_LANCE,      // と
        PRO_KNIGHT,     // 杏
        SILVER,         // 圭
        GOLD,           // 全
    };

    // 次にどの駒でtoにある駒を取るのかを返す。
    template <PieceType PT = PAWN> FORCE_INLINE
    PieceType nextAttacker(const Board& b, const Square to, __m256i* knights, __m256i* no_knights,
        __m256i& ray, const Turn turn, bool& promote)
    {
        constexpr bool IsTGold = isTGold(PT);
        const Piece P = PT | turn;

        // toに利きのある駒を見つける
        // cf. http://denou.jp/tournament2016/img/PR/takotto.pdf
        __m256i attacker = PT == KNIGHT ? knights[turn] : no_knights[turn];
        __m256i tmp = _mm256_set1_epi8(toPieceBit(P));
        __m256i mask = _mm256_cmpeq_epi8(attacker, tmp);
        uint32_t movemask = _mm256_movemask_epi8(mask);
        Square from;

        // PTがTGOLDの場合、PieceBitだけではどの成金なのかが判定できないので実際の配列を探す。
        if (IsTGold && movemask)
        {
            do {
                from = pop<RAY, false>(movemask, to);
            } while (b.piece(from) != P && (movemask &= movemask - 1));
        }

        // 相手側の攻め駒の中に、指定した駒種が存在しない場合は、次の駒種を探しに行く。
        if (!movemask)
            return NEXT_ATTACKER[PT] == KING ? KING : nextAttacker<NEXT_ATTACKER[PT]>(b, to, knights, no_knights, ray, turn, promote);
        
        // 相手側の攻め駒の中に、指定した駒種が存在する場合は戻り値はその駒種で決定
        else
        {
            uint64_t index = bsf64(movemask);

            // attackersを更新
            if (PT == KNIGHT)
                knights[turn] = _mm256_andnot_si256(MASK_PACKED_BIT_INDICES[index], knights[turn]);
            else
                ray = _mm256_andnot_si256(MASK_PACKED_BIT_INDICES[index], ray);

            if (PT == PAWN || PT == LANCE || PT == KNIGHT)
                promote = canPromote(turn, to);

            else if (isNoPromotable(PT))
                promote = false;

            else
            {
                if (canPromote(turn, to))
                    promote = true;
                else
                    promote = canPromote(turn, DESTINATION[index][to]);
            }
            
            return PT;
        }
    }
}

// mのSEE >= s を返す。s以上であることがわかった時点でこの関数を抜けるので、通常のSEEよりも高速。
// 成りは考慮しない。
bool Board::seeGe2(const Move m, const Score s) const
{
    assert(isOK(m));
    Score balance;
    Turn stm = ~turn();
    Square to = toSq(m);
    __m256i neighbor = bb_.pack<NEIGHBOR>(to);
    __m256i ray = bb_.pack<RAY>(to);

    // 駒打ち
    if (isDrop(m))
    {
        // そこに敵の利きがないなら取り合いが起こらない。
        if (!::existAttacker(stm, to, neighbor, ray))
            return SCORE_ZERO >= s;

        balance = SCORE_ZERO;
    }

    // 駒打ちではない
    else
    {
        // fromが空いたことによって敵の利きが通るかもしれない
        const Square from = fromSq(m);

        if (movedPieceType(m) == KNIGHT)
            neighbor = _mm256_andnot_si256(squareMask<NEIGHBOR>(to, from), neighbor);
        else
            ray = _mm256_andnot_si256(squareMask<RAY>(to, from), ray);

        balance = captureScore(capturePieceType(m));

        if (isPromote(m))
            balance += promoteScore(movedPieceType(m));

        // 敵の利きがないなら、そこで取り合いは終わり。
        if (!::existAttacker(stm, to, neighbor, ray))
            return balance >= s;
    }

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

    // no_knightsだけ取得を遅らせる。
    __m256i knights[TURN_MAX] = { ::attackers<KNIGHTS>(BLACK, to, neighbor), ::attackers<KNIGHTS>(WHITE, to, neighbor) };
    __m256i no_knights[TURN_MAX];
    no_knights[stm] = ::attackers<NO_KNIGHTS>(stm, to, ray);

    do {
        // 次に相手がどの駒で取るのか。
        Score c = captureScore(next_victim);
        next_victim = nextAttacker(*this, to, knights, no_knights, ray, stm, promote);
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

        no_knights[stm] = ::attackers<NO_KNIGHTS>(stm, to, ray);

        // 玉で取る手が発生したら、それが合法であれば取り返せるし、合法でなければ取り返せない。
        if (next_victim == KING)
            return (relative_turn == (!isZero(knights[stm]) || !isZero(no_knights[stm])));

        relative_turn = !relative_turn;

        // pin aware seeにしたいのだが、コストが高い。

        // 駒の利きがないなら、そこで取り合いは終わり。このときは取り返せなかったときのbalance >= sの論理を返せば良い。
    } while (!isZero(knights[stm]) || !isZero(no_knights[stm]));

    return relative_turn;
}

// self側がfromにある駒pcをtoに動かしたときにksqにいるenemy玉が詰みならtrueを返す。
template <Turn T, bool IsDrop, bool IsKnight = false> FORCE_INLINE
bool isMateCheck(const Board& b, const Byteboard& bb, Square from, Square to, Square ksq, Piece moved_after_pc,
    __m256i neighbor_to, __m256i ray_to = _mm256_setzero_si256())
{
    constexpr Turn self = T;
    constexpr Turn enemy = ~T;
    bool mate = false;
    PieceBit apb = toPieceBit(moved_after_pc);

    // 王手である
    assert(givesEffect<T>(apb, ksq, to));

    auto tbb = const_cast<Byteboard*>(&bb);
    auto from_pb = tbb->pieceBit(from);
    auto to_pb = tbb->pieceBit(to);

    tbb->setPieceBit(to, apb);

    if (!IsDrop)
        tbb->setPieceBit(from, PB_EMPTY);
        
    // neighbor_toがあるのはIsKnightの時、または桂馬が成る手の時
    // ray_toがあるのは!IsKnightの時
    if (!IsKnight)
    {
        assert(!isZero(ray_to));

        if (!IsDrop)
            ray_to = _mm256_andnot_si256(squareMask<RAY>(to, from), ray_to);

        if (isZero(neighbor_to))
            neighbor_to = bb.pack<NEIGHBOR>(to);

        else if (!IsDrop)
            neighbor_to = _mm256_andnot_si256(squareMask<NEIGHBOR>(to, from), neighbor_to);
    }
    else
    {
        assert(!isZero(neighbor_to));
        ray_to = bb.pack<RAY>(to);

        if (!IsDrop)
            neighbor_to = _mm256_andnot_si256(squareMask<NEIGHBOR>(to, from), neighbor_to);
    }
        
    // toにselfの利きがある
    // enemy玉が逃げる場所がない
    if (IsKnight || existAttacker(self, to, neighbor_to, ray_to))
    {
        if (IsDrop)
        {
            // 玉以外の駒で取り返すことができない
            if (!b.canPieceCapture2(enemy, to, ksq, b.state()->slider_blockers[enemy], neighbor_to, ray_to)
                && !b.canKingEscape2<T>(ksq, to))
                mate = true;
        }
        else
        {
            // 両王手、または玉以外の駒で取り返すことができない
            // この移動によりself玉が素抜かれない
            if ((isDiscoveredCheck<IsKnight>(b.state()->slider_blockers[enemy], from, to, ksq)
                || !b.canPieceCapture2(enemy, to, ksq, sliderBlockers(ksq, enemy, bb.pack<RAY>(ksq)), neighbor_to, ray_to))
                && !b.canKingEscape2<T>(ksq, to) 
                && !isDiscoveredCheck<IsKnight>(b.state()->slider_blockers[self], from, to, b.kingSquare(self)))
                mate = true;
        }
    }

    tbb->setPieceBit(to, to_pb);

    if (!IsDrop)
        tbb->setPieceBit(from, from_pb);

    return mate;
}

template <Turn T, PieceType PT> inline bool Board::mateCheck(Square to, Square ksq)
{
    bb_.setPieceBit(to, toPieceBit(T | PT));
    bool mate = (PT == KNIGHT ? true : ::existAttacker(T, to, bb_))
        && !canKingEscape2<T>(ksq, to)
        && !canPieceCapture2(~T, to, ksq);
    bb_.setPieceBit(to, PB_EMPTY);

    return mate;
}

template <Turn T, PieceType PT, bool Promote> inline bool Board::mateCheck(Square from, Square to, Square ksq)
{
    constexpr Piece ToPC = Promote ? T | (PT | PieceType(PROMOTED)) : (T | PT);
    assert(bb_.pieceBit(from) == toPieceBit(T | PT));
    bb_.setPieceBit(from, PB_EMPTY);
    PieceBit to_pb = bb_.pieceBit(to);
    bb_.setPieceBit(to, toPieceBit(ToPC));

    bool mate = (PT == KNIGHT && !Promote ? true : ::existAttacker(T, to, bb_))
        && !canKingEscape2<T>(ksq, to)
        && (::isDiscoveredCheck(st_->slider_blockers[~T], from, to, ksq)
            || !canPieceCapture2(~T, to, ksq, ::sliderBlockers(ksq, ~T, bb_.pack<RAY>(ksq))))
        && !::isDiscoveredCheck(st_->slider_blockers[T], from, to, kingSquare(T));

    bb_.setPieceBit(to, to_pb);
    bb_.setPieceBit(from, toPieceBit(T | PT));

    return mate;
}

#if 1
// この局面が自分の手番だとして、1手で相手玉が詰むかどうか。詰むならその指し手を返す。
// 詰まないならMOVE_NONEを返す。ksqは敵玉の位置。
template <Turn T> 
Move Board::mate1ply2(const Square ksq) const
{
    // 自玉に王手がかかっていたらここでは相手玉の1手詰めは調べない。そういうときにこの関数を読んではならない。
    // 厳密には先手玉に王手がかかっていても王手駒をとりかえせば1手詰めだったりするが、それはここでは調べないことにする。
    assert(!inCheck());

    constexpr Turn self = T;
    constexpr Turn enemy = ~T;
    constexpr PieceBit TPB_GOLD = T == BLACK ? PB_GOLD : PB_GOLD | PB_ENEMY;
    const Hand h = hand(self);
    int hand_num = 0;
    Piece hands[6];
    uint32_t notself32bit = ~turn32(state()->king_neighbor[enemy], self);

    if (h.exists(LANCE))  hands[hand_num++] = self | LANCE; 
    if (h.exists(SILVER)) hands[hand_num++] = self | SILVER;
    if (h.exists(GOLD))   hands[hand_num++] = self | GOLD;
    if (h.exists(BISHOP)) hands[hand_num++] = self | BISHOP;
    if (h.exists(ROOK))   hands[hand_num++] = self | ROOK;

    //constexpr PackType Checkable = self == BLACK ? CHECKABLE_BLACK : CHECKABLE_WHITE;
    //__m256i checkable = bb_.pack<Checkable>(ksq);
    //Rank r = self == BLACK ? rankOf(ksq) : inverse(rankOf(ksq));
    //checkable = checkable & MASK_PACKED_BIT_CHECKABLE[r] & turnMask(checkable, self);

    //if (!isZero(checkable))
    //{
    //    uint32_t m = to32bit(checkable);

    //    while (m)
    //    {
    //        Square from = pop<Checkable>(m, ksq);
    //        std::cout << *this << piece(from) << " " << pretty(from) << std::endl;
    //        Piece p = piece(from);
    //        Bitboard target = state()->check_sq[typeOf(p)] & attackAll(p, from, bbOccupied());
    //        do {
    //            Square to = target.firstOne();

    //            isMateCheck<T, false, false>(b, bb, from, to, ksq, )
    //    }
    //}

    // 桂馬
    uint32_t e = PIECE_EFFECT[enemy | KNIGHT][ksq] & notself32bit;

    while (e)
    {
        Square to = pop<NEIGHBOR>(e, ksq);
        __m256i neighbor_to = bb_.pack<NEIGHBOR>(to);
        __m256i attack = attackBit<KNIGHTS>(self, to, neighbor_to);

        if (!isZero(attack))
        {
            uint32_t mask = to32bit(attack);

            do {
                Square from = pop<NEIGHBOR>(mask, to);
                Piece p = piece(from);

                if (isMateCheck<T, false, true>(*this, bb_, from, to, ksq, p, neighbor_to))
                    return makeMove(from, to, p, piece(to), false);
            } while (mask);
        }

        if (piece(to) == EMPTY)
            if (h.exists(KNIGHT))
                if (isMateCheck<T, true, true>(*this, bb_, SQ_MAX, to, ksq, self | KNIGHT, neighbor_to))
                    return makeDrop(self | KNIGHT, to);
    }

    e = PIECE_EFFECT[KING][ksq] & notself32bit;

    // 桂馬以外
    while (e)
    {
        int i = bsf64(e);
        assert(i < 12);
        Square to = pop<NEIGHBOR>(e, ksq);
        __m256i ray_to = bb_.pack<RAY>(to);
        __m256i neighbor_to = _mm256_setzero_si256();
        bool can_promote_to = canPromote(self, to);

        // 桂馬が成ることで王手になるなら桂馬の王手を生成する。
        if (can_promote_to && givesEffect<T>(TPB_GOLD, ksq, to))
        {
            neighbor_to = bb_.pack<NEIGHBOR>(to);
            __m256i attack = attackBit<KNIGHTS>(self, to, neighbor_to);

            if (!isZero(attack))
            {
                uint32_t mask = to32bit(attack);

                do {
                    Square from = pop<NEIGHBOR>(mask, to);
                    if (isMateCheck<T, false>(*this, bb_, from, to, ksq, self | PRO_KNIGHT, neighbor_to, ray_to))
                        return makeMove(from, to, self | KNIGHT, piece(to), true);
                } while (mask);
            }

            attack = attackBit<NO_KNIGHTS>(self, to, ray_to);

            if (!isZero(attack))
            {
                uint32_t mask = to32bit(attack);

                do {
                    Square from = pop<RAY>(mask, to);
                    Piece p = piece(from);
                    Piece moved_after_pc = isNoPromotable(p) ? p : promotePiece(p);
                    if (isMateCheck<T, false>(*this, bb_, from, to, ksq, moved_after_pc, neighbor_to, ray_to))
                        return makeMove(from, to, p, piece(to), !isNoPromotable(p));
                } while (mask);
            }
        }
        else
        {
            __m256i attack = ::attackers<NO_KNIGHTS, true>(self, to, ray_to) & MASK_PACKED_BIT_NOKNIGHTS_EFFECTABLE[self][i];

            if (!isZero(attack))
            {
                uint32_t mask = to32bit(attack);

                do {
                    Square from = pop<RAY>(mask, to);
                    Piece p = piece(from);

                    if (isMateCheck<T, false>(*this, bb_, from, to, ksq, p, neighbor_to, ray_to))
                        return makeMove(from, to, p, piece(to), false);

                    // fromからtoに移動したときに成れるのなら、なったときに王手になるかどうかを確かめる。
                    if (canPromote(self, from)
                        && !isNoPromotable(p)
                        && givesEffect<T>(toPieceBit(promotePiece(p)), ksq, to)
                        && isMateCheck<T, false>(*this, bb_, from, to, ksq, promotePiece(p), neighbor_to, ray_to))
                        return makeMove(from, to, p, piece(to), true);
                } while (mask);
            }
        }

        if (piece(to) == EMPTY)
            for (int i = 0; i < hand_num; i++)
                if (givesEffect<T>(toPieceBit(hands[i]), ksq, to)
                    && isMateCheck<T, true>(*this, bb_, SQ_MAX, to, ksq, hands[i], neighbor_to, ray_to))
                    return makeDrop(hands[i], to);
    }

    return MOVE_NONE;
}
#else
// この局面が自分の手番だとして、1手で相手玉が詰むかどうか。詰むならその指し手を返す。
// 詰まないならMOVE_NONEを返す。ksqは敵玉の位置。
template <Turn T>
Move Board::mate1ply2(const Square ksq) const
{
    // 自玉に王手がかかっていたらここでは相手玉の1手詰めは調べない。そういうときにこの関数を読んではならない。
    // 厳密には先手玉に王手がかかっていても王手駒をとりかえせば1手詰めだったりするが、それはここでは調べないことにする。
    assert(!inCheck());

    constexpr Turn self = T;
    constexpr Turn enemy = ~T;
    constexpr PieceBit TPB_GOLD = T == BLACK ? PB_GOLD : PB_GOLD | PB_ENEMY;
    const Hand h = hand(self);
    int hand_num = 0;
    Piece hands[6];
    uint32_t notself32bit = ~turn32(state()->king_neighbor[enemy], self);

    if (h.exists(LANCE))  hands[hand_num++] = self | LANCE;
    if (h.exists(SILVER)) hands[hand_num++] = self | SILVER;
    if (h.exists(GOLD))   hands[hand_num++] = self | GOLD;
    if (h.exists(BISHOP)) hands[hand_num++] = self | BISHOP;
    if (h.exists(ROOK))   hands[hand_num++] = self | ROOK;

    //constexpr PackType Checkable = self == BLACK ? CHECKABLE_BLACK : CHECKABLE_WHITE;
    //__m256i checkable = bb_.pack<Checkable>(ksq);
    //Rank r = self == BLACK ? rankOf(ksq) : inverse(rankOf(ksq));
    //checkable = checkable & MASK_PACKED_BIT_CHECKABLE[r] & turnMask(checkable, self);

    //if (!isZero(checkable))
    //{
    //    uint32_t m = to32bit(checkable);

    //    while (m)
    //    {
    //        Square from = pop<Checkable>(m, ksq);
    //        std::cout << *this << piece(from) << " " << pretty(from) << std::endl;
    //    }
    //}

    // 桂馬
    uint32_t e = PIECE_EFFECT[enemy | KNIGHT][ksq] & notself32bit;

    while (e)
    {
        Square to = pop<NEIGHBOR>(e, ksq);
        __m256i neighbor_to = bb_.pack<NEIGHBOR>(to);
        __m256i attack = attackBit<KNIGHTS>(self, to, neighbor_to);

        if (!isZero(attack))
        {
            uint32_t mask = to32bit(attack);

            do {
                Square from = pop<NEIGHBOR>(mask, to);
                Piece p = piece(from);

                if (isMateCheck<T, false, true>(*this, bb_, from, to, ksq, p, neighbor_to))
                    return makeMove(from, to, p, piece(to), false);
            } while (mask);
        }

        if (piece(to) == EMPTY)
            if (h.exists(KNIGHT))
                if (isMateCheck<T, true, true>(*this, bb_, SQ_MAX, to, ksq, self | KNIGHT, neighbor_to))
                    return makeDrop(self | KNIGHT, to);
    }

    e = PIECE_EFFECT[KING][ksq] & notself32bit;

    // 桂馬以外
    while (e)
    {
        Square to = pop<NEIGHBOR>(e, ksq);
        __m256i ray_to = bb_.pack<RAY>(to);
        __m256i neighbor_to = _mm256_setzero_si256();
        bool can_promote_to = canPromote(self, to);

        // 桂馬が成ることで王手になるなら桂馬の王手を生成する。
        if (can_promote_to && givesEffect<T>(TPB_GOLD, ksq, to))
        {
            neighbor_to = bb_.pack<NEIGHBOR>(to);
            __m256i attack = attackBit<KNIGHTS>(self, to, neighbor_to);

            if (!isZero(attack))
            {
                uint32_t mask = to32bit(attack);

                do {
                    Square from = pop<NEIGHBOR>(mask, to);
                    if (isMateCheck<T, false>(*this, bb_, from, to, ksq, self | PRO_KNIGHT, neighbor_to, ray_to))
                        return makeMove(from, to, self | KNIGHT, piece(to), true);
                } while (mask);
            }
        }

        __m256i attack = attackBit<NO_KNIGHTS, true>(self, to, ray_to);

        if (!isZero(attack))
        {
            uint32_t mask = to32bit(attack);

            do {
                Square from = pop<RAY>(mask, to);
                Piece p = piece(from);

                if (givesEffect<T>(toPieceBit(p), ksq, to)
                    && isMateCheck<T, false>(*this, bb_, from, to, ksq, p, neighbor_to, ray_to))
                    return makeMove(from, to, p, piece(to), false);

                // fromからtoに移動したときに成れるのなら、なったときに王手になるかどうかを確かめる。
                if ((can_promote_to || canPromote(self, from))
                    && !isNoPromotable(p)
                    && givesEffect<T>(toPieceBit(promotePiece(p)), ksq, to)
                    && isMateCheck<T, false>(*this, bb_, from, to, ksq, promotePiece(p), neighbor_to, ray_to))
                    return makeMove(from, to, p, piece(to), true);
            } while (mask);
        }

        if (piece(to) == EMPTY)
            for (int i = 0; i < hand_num; i++)
                if (givesEffect<T>(toPieceBit(hands[i]), ksq, to)
                    && isMateCheck<T, true>(*this, bb_, SQ_MAX, to, ksq, hands[i], neighbor_to, ray_to))
                    return makeDrop(hands[i], to);
    }

    return MOVE_NONE;
}

#endif

// mate1plyのラッパー
Move Board::mate1ply2() const
{
    const Turn t = turn();
    const Square ksq = kingSquare(~t);
    Move ret = t == BLACK ? mate1ply2<BLACK>(ksq) : mate1ply2<WHITE>(ksq);

    assert(verify());
    assert(ret == MOVE_NONE || (pseudoLegal(ret) && legal(ret)));
    return ret;
}

// sqから王手されているenemy側の玉が逃げられるかどうかを返す。
template <Turn T>
bool Board::canKingEscape2(const Square ksq, const Square sq) const
{
    constexpr Turn self = T;
    constexpr Turn enemy = ~T;
    constexpr PieceBit king = T == BLACK ? PB_KING | PB_ENEMY : PB_KING;
    auto tbb = const_cast<Byteboard*>(&bb_);
    tbb->setPieceBit(ksq, PB_EMPTY);

    // 王手駒を王で取る手は無効。(sqに駒が利いていることが前提なので）
    auto effect = PIECE_EFFECT[KING][ksq] & ~turn32(state()->king_neighbor[enemy], enemy);

    // 移動先に駒の利きがある間調べていく。
    while (effect)
    {
        Square to = pop<NEIGHBOR>(effect, ksq);

        if (sq != to && !::existAttacker(self, to, bb_))
        {
            tbb->setPieceBit(ksq, king);
            return true;
        }
    }

    tbb->setPieceBit(ksq, king);
    return false;
}

bool Board::givesCheck2(Move m) const
{
    const Turn enemy = ~turn();
    const Square to = toSq(m);
    const Square ksq = kingSquare(enemy);

    // 持ち駒打ちなら打った駒が王手になっているかを判定して終わり。
    if (isDrop(m))
        return givesEffect(~enemy, movedPiece(m), ksq, to, state()->reach_sliders);

    // 直接王手
    Piece p = movedPieceTo(m);

    if (typeOf(p) != KING && givesEffect(~enemy, p, ksq, to, state()->reach_sliders))
        return true;

    // fromにある駒を動かすと相手玉に対して王手になる
    if (::isDiscoveredCheck(state()->slider_blockers[enemy], fromSq(m), to, ksq))
        return true;

    return false;
}

// 任意のmoveに対して、Legalかどうかを判定する。
// moveが32bitなので、比較的単純なチェックですんでいる。
bool Board::pseudoLegal2(const Move move) const
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

        // 桂馬の王手に対して持ち駒打ちを生成することはできない。
        if (!isZero(state()->checker_knights))
            return false;

        // ※レアケース
        if (!isZero(state()->checker_no_knights))
        {
            uint32_t nm = to32bit(state()->checker_no_knights);

            // 両王手に対して持ち駒を打つ手を生成することはできない。
            if (nm & (nm - 1))
                return false;

            // 王手されている && 駒打ち　== 合駒のはず。
            const Square ksq = kingSquare(self);
            const Square csq = pop<RAY>(nm, ksq);

            // 玉と、王手した駒との間に駒を打っていない。
            if ((square32<RAY>(ksq, to) & MASK_32BIT_SQ_TO_BASE[ksq][csq]) == 0)
                return false;
        }

        // 二歩
        if (hp_from == PAWN && existPawnFile(self, to))
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
        if (piece(to) && turnOf(piece(to)) == self)
            return false;

        const Square from = fromSq(move);
        const Piece pc_from = movedPiece(move);

        // 成れない駒を成る手は生成しないので、引っかからないはず。
        assert(!isPromote(move) || typeOf(pc_from) < GOLD);

        // fromにある駒がpt_fromではない
        if (piece(from) != pc_from)
            return false;

        // fromの駒が飛び駒で、かつtoに利いていない。飛び駒でなければ必ずtoに利いている。
        if (isSlider(pc_from) && (to32bit(bb_.pack<RAY>(from)) & MASK_32BIT_SQ_TO_BASE[from][to]))
            return false;

        if (inCheck())
        {
            if (typeOf(pc_from) != KING)// 王以外の駒を移動させたとき
            {
                uint32_t km = to32bit(state()->checker_knights);
                uint32_t nm = to32bit(state()->checker_no_knights);

                // 両王手なので玉が動かなければならない。
                if ((km && nm) || (nm & nm - 1))
                    return false;

                const Square ksq = kingSquare(self);
                const Square csq = km ? pop<NEIGHBOR>(km, ksq) : pop<RAY>(nm, ksq);

                // 移動合いもしくは王手駒を取る手以外はだめ。
                if (to != csq && !(square32<RAY>(ksq, to) & MASK_32BIT_SQ_TO_BASE[ksq][csq]))
                    return false;
            }
        }
    }

    return true;
}

template <bool NotAlwaysDrop>
bool Board::legal2(const Move move) const
{
    if (!NotAlwaysDrop && isDrop(move))
    {
        // 打ち歩詰めの判定
        if (movedPieceType(move) == PAWN
            && toSq(move) + (turn() == BLACK ? SQ_U : SQ_D) == kingSquare(~turn())
            && isPawnDropCheckMate2(turn(), toSq(move), kingSquare(~turn())))
            return false;

        // 他の駒打ちはpseudoLegalで正しいことを確認済み
        return true;
    }

    assert(!isDrop(move));
    assert(capturePieceType(move) != KING);
    assert(piece(fromSq(move)) == movedPiece(move));

    const Turn self = turn();
    const Square ksq = kingSquare(self);
    const Square from = fromSq(move);

    // 玉の移動先に相手の駒の利きがあれば、合法手ではないのでfalse
    if (typeOf(piece(from)) == KING)
    {
        auto pb = bb_.pieceBit(ksq);
        const_cast<Byteboard*>(&bb_)->setPieceBit(ksq, PB_EMPTY);
        bool exist_attacker = ::existAttacker(~self, toSq(move), bb_);
        const_cast<Byteboard*>(&bb_)->setPieceBit(ksq, pb);
        return !exist_attacker;
    }

    // 玉以外の駒の移動 fromにある駒がtoに移動したとき自分の玉に王手がかかるかどうか。
    return !(::isDiscoveredCheck(state()->slider_blockers[self], from, toSq(move), ksq));
}

template bool Board::legal2<false>(const Move move) const;
template bool Board::legal2<true>(const Move move) const;

// 入玉勝ちかどうかを判定
bool Board::isDeclareWin2() const
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
    const Square ksq = kingSquare(us);

    if (us == BLACK && rankOf(ksq) > RANK_3 || us == WHITE && rankOf(ksq) < RANK_7)
        return false;

    // 宣言する者の敵陣にいる駒は、玉を除いて10枚以上である。
    __m256i field = bb_.turnField(~us);
    field &= turnMask(field, us);
    uint32_t emask = to32bit(field);
    int count = popCount(emask);

    if (count < 11)
        return false;

    //「宣言する者の敵陣にいる駒」と「宣言する者の持ち駒」を対象に前述の点数計算を行ったとき、宣言する者が先手の場合28点以上、後手の場合27点以上ある。
    // 大駒の枚数が分かれば小駒の枚数もわかる。
    __m256i bigmask = _mm256_set1_epi8(PB_BISHOP | PB_ROOK);
    field &= bigmask;
    emask = to32bit(field);
    int big = popCount(emask);
    int small = count - big - 1;
    Hand h = hand(us);

    const int score = (big + h.count(ROOK) + h.count(BISHOP)) * 5
        + small + h.count(PAWN) + h.count(LANCE) + h.count(KNIGHT) + h.count(SILVER) + h.count(GOLD);

    if (score < (us == BLACK ? 28 : 27))
        return false;

    return true;
}

template <Turn T, MoveType MT>
MoveStack* generateMoveFrom(const Byteboard& bb, Square from, Piece p, MoveStack* mlist)
{
    if (isSlider(p))
    {

    }
    else
    {

    }

    return mlist;
}

template <MoveType MT>
MoveStack* generateMoveFrom(const Byteboard& bb, Square from, Piece p, MoveStack* mlist)
{
    return turnOf(p) == BLACK ? generateMoveFrom<BLACK, MT>(bb, from, p, mlist) : generateMoveFrom<WHITE, MT>(bb, from, p, mlist);
}

namespace
{
    // 盤上の駒を動かす手を生成
    template <Turn T, MoveType MT>
    MoveStack* generateOnBoard(const Board& b, MoveStack* mlist)
    {
        const Byteboard& bb = b.getByteboard();
        uint64_t bit = b.getExistsPiece(T);

        do {
            PieceNo i = (PieceNo)bsf64(bit);
            Square from = b.pieceSquare(i);
            assert(from != SQ_MAX);
            Piece p = b.piece(from);
            assert(turnOf(p) == T);
            mlist = generateMoveFrom<T, MT>(bb, from, p, mlist);
        } while (bit &= bit - 1);

        return mlist;
    }

    // 持ち駒を打つ手を生成。最大で7枚一気に生成できるので持ち駒の多い局面なら高速だと思う。
    template <Turn T>
    Move* generateDropMove(const Board& b, __m256i target, Move* mlist)
    {
        constexpr Move dp = makeDrop(T | PAWN);
        constexpr Move dl = makeDrop(T | LANCE);
        constexpr Move dk = makeDrop(T | KNIGHT);
        constexpr uint32_t p = Hand::pMask(), l = Hand::lMask(), k = Hand::kMask();

        int hand_num = 0;
        Move pc[7];
        Square to;
        const uint32_t hand_bit = b.hand(T).existsBit();
        if (hand_bit & (1 << 0)) { pc[hand_num++] = makeDrop(T | BISHOP); }
        if (hand_bit & (1 << 1)) { pc[hand_num++] = makeDrop(T | ROOK); }
        if (hand_bit & (1 << 5)) { pc[hand_num++] = makeDrop(T | SILVER); }
        if (hand_bit & (1 << 6)) { pc[hand_num++] = makeDrop(T | GOLD); }

        uint16_t tmp = b.state()->nifu_flags[T];
        __m256i target_p = target & PAWN_DROPABLE_MASK[T][tmp];
        __m256i target_l = target & LANCE_DROPABLE_MASK[T];
        __m256i target_k = target & KNIGHT_DROPABLE_MASK[T];
        __m256i move256;

#define MAKE_TO *mlist++ = Move(pc[i] | to)
#define MAKE_256_2 move256 = _mm256_set_epi32(0, 0, 0, 0, 0, 0, pc[1], pc[0]);
#define MAKE_256_3 move256 = _mm256_set_epi32(0, 0, 0, 0, 0, pc[2], pc[1], pc[0]);
#define MAKE_256_4 move256 = _mm256_set_epi32(0, 0, 0, 0, pc[3], pc[2], pc[1], pc[0]);
#define MAKE_256_5 move256 = _mm256_set_epi32(0, 0, 0, pc[4], pc[3], pc[2], pc[1], pc[0]);
#define MAKE_256_6 move256 = _mm256_set_epi32(0, 0, pc[5], pc[4], pc[3], pc[2], pc[1], pc[0]);
#define MAKE_256_7 move256 = _mm256_set_epi32(0, pc[6], pc[5], pc[4], pc[3], pc[2], pc[1], pc[0]);
#define MAKE_YMM(offset) { *(__m256i*)mlist = _mm256_or_si256(MOVE_TO[to], move256); mlist += offset; };

        switch (hand_bit & Hand::plkMask())
        {
            // 歩香桂を持っていない場合
        case 0:
            switch (hand_num)
            {
            case 0: break; // それ以外もないなら終わり。
            case 1: FBT(target, to, UROL1(MAKE_TO)); break;
            case 2: MAKE_256_2; FBT(target, to, MAKE_YMM(2)); break;
            case 3: MAKE_256_3; FBT(target, to, MAKE_YMM(3)); break;
            case 4: MAKE_256_4; FBT(target, to, MAKE_YMM(4)); break;
            default: UNREACHABLE;
            }
            break;

            // 歩を持っている場合
        case p:
        {
            __m256i target_o = target ^ target_p;
            pc[hand_num] = dp;

            switch (hand_num)
            {
            case 0: FBT(target_p, to, UROL1(MAKE_TO)); break;
            case 1: MAKE_256_2; FBT(target_p, to, MAKE_YMM(2)); FBT(target_o, to, UROL1(MAKE_TO)); break;
            case 2: MAKE_256_3; FBT(target_p, to, MAKE_YMM(3)); FBT(target_o, to, MAKE_YMM(2)); break;
            case 3: MAKE_256_4; FBT(target_p, to, MAKE_YMM(4)); FBT(target_o, to, MAKE_YMM(3)); break;
            case 4: MAKE_256_5; FBT(target_p, to, MAKE_YMM(5)); FBT(target_o, to, MAKE_YMM(4)); break;
            default: UNREACHABLE;
            }
            break;
        }

        // 香車を持っている場合
        case l:
        {
            __m256i target_o = target ^ target_l;
            pc[hand_num] = dl;

            switch (hand_num)
            {
            case 0: FBT(target_l, to, UROL1(MAKE_TO)); break;
            case 1: MAKE_256_2; FBT(target_l, to, MAKE_YMM(2)); FBT(target_o, to, UROL1(MAKE_TO)); break;
            case 2: MAKE_256_3; FBT(target_l, to, MAKE_YMM(3)); FBT(target_o, to, MAKE_YMM(2)); break;
            case 3: MAKE_256_4; FBT(target_l, to, MAKE_YMM(4)); FBT(target_o, to, MAKE_YMM(3)); break;
            case 4: MAKE_256_5; FBT(target_l, to, MAKE_YMM(5)); FBT(target_o, to, MAKE_YMM(4)); break;
            default: UNREACHABLE;
            }
            break;
        }

        case k:
        {
            __m256i target_o = target ^ target_k;
            pc[hand_num] = dk;

            switch (hand_num)
            {
            case 0: FBT(target_k, to, UROL1(MAKE_TO)); break;
            case 1: MAKE_256_2; FBT(target_k, to, MAKE_YMM(2)); FBT(target_o, to, UROL1(MAKE_TO)); break;
            case 2: MAKE_256_3; FBT(target_k, to, MAKE_YMM(3)); FBT(target_o, to, MAKE_YMM(2)); break;
            case 3: MAKE_256_4; FBT(target_k, to, MAKE_YMM(4)); FBT(target_o, to, MAKE_YMM(3)); break;
            case 4: MAKE_256_5; FBT(target_k, to, MAKE_YMM(5)); FBT(target_o, to, MAKE_YMM(4)); break;
            default: UNREACHABLE;
            }
            break;
        }

        case p | l:
        {
            // 歩も香も打てない場所
            __m256i target_o = target ^ target_l;

            // 香車だけが打てる場所
            target_l ^= target_p;

            pc[hand_num] = dl;
            pc[hand_num + 1] = dp;

            switch (hand_num)
            {
            case 0: MAKE_256_2; FBT(target_p, to, MAKE_YMM(2)); FBT(target_l, to, UROL1(MAKE_TO)); break;
            case 1: MAKE_256_3; FBT(target_p, to, MAKE_YMM(3)); FBT(target_l, to, MAKE_YMM(2)); FBT(target_o, to, UROL1(MAKE_TO)); break;
            case 2: MAKE_256_4; FBT(target_p, to, MAKE_YMM(4)); FBT(target_l, to, MAKE_YMM(3)); FBT(target_o, to, MAKE_YMM(2)); break;
            case 3: MAKE_256_5; FBT(target_p, to, MAKE_YMM(5)); FBT(target_l, to, MAKE_YMM(4)); FBT(target_o, to, MAKE_YMM(3)); break;
            case 4: MAKE_256_6; FBT(target_p, to, MAKE_YMM(6)); FBT(target_l, to, MAKE_YMM(5)); FBT(target_o, to, MAKE_YMM(4)); break;
            default:
                UNREACHABLE;
            }
            break;
        }

        case k | p:
        {
            __m256i target_o = target ^ (target_p | target_k);
            __m256i target_pk = target_p & target_k;
            target_p ^= target_pk;
            target_k ^= target_pk;
            pc[hand_num] = dk;
            pc[hand_num + 1] = dp;

            switch (hand_num)
            {
            case 0:
                MAKE_256_2;
                FBT(target_pk, to, MAKE_YMM(2));
                FBT(target_k, to, UROL1(MAKE_TO)); pc[0] = dp;
                FBT(target_p, to, UROL1(MAKE_TO));
                break;
            case 1:
                MAKE_256_3;
                FBT(target_pk, to, MAKE_YMM(3));
                FBT(target_k, to, MAKE_YMM(2)); pc[1] = dp; MAKE_256_2;
                FBT(target_p, to, MAKE_YMM(2));
                FBT(target_o, to, UROL1(MAKE_TO));
                break;
            case 2:
                MAKE_256_4;
                FBT(target_pk, to, MAKE_YMM(4));
                FBT(target_k, to, MAKE_YMM(3)); pc[2] = dp; MAKE_256_3;
                FBT(target_p, to, MAKE_YMM(3));
                FBT(target_o, to, MAKE_YMM(2));
                break;
            case 3:
                MAKE_256_5;
                FBT(target_pk, to, MAKE_YMM(5));
                FBT(target_k, to, MAKE_YMM(4)); pc[3] = dp; MAKE_256_4;
                FBT(target_p, to, MAKE_YMM(4));
                FBT(target_o, to, MAKE_YMM(3));
                break;
            case 4:
                MAKE_256_6;
                FBT(target_pk, to, MAKE_YMM(6));
                FBT(target_k, to, MAKE_YMM(5)); pc[4] = dp; MAKE_256_5;
                FBT(target_p, to, MAKE_YMM(5));
                FBT(target_o, to, MAKE_YMM(4));
                break;
            default:
                UNREACHABLE;
            }
            break;
        }

        case k | l:
        {
            __m256i target_o = target ^ (target_k | target_l);
            target_l ^= target_k;
            pc[hand_num] = dl;
            pc[hand_num + 1] = dk;

            switch (hand_num)
            {
            case 0:
                MAKE_256_2;
                FBT(target_k, to, MAKE_YMM(2));
                FBT(target_l, to, UROL1(MAKE_TO));
                break;
            case 1:
                MAKE_256_3;
                FBT(target_k, to, MAKE_YMM(3));
                FBT(target_l, to, MAKE_YMM(2));
                FBT(target_o, to, UROL1(MAKE_TO));
                break;
            case 2:
                MAKE_256_4;
                FBT(target_k, to, MAKE_YMM(4));
                FBT(target_l, to, MAKE_YMM(3));
                FBT(target_o, to, MAKE_YMM(2));
                break;
            case 3:
                MAKE_256_5;
                FBT(target_k, to, MAKE_YMM(5));
                FBT(target_l, to, MAKE_YMM(4));
                FBT(target_o, to, MAKE_YMM(3));
                break;
            case 4:
                MAKE_256_6;
                FBT(target_k, to, MAKE_YMM(6));
                FBT(target_l, to, MAKE_YMM(5));
                FBT(target_o, to, MAKE_YMM(4));
                break;
            default:
                UNREACHABLE;
            }
            break;
        }

        case p | k | l:
        {
            // 歩と桂馬と香車すべてが打てる場所
            __m256i target_pkl = target_p & target_k & target_l;

            // 歩と香車だけ打てる場所(=2段目）
            __m256i target_pl = target_p & (target_l ^ target_k);

            // 桂馬と香車だけ打てる場所
            __m256i target_kl = target_k & (target_l ^ target_p);

            // 歩も香車も桂馬も打てない場所(=1段目)
            __m256i target_o = target ^ target_l;

            // 香車だけ打てる場所
            target_l ^= (target_p | target_k);

            pc[hand_num] = dl;
            pc[hand_num + 1] = dp;
            pc[hand_num + 2] = dk;

            switch (hand_num)
            {
            case 0:
                MAKE_256_3;
                FBT(target_pkl, to, MAKE_YMM(3));
                FBT(target_pl, to, MAKE_YMM(2)); pc[1] = dk; MAKE_256_2;
                FBT(target_kl, to, MAKE_YMM(2));
                FBT(target_l, to, UROL1(MAKE_TO));
                break;
            case 1:
                MAKE_256_4;
                FBT(target_pkl, to, MAKE_YMM(4));
                FBT(target_pl, to, MAKE_YMM(3)); pc[2] = dk; MAKE_256_3;
                FBT(target_kl, to, MAKE_YMM(3));
                FBT(target_l, to, MAKE_YMM(2));
                FBT(target_o, to, UROL1(MAKE_TO));
                break;
            case 2:
                MAKE_256_5;
                FBT(target_pkl, to, MAKE_YMM(5));
                FBT(target_pl, to, MAKE_YMM(4)); pc[3] = dk; MAKE_256_4;
                FBT(target_kl, to, MAKE_YMM(4));
                FBT(target_l, to, MAKE_YMM(3));
                FBT(target_o, to, MAKE_YMM(2));
                break;
            case 3:
                MAKE_256_6;
                FBT(target_pkl, to, MAKE_YMM(6));
                FBT(target_pl, to, MAKE_YMM(5)); pc[4] = dk; MAKE_256_5;
                FBT(target_kl, to, MAKE_YMM(5));
                FBT(target_l, to, MAKE_YMM(4));
                FBT(target_o, to, MAKE_YMM(3));
                break;
            case 4:
                MAKE_256_7;
                FBT(target_pkl, to, MAKE_YMM(7));
                FBT(target_pl, to, MAKE_YMM(6)); pc[5] = dk; MAKE_256_6;
                FBT(target_kl, to, MAKE_YMM(6));
                FBT(target_l, to, MAKE_YMM(5));
                FBT(target_o, to, MAKE_YMM(4));
                break;
            default:
                UNREACHABLE;
            }
            break;
        }

        default:
            UNREACHABLE;
        }

        return mlist;
    }

    // toに動かす手を生成する。持ち駒打ちは生成しない。
    // ALLがtrueなら飛車角歩、香の2段目への不成も生成する。falseなら飛車角歩は必ず成る。
    template <Turn T, bool All>
    Move* generateMoveTo(const Board& b, Square to, Move* mlist)
    {
        constexpr Turn self = T;
        constexpr Rank TRank3 = T == BLACK ? RANK_3 : RANK_7;
        const Byteboard& bb = b.getByteboard();
        Piece c = b.piece(to);
        bool can_promote_to = canPromote(self, to);
        uint32_t km = to32bit(attackBit<KNIGHTS>(self, to, bb.pack<NEIGHBOR>(to)));

        while (km)
        {
            auto from = pop<NEIGHBOR>(km, to);
            Piece p = self | KNIGHT;

            if (isBehind(self, RANK_2, to))
                *mlist++ = makeMove(from, to, p, c, false);
            if (can_promote_to)
                *mlist++ = makeMove(from, to, p, c, true);
        }

        uint32_t nm = to32bit(attackBit<NO_KNIGHTS, false>(self, to, bb.pack<RAY>(to)));

        while (nm)
        {
            auto from = pop<RAY>(nm, to);
            Piece p = b.piece(from);

            // 成れない駒はここですべて処理
            if (isNoPromotable(p))
                *mlist++ = makeMove(from, to, p, c, false);

            else
            {
                if (can_promote_to || canPromote(self, from))
                {
                    *mlist++ = makeMove(from, to, p, c, true);

                    if (All)
                    {
                        if (isBehind(self, RANK_1, to)
                            || typeOf(p) == SILVER
                            || typeOf(p) == BISHOP
                            || typeOf(p) == ROOK)
                            *mlist++ = makeMove(from, to, p, c, false);
                    }
                    else
                    {
                        if (typeOf(p) == SILVER
                            || (typeOf(p) == LANCE && rankOf(to) == TRank3))
                            *mlist++ = makeMove(from, to, p, c, false);
                    }
                }

                else
                    *mlist++ = makeMove(from, to, p, c, false);
            }
        }

        return mlist;
    }

    // 王手回避手
    template <Turn T, bool All>
    Move* generateEvasion(const Board& b, Move* mlist)
    {
        constexpr Turn self = T;
        constexpr Turn enemy = ~T;
        constexpr Piece king = T | KING;

        const Square ksq = b.kingSquare(T);
        __m256i neighbor = b.state()->king_neighbor[T], ray = b.state()->king_ray[T];

        // 玉が動ける場所 == 王手している駒の利きがない && 自分の駒がない && 玉の動ける場所
        auto king_effect = PIECE_EFFECT[KING][ksq] & ~turn32(neighbor, self);

        // 移動先に敵の利きがあるか調べずに指し手を生成する。
        while (king_effect)
        {
            auto to = pop<NEIGHBOR>(king_effect, ksq);
            *mlist++ = makeMove(ksq, to, king, b.piece(to), false);
        }

        uint32_t km = to32bit(attackBit<KNIGHTS>(enemy, ksq, neighbor));
        uint32_t sm = to32bit(attackBit<NO_KNIGHTS, true>(enemy, ksq, ray));

        // 両王手なら、玉を移動するしか回避方法はない。玉の移動は生成したので、ここで終了
        if ((km && sm) || (sm & sm - 1))
            return mlist;

        // 王手している駒を玉以外で取る手の生成。pinされているかどうかは、move_pickerかsearchで調べる
        // 王手している駒が近接駒の場合
        if (km)
            mlist = generateMoveTo<T, All>(b, pop<NEIGHBOR, false>(km, ksq), mlist);

        // 王手している駒が飛び駒の場合
        else
        {
            // 王手駒を取る手を生成
            Square to = pop<RAY, false>(sm, ksq);
            mlist = generateMoveTo<T, All>(b, to, mlist);

            // 王手駒と玉の間に駒を移動させる手を生成。
            __m256i target = MASK_BITBOARD_SQ_TO_BASE[ksq][to];

            if (!isZero(target))
            {
                FBT(target, to, { mlist = (generateMoveTo<T, All>(b, to, mlist)); });
                mlist = generateDropMove<T>(b, target, mlist);
            }
        }

        return mlist;
    }

    // 合法手をすべてを生成。Allがtrueなら飛車角歩の不成、香車の2段目への不成も生成する。
    template <Turn T, bool All>
    Move* generateNoEvasion(const Board& b, Move* mlist)
    {
        constexpr Rank TRank3 = T == BLACK ? RANK_3 : RANK_7;
        const Byteboard& bb = b.getByteboard();
        uint64_t bit = b.getExistsPiece(T);

        do {
            PieceNo i = (PieceNo)bsf64(bit);
            Square from = b.pieceSquare(i);
            assert(from != SQ_MAX);
            Piece p = b.piece(from);
            assert(turnOf(p) == T);

            bool is_slider = isSlider(p);
            uint32_t mask;

            if (is_slider)
            {
                __m256i packs = bb.pack<RAY>(from);
                mask = ~turn32(packs, T) & strike32<RAY>(packs, from);
            }
            else
            {
                __m256i packs = bb.pack<NEIGHBOR>(from);
                mask = ~turn32(packs, T);
            }

            uint32_t e = PIECE_EFFECT[p][from] & mask;

            while (e)
            {
                Square to = (is_slider ? pop<RAY> : pop<NEIGHBOR>)(e, from);
                Piece c = b.piece(to);

                if (isNoPromotable(p))
                    *mlist++ = makeMove(from, to, p, c, false);

                else
                {
                    PieceType pt = typeOf(p);

                    if (All)
                    {
                        // 2段目より↑の桂不成、1段目への歩、香の不成以外を生成する。
                        if (isBehind(T, RANK_2, to)
                            || (pt != KNIGHT && isBehind(T, RANK_1, to))
                            || (pt != KNIGHT && pt != LANCE && pt != PAWN))
                            *mlist++ = makeMove(from, to, p, c, false);
                    }

                    if (canPromote(T, to) || canPromote(T, from))
                    {
                        *mlist++ = makeMove(from, to, p, c, true);

                        if (!All)
                        {
                            // 不成を生成するのは銀と3段目への桂馬、香車のみ
                            if (pt == SILVER
                                || ((pt == KNIGHT || pt == LANCE) && rankOf(to) == TRank3))
                                *mlist++ = makeMove(from, to, p, c, false);
                        }
                    }
                    else if (!All)
                        *mlist++ = makeMove(from, to, p, c, false);
                }
            }
        } while (bit &= bit - 1);

        return generateDropMove<T>(b, bb.emptys(), mlist);
    }

    template <Turn T, bool All>
    Move* generateLegal(const Board& b, Move* mlist)
    {
        Move* curr = mlist;
        mlist = b.inCheck() ? generateEvasion<T, All>(b, mlist) : generateNoEvasion<T, All>(b, mlist);

        // 玉の移動による自殺手と、pinされている駒の移動による自殺手を削除
        while (curr != mlist)
        {
            if (!b.pseudoLegal(*curr) || !b.legal(*curr))
                *curr = *(--mlist);
            else
                ++curr;
        }

        return mlist;
    }

    // 駒を取らない王手生成。
    template <Turn T>
    MoveStack* generateQuietCheck(const Board& b, MoveStack* mlist)
    {
        constexpr Turn self = T, enemy = ~T;
        constexpr Square T_DOWN = T == BLACK ? SQ_D : SQ_U;
        const Square ksq = b.kingSquare(enemy);
        const Hand h = b.hand(self);
        uint32_t empty_nei = to32bitRev(b.state()->king_neighbor[enemy]);
        uint32_t strike = _mm256_movemask_epi8(b.state()->reach_sliders);
        uint32_t empty_ray = strike ^ (to32bit(b.state()->king_ray[enemy]) & strike);

        if (h.exists(PAWN)
            && !b.existPawnFile(self, ksq)
            && (empty_nei & PIECE_EFFECT[enemy | PAWN][ksq]))
            mlist++->move = makeDrop(T | PAWN, ksq + T_DOWN);

        if (h.exists(KNIGHT))
            for (uint32_t e = empty_nei & PIECE_EFFECT[enemy | KNIGHT][ksq]; e;)
                mlist++->move = makeDrop(T | KNIGHT, pop<NEIGHBOR>(e, ksq));

        if (h.exists(LANCE))
            for (uint32_t e = empty_ray & PIECE_EFFECT[enemy | LANCE][ksq]; e;)
                mlist++->move = makeDrop(T | LANCE, pop<RAY>(e, ksq));

        if (h.exists(SILVER))
            for (uint32_t e = empty_nei & PIECE_EFFECT[enemy | SILVER][ksq]; e;)
                mlist++->move = makeDrop(T | SILVER, pop<NEIGHBOR>(e, ksq));

        if (h.exists(GOLD))
            for (uint32_t e = empty_nei & PIECE_EFFECT[enemy | GOLD][ksq]; e;)
                mlist++->move = makeDrop(T | GOLD, pop<NEIGHBOR>(e, ksq));

        if (h.exists(BISHOP))
            for (uint32_t e = empty_ray & PIECE_EFFECT[enemy | BISHOP][ksq]; e;)
                mlist++->move = makeDrop(T | BISHOP, pop<RAY>(e, ksq));

        if (h.exists(ROOK))
            for (uint32_t e = empty_ray & PIECE_EFFECT[enemy | ROOK][ksq]; e;)
                mlist++->move = makeDrop(T | ROOK, pop<RAY>(e, ksq));

#if 1
        constexpr PieceBit TPB_GOLD = T == BLACK ? PB_GOLD : PB_GOLD | PB_ENEMY;
        const Byteboard& bb_ = b.getByteboard();

        // 桂馬
        uint32_t e = PIECE_EFFECT[enemy | KNIGHT][ksq] & empty_nei;

        while (e)
        {
            Square to = pop<NEIGHBOR>(e, ksq);
            __m256i neighbor_to = bb_.pack<NEIGHBOR>(to);
            __m256i attack = attackBit<KNIGHTS>(self, to, neighbor_to);

            if (!isZero(attack))
            {
                uint32_t mask = to32bit(attack);

                do {
                    Square from = pop<NEIGHBOR>(mask, to);
                    mlist++->move = makeMove(from, to, self | KNIGHT, EMPTY, false);
                } while (mask);
            }
        }

        e = PIECE_EFFECT[enemy | KING][ksq] & empty_nei;

        // 桂馬以外
        while (e)
        {
            int i = bsf64(e);
            Square to = pop<NEIGHBOR>(e, ksq);
            __m256i ray_to = bb_.pack<RAY>(to);
            __m256i neighbor_to = _mm256_setzero_si256();
            bool can_promote_to = canPromote(self, to);

            // 桂馬が成ることで王手になるなら桂馬の王手を生成する。
            if (can_promote_to && givesEffect<T>(TPB_GOLD, ksq, to))
            {
                neighbor_to = bb_.pack<NEIGHBOR>(to);
                __m256i attack = attackBit<KNIGHTS>(self, to, neighbor_to);

                if (!isZero(attack))
                {
                    uint32_t mask = to32bit(attack);

                    do {
                        Square from = pop<NEIGHBOR>(mask, to);
                        mlist++->move = makeMove(from, to, self | KNIGHT, EMPTY, true);
                    } while (mask);
                }

                attack = attackBit<NO_KNIGHTS>(self, to, ray_to);

                if (!isZero(attack))
                {
                    uint32_t mask = to32bit(attack);

                    do {
                        Square from = pop<RAY>(mask, to);
                        Piece p = b.piece(from);
                        mlist++->move = makeMove(from, to, p, EMPTY, !isNoPromotable(p));
                    } while (mask);
                }
            }
            else
            {
                __m256i attack = attackers<NO_KNIGHTS, true>(self, to, ray_to);// &MASK_PACKED_BIT_NOKNIGHTS_EFFECTABLE[self][i];

                if (!isZero(attack))
                {
                    uint32_t mask = to32bit(attack);

                    do {
                        Square from = pop<RAY>(mask, to);
                        Piece p = b.piece(from);
                        bool can_promote_from = canPromote(self, from);

                        if (givesEffect<T>(toPieceBit(p), ksq, to)
                            && (isNoPromotable(p)
                                || typeOf(p) == SILVER
                                || ((typeOf(p) == PAWN || typeOf(p) == ROOK || typeOf(p) == BISHOP) && (!can_promote_to && !can_promote_from))))
                        {
                            mlist++->move = makeMove(from, to, p, EMPTY, false);
                        }

                        // fromからtoに移動したときに成れるのなら、なったときに王手になるかどうかを確かめる。
                        if ((can_promote_to || canPromote(self, from))
                            && !isNoPromotable(p)
                            && givesEffect<T>(toPieceBit(promotePiece(p)), ksq, to))
                            mlist++->move = makeMove(from, to, p, EMPTY, true);
                    } while (mask);
                }
            }
        }

        // TODO:空き王手
        // e = b.state()->slider_blockers[enemy] & turn32(b.state()->king_ray[enemy], self);

#endif
        return mlist;
    }

} // namespace

MoveStack* generateQuietCheck(const Board& b, MoveStack* mlist)
{
    return b.turn() == BLACK ? generateQuietCheck<BLACK>(b, mlist) : generateQuietCheck<WHITE>(b, mlist);
}

Move* generateDrop(const Board& b, Move* mlist)
{
    const Byteboard& bb = b.getByteboard();
    return b.turn() == BLACK ? generateDropMove<BLACK>(b, bb.emptys(), mlist) : generateDropMove<WHITE>(b, bb.emptys(), mlist);
}

template <MoveType MT>
MoveStack* generateOnBoard(const Board& b, MoveStack* mlist)
{
    return b.turn() == BLACK ? generateOnBoard<BLACK, MT>(b, mlist) : generateOnBoard<WHITE, MT>(b, mlist);
}

template MoveStack* generateOnBoard<CAPTURE_PLUS_PAWN_PROMOTE>(const Board& b, MoveStack* mlist);
template MoveStack* generateOnBoard<NO_CAPTURE_MINUS_PAWN_PROMOTE>(const Board& b, MoveStack* mlist);
template MoveStack* generateOnBoard<NO_EVASIONS>(const Board& b, MoveStack* mlist);

Move* generateEvasion(const Board& b, Move* mlist)
{
    return b.turn() == BLACK ? generateEvasion<BLACK, false>(b, mlist) : generateEvasion<WHITE, false>(b, mlist);
}

Move* generateRecapture(const Board& b, Move* mlist, const Square sq)
{
    return b.turn() == BLACK ? generateMoveTo<BLACK, false>(b, sq, mlist) : generateMoveTo<WHITE, false>(b, sq, mlist);
}

template <bool All>
Move* generateLegal(const Board& b, Move* mlist)
{
    return b.turn() == BLACK ? generateLegal<BLACK, All>(b, mlist) : generateLegal<WHITE, All>(b, mlist);
}
template Move* generateLegal<true>(const Board& b, Move* mlist);
template Move* generateLegal<false>(const Board& b, Move* mlist);

// fromからDIR[i]の方向に移動したときに盤面内ならtrue。dには12方向のいずれかが入る。
bool isOK(Square from, int i)
{
    assert(0 <= i && i < 12);
    int ofile[12] = { 0, 0, 1, -1, 1, -1, 1, -1, -1, 1, -1, 1, };
    int orank[12] = { -1, 1, 0, 0, -1, 1, 1, -1, -2, -2, 2, 2, };
    return isOK(rankOf(from) + orank[i]) && isOK(fileOf(from) + ofile[i]);
}

// fromからDIR_CHECKABLE[i]の方向に移動したときに盤面内ならtrue。
bool isOK2(Square from, Turn t, int i)
{
    assert(0 <= i && i < 32);
    int ofile[32] = { -2, -1,  0,  1,  2,
                      -2, -1,  0,  1,  2,
                      -2, -1,      1,  2,
                      -2, -1,  0,  1,  2,
                      -2, -1,  0,  1,  2,
                      -2, -1,  0,  1,  2,
                      -2,      0,      2 };
    int orank[32] = { -2, -2, -2, -2, -2,
                      -1, -1, -1, -1, -1,
                       0,  0,      0,  0,
                       1,  1,  1,  1,  1,
                       2,  2,  2,  2,  2,
                       3,  3,  3,  3,  3,
                       4,      4,      4 };
    int f = t == BLACK ? ofile[i] : -ofile[i];
    int r = t == BLACK ? orank[i] : -orank[i];
    return isOK(rankOf(from) + r) && isOK(fileOf(from) + f);
}

// pがtoに存在できるかどうかを返す。
bool isOK(Piece p, Square to)
{
    if (p == B_PAWN || p == B_LANCE)
    {
        if (rankOf(to) == RANK_1)
            return false;
    }
    else if (p == B_KNIGHT)
    {
        if (rankOf(to) <= RANK_2)
            return false;
    }
    else if (p == W_PAWN || p == W_LANCE)
    {
        if (rankOf(to) == RANK_9)
            return false;
    }
    else if (p == W_KNIGHT)
    {
        if (rankOf(to) >= RANK_8)
            return false;
    }

    return true;
}

// pがfromからtoに移動できるならtrue。飛び駒の場合は障害物は考えない。
// 行き所のない駒ならfalseを返す。
// 探索中に呼び出すわけではないので高速でなくとも問題はない。
// 将来的にBitboardは解雇するかもしれないので、Bitboardを使わずに実装。
bool isOK(Square from, Square to, Piece p, bool promote)
{
    // pがtoに存在できないなら全部false。
    if (!promote && !isOK(p, to))
        return false;

    // pieceが動ける場所をbitで表す。
    // 0000 0000 0000 1111 1111 1111 1111 1111
    // 左上　右下　左下　右上　←　→　↓　↑　後桂左 後桂右　先桂左 先桂右 左上　右下　左下　右上　←　→　↓　↑
    uint32_t piece_effect[PIECE_MAX] =
    {
        0x00000000, 0x000f0000, 0x0000f000,
        0x00000001, 0x00001000, 0x00000300, 0x000000f1, 0x0000009f, 0x000000ff,
        0x000f000f, 0x0000f0f0, 0x0000009f, 0x0000009f, 0x0000009f, 0x0000009f, 0x00000000,
        0x00000000, 0x000f0000, 0x0000f000,
        0x00000002, 0x00002000, 0x00000c00, 0x000000f2, 0x0000006f, 0x000000ff,
        0x000f000f, 0x0000f0f0, 0x0000006f, 0x0000006f, 0x0000006f, 0x0000006f,
    };

    for (auto i = piece_effect[p]; i; i &= i - 1)
    {
        int bid = bsf64(i);
        Square d = DIR[bid % 12];

        if (isOK(from, bid % 12))
        {
            if (from + d == to)
                return true;

            if (bid >= 12)
                for (auto now = from + d; isOK(now, bid % 12); now += d)
                    if (now + d == to)
                        return true;
        }
    }

    return false;
}

void initByteboardTables()
{
    // sqに対して利いている12マスのPieceBitのBit。飛び駒はここでは考えない。
    uint8_t near_mask[TURN_MAX][12] =
    {
        { 0x40, 0x61, 0x40, 0x40, 0x20, 0x60, 0x60, 0x20, 0x00, 0x00, 0x02, 0x02, },
        { 0x61, 0x40, 0x40, 0x40, 0x60, 0x20, 0x20, 0x60, 0x02, 0x02, 0x00, 0x00, }
    };

    // sqに対して利いている飛び駒のPieceBit。
    uint8_t sliders_mask[TURN_MAX][8] =
    {
        { 0x10, 0x14, 0x10, 0x10, 0x08, 0x08, 0x08, 0x08 },
        { 0x14, 0x10, 0x10, 0x10, 0x08, 0x08, 0x08, 0x08 },
    };

    // sqの周囲8マスに対して利いている飛び駒+近接駒のPieceBit。
    uint8_t no_knights_mask[TURN_MAX][8] =
    {
        { 0x50, 0x75, 0x50, 0x50, 0x28, 0x68, 0x68, 0x28 },
        { 0x75, 0x50, 0x50, 0x50, 0x68, 0x28, 0x28, 0x68 },
    };

    uint8_t checkable_base[] =
    {
        0x20, 0x00, 0x60, 0x00, 0x20,
        0x00, 0x40, 0x00, 0x40, 0x00,
        0x60, 0x40, /***/ 0x40, 0x60,
        0x60, 0x00, 0x00, 0x00, 0x60,
        0x60, 0x60, 0x61, 0x60, 0x60,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x02,       0x02,       0x02,
    };

    uint8_t checkable_promote_base[] =
    {
        0x20, 0x20, 0x60, 0x20, 0x20,
        0x20, 0x40, 0x20, 0x40, 0x20,
        0x60, 0x40, /***/ 0x40, 0x60,
        0x60, 0x03, 0x00, 0x03, 0x60,
        0x62, 0x61, 0x63, 0x61, 0x62,
        0x02, 0x02, 0x02, 0x02, 0x02,
        0x02,       0x02,       0x02,
    };

    uint8_t pro_ranks[] =
    {
        4, 4, 4, 4, 4,
        3, 3, 3, 3, 3,
        2, 2, 2, 2, 2,
        2, 2, 2, 2, 2,
        2, 1, 2, 1, 2,
        1, 1, 1, 1, 1,
        0,    0,    0
    };

    for (Turn t : Turns)
        for (int i = 0; i < 12; i++)
            MASK_PACKED_BIT_NOKNIGHTS_EFFECTABLE[t][i] = _mm256_set1_epi8(no_knights_mask[t][i]);

    for (auto r : Ranks)
        for (int i = 0; i < 32; i++)
        {
            if (r <= pro_ranks[i])
                MASK_PACKED_BIT_CHECKABLE[r].m256i_u8[i] = checkable_promote_base[i];
            else
                MASK_PACKED_BIT_CHECKABLE[r].m256i_u8[i] = checkable_base[i];
        }

    const RelationType rts[4] = { DIRECT_FILE, DIRECT_RANK, DIRECT_DIAG1, DIRECT_DIAG2 };

    for (auto from : Squares)
    {
        for (auto to : Squares)
            SQUARE_RELATIONSHIP[from][to] = DIRECT_MISC;

        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 2; j++)
                for (Square now = from, d = DIR[i * 2 + j]; isOK(now, i * 2 + j); now += d)
                    SQUARE_RELATIONSHIP[from][now + d] = rts[i];
    }

    for (Turn t : Turns)
        for (int i = 0; i < 12; i++)
        {
            MASK_PACKED_BIT_NEIGHBORS[t].m256i_u8[i] = near_mask[t][i];

            if (i >= 8)
                MASK_PACKED_BIT_KNIGHTS[t].m256i_u8[i] = near_mask[t][i];
        }

    for (int i = 0; i < 32; i++)
    {
        for (auto sq : Squares)
            DESTINATION[i][sq] = Square(-1);

        MASK_PACKED_BIT_INDICES[i].m256i_u8[i] = -1;
    }

    for (auto sq : Squares)
    {
        // shuffleは最上位bitが1になっているところを0にするので、初期値は0xffが最適。
        // 初期値を0にする処理は、グローバル変数が0初期化されていることを利用して省く。
        SHUFFLE_NEIGHBOR[sq] = Byteboard::one();
        SHUFFLE_CROSS[sq] = Byteboard::one();
        SHUFFLE_DIAG[sq] = Byteboard::one();

        for (auto t : Turns)
        {
            SHUFFLE_CHECKABLE_LOW[t][sq] = Byteboard::one();
            SHUFFLE_CHECKABLE_HIGH[t][sq] = Byteboard::one();
        }

        MOVE_TO[sq] = _mm256_set1_epi32(sq);

        for (int i = 0; i < 12; i++)
            if (isOK(sq, i))
            {
                Square now = sq + DIR[i];
                int sq_idx = now / 16;
                int sh_idx = now % 16;
                SHUFFLE_NEIGHBOR[sq].setPieceBit(Square(sq_idx * 16 + i), PieceBit(sh_idx));
            }

        for (auto t : Turns)
        {
            for (int i = 0; i < 16; i++)
            {
                if (isOK2(sq, t, i))
                {
                    Square now = t == BLACK ? sq + DIR_CHECKABLE[i] : sq - DIR_CHECKABLE[i];
                    int sq_idx = now / 16;
                    int sh_idx = now % 16;
                    SHUFFLE_CHECKABLE_LOW[t][sq].setPieceBit(Square(sq_idx * 16 + i), PieceBit(sh_idx));
                }

                if (isOK2(sq, t, i + 16))
                {
                    Square now = t == BLACK ? sq + DIR_CHECKABLE[i + 16] : sq - DIR_CHECKABLE[i + 16];
                    int sq_idx = now / 16;
                    int sh_idx = now % 16;
                    SHUFFLE_CHECKABLE_HIGH[t][sq].setPieceBit(Square(sq_idx * 16 + i), PieceBit(sh_idx));
                }
            }
        }

        // file, rank, diag1, diag2の4回
        for (int i = 0; i < 4; i++)
        {
            auto dst = (i < 2 ? SHUFFLE_CROSS : SHUFFLE_DIAG);

            // fileなら↑と↓、rankなら→と←というように２方向ずつ調べる。
            for (int j = 0, bid = 0; j < 2; j++)
            {
                if (isOK(sq, i * 2 + j))
                    MASK_PACKED_BIT_RAY_ONE[sq].m256i_u64[i] |= 1ULL << (bid * 8);

                for (Square now = sq, d = DIR[i * 2 + j]; isOK(now, i * 2 + j); bid++)
                {
                    now += d;
                    int shuffle_id = bid + i * 8;
                    int sq_idx = now / 16;
                    int sh_idx = now % 16;
                    dst[sq].setPieceBit(Square(sq_idx * 16 + shuffle_id - (i < 2 ? 0 : 16)), PieceBit(sh_idx));
                    DESTINATION[shuffle_id][sq] = now;

                    for (Turn t : Turns)
                    {
                        MASK_PACKED_BIT_SLIDERS[t][sq].m256i_u8[shuffle_id] = sliders_mask[t][i * 2 + j];
                        MASK_PACKED_BIT_NO_KNIGHTS[t][sq].m256i_u8[shuffle_id] =
                            (now - d == sq ? no_knights_mask : sliders_mask)[t][i * 2 + j];
                    }

                    if (j == 1)
                        MASK_PACKED_BIT_RAY_SEPARATER[sq].m256i_u64[i] |= 0xffULL << (bid * 8);

                    MASK_PACKED_BIT_RAY_USE[sq].m256i_u64[i] |= 0xffULL << (bid * 8);
                }
            }
        }

        // 駒の利きと指し手配列の初期化
        for (Turn t : Turns)
            for (PieceType pt : PieceTypes)
            {
                Piece p = pt | t;

                if (isSlider(p))
                {
                    for (int i = 0; i < 4; i++)
                        for (int j = 0, bid = 0; j < 2; j++)
                            for (Square now = sq, d = DIR[i * 2 + j]; isOK(now, i * 2 + j); bid++, now += d)
                                if (isOK(sq, now + d, p, true))
                                    PIECE_EFFECT[p][sq] |= 1 << (bid + i * 8);

                    uint32_t e = PIECE_EFFECT[p][sq];

                    if (pt == HORSE)
                        e &= 0x0000ffff;
                    else if (pt == DRAGON)
                        e &= 0xffff0000;

                    for (int i = 0; e; i++)
                    {
                        int shift = pt == BISHOP || pt == DRAGON ? i + 16 : i;

                        if (e & (1 << shift))
                        {
                            Square to = pop<RAY>(e, sq);

                            if (pt == LANCE)
                            {
                                if (canPromote(turnOf(p), to))
                                    MOVE16_PIECE_BASE[t][pt][sq].m256i_u16[i + 8] = makeMove16(sq, to, true);

                                if (isBehind(t, RANK_2, to))
                                    MOVE16_PIECE_BASE[t][pt][sq].m256i_u16[i] = makeMove16(sq, to, false);
                            }
                            else
                            {
                                // 飛車角の不成は生成しない。
                                if (!isNoPromotable(p) && (canPromote(turnOf(p), sq) || canPromote(turnOf(p), to)))
                                    MOVE16_PIECE_BASE[t][pt][sq].m256i_u16[i] = makeMove16(sq, to, true);

                                else
                                    MOVE16_PIECE_BASE[t][pt][sq].m256i_u16[i] = makeMove16(sq, to, false);
                            }
                        }
                    }
                }
                else
                {
                    for (int i = 0, s = 0; i < 12; i++)
                        if (isOK(sq, i) && isOK(sq, sq + DIR[i], p, true))
                            PIECE_EFFECT[p][sq] |= 1 << i;

                    uint32_t e = PIECE_EFFECT[p][sq];

                    for (int i = 0; e; i++)
                    {
                        int shift = pt == KNIGHT ? i + 8 : i;

                        if (e & (1 << shift))
                        {
                            Square to = pop<NEIGHBOR>(e, sq);

                            if (!isNoPromotable(p) && (canPromote(turnOf(p), sq) || canPromote(turnOf(p), to)))
                                MOVE16_PIECE_BASE[t][pt][sq].m256i_u16[i + 8] = makeMove16(sq, to, true);

                            if (isOK(sq, to, p, false)
                                && (isNoPromotable(pt)
                                    || isBehind(t, RANK_3, to)
                                    || pt == SILVER
                                    || (pt == KNIGHT && isBehind(t, RANK_2, to))))
                                MOVE16_PIECE_BASE[t][pt][sq].m256i_u16[i] = makeMove16(sq, to, false);
                        }
                    }
                }
            }
    }

    for (auto base : Squares)
    {
        for (auto sq : Squares)
        {
            bool exist_sq = false;

            for (int i = 0; i < 32; i++)
            {
                if (DESTINATION[i][base] == sq)
                {
                    INDEX_SQ_ON_BASE_RAY[base][sq] = i;
                    exist_sq = true;
                }
            }

            if (!exist_sq)
                INDEX_SQ_ON_BASE_RAY[base][sq] = 32;

            exist_sq = false;

            for (int i = 0; i < 12; i++)
            {
                if (isOK(base, i) && base + DIR[i] == sq)
                {
                    INDEX_SQ_ON_BASE_NEAR[base][sq] = i;
                    exist_sq = true;
                }
            }

            if (!exist_sq)
                INDEX_SQ_ON_BASE_NEAR[base][sq] = 32;
        }

        INDEX_SQ_ON_BASE_RAY[base][SQ_MAX] = 32;
        INDEX_SQ_ON_BASE_NEAR[base][SQ_MAX] = 32;

        for (auto t : Turns)
        {
            for (int i = 0; i < 12; i++)
                if (isOK(base, i))
                    MASK_PIECEBIT_ATTACK_TO_KSQ[t][base][base + DIR[i]] = (PieceBit)near_mask[t][i];

            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 2; j++)
                    for (Square now = base, d = DIR[i * 2 + j]; isOK(now, i * 2 + j); now += d)
                        MASK_PIECEBIT_ATTACK_TO_KSQ[t][base][now + d] ^= (PieceBit)sliders_mask[t][i * 2 + j];
        }
    }

    for (auto base : Squares)
        for (auto sq : Squares)
        {
            if (relation(base, sq) != DIRECT_MISC)
            {
                for (int i = 0; i < 8; i++)
                    for (auto now = base; isOK(now, i); now += DIR[i])
                        if (now + DIR[i] == sq)
                        {
                            for (now = base + DIR[i]; now != sq; now += DIR[i])
                            {
                                MASK_BITBOARD_SQ_TO_BASE[base][sq].m256i_u32[now / 32] |= (1 << (now % 32));
                                MASK_32BIT_SQ_TO_BASE[base][sq] |= square32<RAY>(base, now);
                            }

                            break;
                        }
            }
        }

    for (auto t : Turns)
    {
        for (int p = 0; p < 512; p++)
        {
            for (File file : Files)
                if (!(p & (1 << file)))
                    for (int s = file; s < SQ_MAX; s += FILE_MAX)
                        PAWN_DROPABLE_MASK[t][p].m256i_u32[s / 32] |= 1 << (s % 32);

            if (t == BLACK)
                for (int s = 0; s < 9; s++)
                    PAWN_DROPABLE_MASK[t][p].m256i_u32[0] &= ~(1 << s);
            else
                for (int s = SQ_MAX - 9; s < SQ_MAX; s++)
                    PAWN_DROPABLE_MASK[t][p].m256i_u32[2] &= ~(1 << (s - 64));
        }

        for (Rank r : Ranks)
            for (File f : Files)
            {
                Square s = sqOf(f, r);

                if (isBehind(t, RANK_1, r))
                    LANCE_DROPABLE_MASK[t].m256i_u32[s / 32] |= 1 << (s % 32);

                if (isBehind(t, RANK_2, r))
                    KNIGHT_DROPABLE_MASK[t].m256i_u32[s / 32] |= 1 << (s % 32);
            }
    }
}

Piece toPiece(PieceBit pb)
{
    for (Piece p = EMPTY; p <= W_PRO_SILVER; p++)
        if (toPieceBit(p) == pb)
            return p;

    return EMPTY;
}

std::string pretty(__m256i nei)
{
    std::stringstream ss;
    Piece p[12];

    auto po = [](Piece p)
    {
        std::stringstream ss;
        ss << (turnOf(p) == BLACK ? " " : "") << p;
        return ss.str();
    };

    for (int i = 0; i < 12; i++)
        p[i] = toPiece((PieceBit)nei.m256i_u8[i]);

    ss << po(p[0]) << "   " << po(p[1]) << std::endl
        << po(p[2]) << po(p[3]) << po(p[4]) << std::endl
        << po(p[5]) << " * " << po(p[6]) << std::endl
        << po(p[7]) << po(p[8]) << po(p[9]) << std::endl
        << po(p[10]) << "   " << po(p[11]) << std::endl << std::endl;

    return ss.str();
}

std::string pretty(__m256i sli, Square to)
{
    std::stringstream ss;
    PieceBit* p = (PieceBit*)sli.m256i_u8;
    Piece pcs[32];

    for (int i = 0; i < 32; i++)
        pcs[i] = toPiece(p[i]);

    for (auto r : Ranks)
    {
        for (auto f : Files)
        {
            auto sq = sqOf(f, r);

            if (sq == to)
            {
                ss << " * ";
                continue;
            }

            for (int i = 0; i < 32; i++)
            {
                auto d = DESTINATION[i][to];

                if (d == sq)
                {
                    if (turnOf(pcs[i]) == BLACK)
                        ss << " ";
                    ss << pcs[i];
                    goto END;
                }
            }

            ss << " □";
        END:;
        }

        ss << "\n";
    }
    ss << "\n";
    return ss.str();
}

#ifdef USE_BITBOARD
bool cantMove(Piece p, Square to)
{
    switch (p)
    {
    case B_PAWN: case B_LANCE:
        return mask(to) & mask(RANK_1);
    case W_PAWN: case W_LANCE:
        return mask(to) & mask(RANK_9);
    case B_KNIGHT:
        return mask(to) & frontMask(BLACK, RANK_3);
    case W_KNIGHT:
        return mask(to) & frontMask(WHITE, RANK_7);
    default:
        return false;
    }

    return false;
}

// 大きさは81*7+81*80*7くらい？
int allMoves(const Board& b, Move* mlist)
{
    // 考えられる合法、非合法な指し手を全通り試す。
    int i = 0;
    Turn t = b.turn();
    for (auto from : Squares)
    for (auto to : Squares)
    for (PieceType pt = BISHOP; pt < PIECETYPE_MAX; pt++)
    {
        Piece p = pt | t;

        if (pt >= BISHOP && pt < KING && !cantMove(p, to))
            mlist[i++] = makeDrop(p, to);

        // 指し手生成で生成されるはずのない手は除外する。駒の動きを守らないような手は生成されない。
        if ((attackAll(p, from, allZeroMask()) & to))
        {
            for (Piece c = EMPTY; c < PIECE_MAX; c++)
            {
                if (typeOf(c) == KING || c == SIGN_WHITE)
                    continue;

                if (!cantMove(p, to))
                    mlist[i++] = makeMove(from, to, p, c, false);

                if (!isNoPromotable(pt) && canPromote(t, from) && canPromote(t, to))
                    mlist[i++] = makeMove(from, to, p, c, true);
            }
        }
    }

    return i;
}


void byteboard_test(const Board& b)
{
    Byteboard bb = b.getByteboard();

    // isOKのテスト(OK)
#if 0
    for (auto from : Squares)
        for (auto to : Squares)
            for (auto t : Turns)
                for (auto pt : PieceTypes)
                    for (int i = 0; i < 2; i++)
                {
                    Piece p = pt | t;
                    if (isOK(from, to, p, i) != isOK2(from, to, p, i))
                        std::cout << "!\n";
                }
#endif

    // pack<**>のテスト(OK)
#if 0
    for (auto sq : Squares)
    {
        auto n = bb.pack<NEIGHBOR>(sq);

        for (int i = 0; i < 12; i++)
            if (isOK(sq, i))
            {
                Square to = sq + DIR[i];
                if (toPieceBit(b.piece(to)) != n.m256i_u8[i])
                    std::cout << "!\n";
            }

        n = bb.pack<RAY>(sq);

        for (int i = 0; i < 32; i++)
        {
            auto to = DESTINATION[i][sq];
            if (to == -1)
                continue;

            if (toPieceBit(b.piece(to)) != n.m256i_u8[i])
                std::cout << "!\n";
        }
    }
#endif
    // strikeMaskのテスト(OK)
    // turnMaskのテスト(OK)
#if 1
    for (auto sq : Squares)
    {
        auto ray = bb.pack<RAY>(sq);
        auto block = strikeMask<RAY>(ray, sq) & ray;

        for (int i = 0; i < 4; i++)
            for (int j = 0, bid = 0; j < 2; j++)
            {
                Square d = DIR[i * 2 + j];
                bool found_wall = false;

                for (Square now = sq; isOK(now, i * 2 + j); bid++)
                {
                    now += d;

                    if (!found_wall)
                    {
                        // 壁を見つけるまでは盤面配列と同じはず
                        if (bb.pieceBit(now) != block.m256i_u8[bid + i * 8])
                            std::cout << "!\n";
                        if (bb.pieceBit(now))
                            found_wall = true;
                    }
                    else
                    {
                        // 壁を見つけた後は何も入っていないはず
                        if (block.m256i_u8[bid + i * 8] != PB_EMPTY)
                            std::cout << "!\n";
                    }
                }
            }

        auto black = turnMask(ray, BLACK) & ray;
        auto white = turnMask(ray, WHITE) & ray;

        for (int i = 0; i < 32; i++)
        {
            if (ray.m256i_u8[i])
            {
                if (ray.m256i_u8[i] & (uint8_t)PB_ENEMY)
                {
                    if (white.m256i_u8[i] != ray.m256i_u8[i])
                        std::cout << "!\n";
                }
                else
                {
                    if (black.m256i_u8[i] != ray.m256i_u8[i])
                        std::cout << "!\n";
                }
            }
            else
            {
                if (white.m256i_u8[i] != ray.m256i_u8[i])                    std::cout << "!\n";
                if (black.m256i_u8[i] != ray.m256i_u8[i])                    std::cout << "!\n";
            }
        }
    }
#endif

    // existAttacerのテスト(OK)
#if 1
    for (auto t : Turns)
        for (auto sq : Squares)
        {
            __m256i neighbor = bb.pack<NEIGHBOR>(sq), ray = bb.pack<RAY>(sq);

            bool b1 = ::existAttacker(t, sq, neighbor, ray);
            bool b2 = b.existAttacker1(t, sq);
            if (b1 != b2)
            {
                std::cout << b << "sq = " << pretty(sq) << " t = " << t
                    << " b1 = " << b1 << " b2 = " << b2 << std::endl;
            }
        }
#endif

    // isDiscoveredCheckのテスト(OK)
    // sliderBlockersのテスト(OK)
#if 1
    {
        Turn t = b.turn();
        Square ksq = b.kingSquare(~t);
        __m256i ray = bb.pack<RAY>(ksq);
        uint32_t slider_blockers = b.state()->slider_blockers[~t];
        ray = b.state()->king_ray[~t];

        for (auto m : MoveList<LEGAL>(b))
        {
            if (b.givesCheck(m))
            {
                bool b1 = isDiscoveredCheck(slider_blockers, fromSq(m), toSq(m), ksq);
                bool b2 = b.isDiscoveredCheck(fromSq(m), toSq(m), ksq, b.state()->blockers_for_king[~t]);

                if (b1 != b2)
                {
                    std::cout << b << pretty(ray, ksq) << slider_blockers << b1 << b2 << pretty(m) << std::endl;
                }
            }
        }
    }
#endif

    // givesCheckのテスト(OK)
    // isPawnDropCheckMateのテスト(OK)
#if 1
    for (auto m : MoveList<LEGAL>(b))
    {
        bool b1 = b.givesCheck1(m);
        bool b2 = b.givesCheck2(m);

        if (b1 != b2)
        {
            std::cout << b1 << b2 << b << pretty(m) << std::endl;
            b1 = b.givesCheck1(m);
            b2 = b.givesCheck2(m);
        }

        if (b1 && isDrop(m) && movedPieceType(m) == PAWN)
        {
            b1 = b.isPawnDropCheckMate1(b.turn(), toSq(m), b.kingSquare(~b.turn()));
            b2 = b.isPawnDropCheckMate2(b.turn(), toSq(m), b.kingSquare(~b.turn()));

            if (b1 != b2)
            {
                std::cout << b1 << b2 << b << pretty(m) << std::endl;
                b1 = b.isPawnDropCheckMate1(b.turn(), toSq(m), b.kingSquare(~b.turn()));
                b2 = b.isPawnDropCheckMate2(b.turn(), toSq(m), b.kingSquare(~b.turn()));
            }
        }
    }
#endif

    // attackersのテスト（値見るだけ）
#if 0
    for (auto t : Turns)
        for (auto sq : Squares)
        {
            __m256i neighbor = bb.pack<NEIGHBOR>(sq), ray = bb.pack<RAY>(sq);
            __m256i neighbors = attackers<NEIGHBORS>(t, sq, neighbor);
            __m256i knights = attackers<KNIGHTS>(t, sq, neighbor);
            __m256i sliders = attackers<SLIDERS>(t, sq, ray);
            __m256i no_knights = attackers<NO_KNIGHTS>(t, sq, ray);
            std::cout << b << pretty(neighbor) << "\n" << pretty(ray, sq) << std::endl;
            std::cout << b 
                //<< "neighbors:\n" << pretty(neighbors)
                //<< "\nsliders:\n" << pretty(sliders, sq)
                << "\nknights:\n" << pretty(knights) 
                << "\nno_knights:\n" << pretty(no_knights, sq)
                << std::endl;

        }
#endif
    // canPieceCaptureのテスト(OK)
#if 1
    {
        const Turn t = b.turn();
        const Square ksq = b.kingSquare(t);
        for (auto sq : Squares)
        {
            bool b1 = b.canPieceCapture2(t, sq, ksq);
            bool b2 = b.canPieceCapture1(t, sq, ksq);
            if (b1 != b2)
            {
                std::cout << b1 << b2 << b << pretty(sq) << std::endl;
                b1 = b.canPieceCapture2(t, sq, ksq);
                b2 = b.canPieceCapture1(t, sq, ksq);
            }
        }
    }
#endif

    // inCheckのテスト
#if 0
    {
        bool b1 = b.inCheck();
        bool b2 = b.inCheck2();
        if (b1 != b2)
        {
            std::cout << b << b1 << b2;
        }
    }
#endif
#if 0
    {
        const Turn t = b.turn();
        __m256i mlist[39], *ml = mlist;
        Move16* m16 = (Move16*)mlist;
        MoveList<NO_CAPTURE_MINUS_PAWN_PROMOTE> caps(b);
        ml = generateOnBoard<NO_CAPTURE_MINUS_PAWN_PROMOTE>(b, mlist);
        auto msize = ml - mlist;

        for (int i = 0; i < msize * 16; i++)
        {
            if (m16[i] != MOVE_NONE)
            {
                Move m = move16ToMove(m16[i], b);

                if (b.legal(m))
                    if (!caps.contains(m) && !isPawnPromote(m) && movedPieceType(m) != LANCE)
                    {
                        std::cout << b << b.sfen() << " " << std::hex << m16[i] << " " << pretty(m) << std::endl;
                        ml = generateOnBoard<NO_CAPTURE_MINUS_PAWN_PROMOTE>(b, mlist);
                    }
            }
        }

        for (auto m : caps)
        {
            bool exist = false;

            for (int i = 0; i < msize * 16; i++)
                if (m16[i] != MOVE_NONE)
                    if (m == move16ToMove(m16[i], b))
                        exist = true;

            if (!exist)
            {
                std::cout << b << pretty(m) << std::endl;
                ml = generateOnBoard<NO_CAPTURE_MINUS_PAWN_PROMOTE>(b, mlist);
            }
        }

    }
#endif
    // make**のテスト
#if 0
    {

        const Turn t = b.turn();
        __m256i mlist[39], *ml = mlist;
        Move16* m16 = (Move16*)mlist;
        Move dlist[MAX_MOVES], *dl = dlist;
        MoveStack clist[MAX_MOVES], *cl = clist;

        if (b.inCheck())
        {
            dl = generateEvasion(b, dlist);
        }
        else
        {
            ml = generateOnBoard<NO_EVASIONS>(b, mlist);
            dl = generateDrop(b, dlist);
            cl = generateQuietCheck(b, clist);
        }

        auto msize = ml - mlist;
        auto dsize = dl - dlist;
        auto csize = cl - clist;

        MoveList<LEGAL_ALL> legal_alls(b);
        MoveList<LEGAL> legals(b);

        if (!b.inCheck())
        {
            MoveList<QUIET_CHECKS> qcs(b);
            for (int i = 0; i < csize; i++)
            {
                Move m = clist[i];
                if (!qcs.contains(m) && !isPawnPromote(m))
                {
                    std::cout << b << pretty(m) << std::endl;
                    cl = generateQuietCheck(b, clist);
                    assert(false);
                }
            }

            for (auto m : qcs)
            {
                bool exist = false;
                for (int i = 0; i < csize; i++)
                {
                    if (clist[i] == m)
                        exist = true;
                }
                if (!exist)
                {
                    std::cout << b << pretty(m) << std::endl;
                    cl = generateQuietCheck(b, clist);
                    assert(false);
                }
            }
        }

        // recapture
        {
            MoveStack list2[MAX_MOVES], *pl2 = list2;
            Move list3[MAX_MOVES], *pl3 = list3;
            for (auto to : Squares)
            {
                if (to == b.kingSquare(BLACK) || to == b.kingSquare(WHITE))
                    continue;
                pl2 = generate<RECAPTURES>(list2, b, to);
                pl3 = generateRecapture(b, list3, to);
                auto spl2 = pl2 - list2;
                auto spl3 = pl3 - list3;
                for (int i = 0; i < spl2; i++)
                {
                    bool exist = false;
                    for (int j = 0; j < spl3; j++)
                    {
                        if (list2[i] == list3[j])
                            exist = true;
                    }
                    if (!exist)
                        std::cout << b << pretty(list2[i]) << std::endl;
                }

                for (int i = 0; i < spl3; i++)
                {
                    bool exist = false;
                    for (int j = 0; j < spl2; j++)
                    {
                        if (list2[j] == list3[i])
                            exist = true;
                    }
                    if (!exist)
                        std::cout << b << pretty(list3[i]) << std::endl;
                }
            }

        }
        for (int i = 0; i < msize * 16; i++)
        {
            if (m16[i] != MOVE_NONE)
            {
                Move m = move16ToMove(m16[i], b);

                if (b.legal(m))
                    if (!legals.contains(m))
                    {
                        std::cout << b << b.sfen() << " " << std::hex << m16[i] << " " << pretty(m) << std::endl;
                        ml = generateOnBoard<NO_EVASIONS>(b, mlist);
                        assert(false);
                    }
            }
        }

        for (int i = 0; i < dsize; i++)
        {
            Move m = dlist[i];

            if (b.legal(m))
                if (!legals.contains(m))
                {
                    std::cout << b << b.sfen() << " " << std::hex << dlist[i] << " " << pretty(m) << std::endl;
                    dl = b.inCheck() ? generateEvasion(b, dlist) : generateDrop(b, dlist);
                    assert(false);
                }
        }

        //for (int i = 0; i < csize; i++)
        //{
        //    Move m = clist[i];
        //    if (b.legal(m))
        //        if (!legals.contains(m))
        //        {
        //            std::cout << b << " " << std::hex << m << " " << pretty(m) << std::endl; 
        //            cl = generateQuietCheck(b, clist);
        //            assert(false);
        //        }
        //}
 
        if (!b.inCheck())
        {
            MoveStack qcs[MAX_MOVES];
            auto pqcs = generate<QUIET_CHECKS>(qcs, b);

            for (auto p = qcs; p != pqcs; p++)
            {
                auto c = *p;
                bool exist = false;

                for (int i = 0; i < csize; i++)
                    if (c == clist[i])
                        exist = true;

                if (!exist && isDrop(c) && b.legal(c))
                {
                    std::cout << b << pretty(c) << std::endl;
                    cl = generateQuietCheck(b, clist);
                    assert(false);
                }
            }
        }

        if (b.inCheck())
        {
            for (auto m : legals)
            {
                bool exist = false;

                for (int i = 0; i < dsize; i++)
                    if (b.legal(dlist[i]))
                        if (m == dlist[i])
                            exist = true;

                if (!exist)
                {
                    std::cout << b << pretty(m) << std::endl;
                    ml = generateOnBoard<NO_EVASIONS>(b, mlist);
                    dl = generateDrop(b, dlist);
                    assert(false);
                }
            }
        }
        else
        {
            for (auto m : legals)
            {
                bool exist = false;

                for (int i = 0; i < msize * 16; i++)
                    if (m16[i] != MOVE_NONE)
                        if (m == move16ToMove(m16[i], b))
                            exist = true;

                for (int i = 0; i < dsize; i++)
                    if (m == dlist[i])
                        exist = true;

                if (!exist)
                {
                    std::cout << b << pretty(m) << std::endl;
                    ml = generateOnBoard<NO_EVASIONS>(b, mlist);
                    dl = generateDrop(b, dlist);
                    assert(false);
                }
            }
        }
    }
#endif

    // seeのテスト
#if 0
    for (auto m : MoveList<LEGAL>(b))
    {
        for (Score s = Score(-DRAGON_SCORE); s <= DRAGON_SCORE; s += PROMOTE_SILVER_SCORE)
        {
            bool b1 = b.seeGe2(m, s);
            bool b2 = b.seeGe(m, s);
            if (b1 != b2)
            {
                std::cout << b << pretty(m) << b1 << b2;
                b1 = b.seeGe2(m, s);
                b2 = b.seeGe(m, s);
            }
        }
    }
#endif

    // mate1plyのテスト
#if 1
    if (!b.inCheck())
    {
        Move m1 = b.mate1ply();
        Move m2 = b.mate1ply2();

        if ((bool)m1 != (bool)m2)
        {
            std::cout << b << b.sfen() << pretty(m1) << " " << pretty(m2) << std::endl;
            m1 = b.mate1ply();
            m2 = b.mate1ply2();
        }
    }
#endif

    // generateAllMoveのテスト
#if 0
    {
        Move mlist[MAX_MOVES], *mp = mlist;
        Move mlist2[MAX_MOVES], *mp2 = mlist2;

        mp = generateLegal<true>(b, mp);
        mp2 = generateLegal<false>(b, mp2);

        auto msize = mp - mlist;
        auto msize2 = mp2 - mlist2;

        MoveList<LEGAL_ALL> legal_alls(b);
        MoveList<LEGAL> legals(b);

        // 指し手の重複がないか
        for (int i = 0; i < msize - 1; i++)
            for (int j = i + 1; j < msize; j++)
            {
                if (mlist[i] == mlist[j])
                {
                    std::cout << b << b.sfen() << pretty(mlist[i]);
                }
            }

        // 指し手の重複がないか
        for (int i = 0; i < msize2 - 1; i++)
            for (int j = i + 1; j < msize2; j++)
            {
                if (mlist2[i] == mlist2[j])
                {
                    std::cout << b << b.sfen() << pretty(mlist2[i]);
                }
            }

        if (msize > MAX_MOVES)
        {
            std::cout << b << b.sfen() << std::endl;
        }
        if (msize2 > MAX_MOVES)
        {
            std::cout << b << b.sfen() << std::endl;
        }
        for (int i = 0; i < msize; i++)
        {
            Move m = mlist[i];

            if (!b.legal(m))
                std::cout << b << b.sfen() << " " << std::hex << mlist[i] << " " << pretty(m) << std::endl;

            if (!legal_alls.contains(m))
            {
                std::cout << b << b.sfen() << " " << std::hex << mlist[i] << " " << pretty(m) << std::endl;
                mp = generateLegal<true>(b, mp);
                MoveList<LEGAL_ALL> s(b);
            }
        }

        for (auto m : legal_alls)
        {
            bool exist = false;
            for (int i = 0; i < msize; i++)
            {
                if (mlist[i] == m)
                    exist = true;
            }

            if (!exist)
            {
                std::cout << b << b.sfen() << " " << pretty(m) << std::endl;
                mp = generateLegal<true>(b, mlist);
            }
        }

        for (auto m : legals)
        {
            bool exist = false;
            for (int i = 0; i < msize; i++)
            {
                if (mlist2[i] == m)
                    exist = true;
            }

            if (!exist)
            {
                std::cout << b << b.sfen() << " " << pretty(m) << std::endl;
                mp2 = generateLegal<false>(b, mlist2);
            }
        }
    }

#endif

    // pseudoLegalとLegalのチェック
#if 0
    {
        MoveList<LEGAL_ALL> ml(b);

        auto check = [&](Move m)
        {
            if (b.pseudoLegal(m))
            {
                bool b1 = b.givesCheck1(m);
                bool b2 = b.givesCheck2(m);
                if (b1 != b2)
                {
                    std::cout << b << pretty(m) << b1 << b2;
                    b1 = b.givesCheck1(m);
                    b2 = b.givesCheck2(m);
                }
            }

            // 合法手でない手を合法でないと判断できるかどうか
            if (!ml.contains(m))
            {
                if (b.pseudoLegal2(m) && b.legal2(m))
                {
                    auto c = capturePiece(m);
                    std::cout << b << pretty(m) << " capture = " << capturePiece(m) << b.key() << " " << std::hex << (uint32_t)m << std::endl;

                    MoveList<LEGAL_ALL> mll(b);
                    if (!mll.contains(m))
                        if (b.pseudoLegal2(m) && b.legal2(m))
                        {
                            std::cout << b << pretty(m) << " " << b.key() << " " << std::hex << (uint32_t)m << std::endl;
                        }
                }
            }

            // 合法手を合法と判断できるかどうか
            else
            {
                if (!b.pseudoLegal2(m) || !b.legal2(m))
                {
                    auto c = capturePiece(m);
                    std::cout << b << pretty(m) << " capture = " << capturePiece(m) << b.key() << " " << std::hex << (uint32_t)m << std::endl;

                    MoveList<LEGAL_ALL> mll(b);
                    if (mll.contains(m))
                        if (!b.pseudoLegal2(m) || !b.legal2(m))
                        {
                            std::cout << b << pretty(m) << " " << b.key() << " " << std::hex << (uint32_t)m << std::endl;
                        }
                }
            }
        };

        // 考えられる合法、非合法な指し手を全通り試す。
        Move m;
        Turn t = b.turn();
        for (auto from : Squares)
            for (auto to : Squares)
                for (PieceType pt = BISHOP; pt < PIECETYPE_MAX; pt++)
                {
                    Piece p = pt | t;

                    if (pt >= BISHOP && pt < KING && !cantMove(p, to))
                    {
                        m = makeDrop(p, to);
                        check(m);
                    }

                    // 指し手生成で生成されるはずのない手は除外する。駒の動きを守らないような手は生成されない。
                    if ((attackAll(p, from, allZeroMask()) & to))
                    {
                        for (Piece c = EMPTY; c < PIECE_MAX; c++)
                        {
                            if (typeOf(c) == KING || c == SIGN_WHITE)
                                continue;

                            if (!cantMove(p, to))
                            {
                                m = makeMove(from, to, p, c, false);
                                check(m);
                            }

                            if (!isNoPromotable(pt) && canPromote(t, from) && canPromote(t, to))
                            {
                                m = makeMove(from, to, p, c, true);
                                check(m);
                            }

                        }
                    }
                }
    }
#endif

    // isDeclareaWinのチェック
#if 1
    {
        bool b1 = b.isDeclareWin1();
        bool b2 = b.isDeclareWin2();
        if (b1 != b2)
        {
            std::cout << b1 << b2 << b;
            b1 = b.isDeclareWin1();
            b2 = b.isDeclareWin2();
        }
    }
#endif
}

// 指し手生成の速度を計測
void measureBBGenerateMoves(const Board& b)
{
    std::cout << b << std::endl;

    Move legal_moves[MAX_MOVES];
    __m256i legal_onboard[MAX_MOVES];

    for (int i = 0; i < MAX_MOVES; ++i)
        legal_moves[i] = MOVE_NONE;

    Byteboard bb = b.getByteboard();
    Move* pms = &legal_moves[0];
    __m256i* oms = &legal_onboard[0];
    Move16* m16 = (Move16*)oms;

    const uint64_t num = 5000000;
    TimePoint start = now();
    
    if (b.inCheck())
    {
        for (uint64_t i = 0; i < num; i++)
        {
            pms = &legal_moves[0];
            pms = generateEvasion(b, pms);
        }
    }
    else
    {
        for (uint64_t i = 0; i < num; i++)
        {
            pms = &legal_moves[0];
            oms = &legal_onboard[0];
            oms = generateOnBoard<CAPTURE_PLUS_PAWN_PROMOTE>(b, oms);
            oms = generateOnBoard<NO_CAPTURE_MINUS_PAWN_PROMOTE>(b, oms);
            pms = generateDrop(b, pms);
        }
    }

    TimePoint end = now();

    std::cout << "elapsed = " << end - start << " [msec]" << std::endl;

    if (end - start != 0)
        std::cout << "times/s = " << num * 1000 / (end - start) << " [times/sec]" << std::endl;

    const ptrdiff_t count = pms - &legal_moves[0];
    const ptrdiff_t onb = oms - &legal_onboard[0];

    int generated = 0;

    for (int i = 0; i < count; ++i, generated++)
        std::cout << legal_moves[i] << " ";

    for (int i = 0; i < onb * 16; i++)
        if (m16[i] != MOVE_NONE)
        {
            std::cout << move16ToMove(m16[i], b) << " ";
            generated++;
        }

    std::cout << "num of moves = " << generated << std::endl;

    std::cout << std::endl;
}

class TimeMeasure
{
    TimePoint t;
    TimePoint elapsed[2];

public:
    void start() { t = now(); }
    void save(int ex_id)
    {
        TimePoint end = now();
        assert(ex_id == 0 || ex_id == 1);
        elapsed[ex_id] = end - t;
    }

    void outResult(uint64_t num) const
    { 
        for (int i = 0; i < 2; i++)
        {
            std::cout << "elapsed = " << std::dec << elapsed[i] << " [msec]";

            if (elapsed[i] != 0)
                std::cout << " times/s = " << std::dec << (num * 1000 / elapsed[i]) << " [times/sec]" << std::endl;
        }

        // 何%高速化した？
        if (elapsed[0] != 0 && elapsed[1] != 0)
        {
            double per = double(elapsed[0]) / double(elapsed[1]);
            std::cout << (per - 1.0) * 100 << "% speed up." << std::endl;
        }

        std::cout << std::endl;
    }
};

// 指し手生成の速度を計測
void measure_module(const Board& b)
{
    std::cout << b << std::endl;

    Byteboard bb = b.getByteboard();
    const uint64_t num = 50000000;
    const uint64_t num1 = 5000000;
    const uint64_t num3 = 500000;
    const uint64_t num2 = 100000;
    TimeMeasure tm;
    Bitboard b1, dam0 = allZeroMask();
    uint64_t dammy = 0;
    __m256i b2, b3, dam1;
   
    MoveList<LEGAL_ALL> legal_alls(b);

    // ラムダでも書けるのだが、結果が安定しなかったのでdefineで愚直に書く。
#define BENCH(_count, _test_name, _func1, _func2) \
std::cout << _test_name << ":" << std::endl;\
tm.start(); for (uint64_t i = 0; i < _count; i++) { _func1; } tm.save(0); \
tm.start(); for (uint64_t i = 0; i < _count; i++) { _func2; } tm.save(1); \
tm.outResult(_count);

    //BENCH(num1, "attackers", 
    //{ 
    //    for (auto sq : Squares)
    //    {
    //        b1 = b.attackers(BLACK, sq);
    //        dam0 ^= b1;
    //    }
    //},
    //{
    //    for (auto sq : Squares)
    //    {
    //        b2 = attackers<NEIGHBORS>(BLACK, sq, bb.pack<NEIGHBOR>(sq));
    //        b3 = attackers<SLIDERS>(BLACK, sq, bb.pack<RAY>(sq));
    //        dam1 ^= b2 ^ b3;
    //    }
    //});

    //BENCH(num1, "existAttacker",
    //{
    //    for (auto sq : Squares)
    //    {
    //        bool x = b.existAttacker1(BLACK, sq);
    //        dammy += x;
    //    }
    //},
    //{
    //    for (auto sq : Squares)
    //    {
    //        bool x = b.existAttacker2(BLACK, sq);
    //        dammy += x;
    //    }
    //});

    //BENCH(num3, "isDiscoveredCheck",
    //{
    //    for (auto from : Squares)
    //        for (auto to : Squares)
    //        {
    //            bool x = b.isDiscoveredCheck(from, to, b.kingSquare(BLACK), b.state()->blockers_for_king[BLACK]);
    //            dammy += x;
    //        }
    //},
    //{
    //    for (auto from : Squares)
    //        for (auto to : Squares)
    //        {
    //            bool x = isDiscoveredCheck(b.state()->slider_blockers[BLACK], from, to, b.kingSquare(BLACK));
    //            dammy += x;
    //        }
    //});

    //BENCH(num, "isPawnDropCheckMate",
    //{
    //    dammy += b.isPawnDropCheckMate1(BLACK, b.kingSquare(BLACK) + SQ_U, b.kingSquare(BLACK));
    //    dammy += b.isPawnDropCheckMate1(WHITE, b.kingSquare(WHITE) + SQ_D, b.kingSquare(WHITE));
    //},
    //{
    //    dammy += b.isPawnDropCheckMate2(BLACK, b.kingSquare(BLACK) + SQ_U, b.kingSquare(BLACK));
    //    dammy += b.isPawnDropCheckMate2(WHITE, b.kingSquare(WHITE) + SQ_D, b.kingSquare(WHITE));
    //});

    //BENCH(num3, "canPieceCapture",
    //{
    //    for (auto sq : Squares)
    //    {
    //        dammy += b.canPieceCapture1(BLACK, sq, b.kingSquare(BLACK));
    //        dammy += b.canPieceCapture1(WHITE, sq, b.kingSquare(WHITE));
    //    }
    //},
    //{ 
    //    for (auto sq : Squares)
    //    {
    //        dammy += b.canPieceCapture2(BLACK, sq, b.kingSquare(BLACK));
    //        dammy += b.canPieceCapture2(WHITE, sq, b.kingSquare(WHITE));
    //    }
    //});

    BENCH(num, "mate1ply",
    {
        dammy += (uint64_t)b.mate1ply1();
    },
    {
        dammy += (uint64_t)b.mate1ply2();
    });

    //BENCH(num1, "givesCheck",
    //{
    //    for (auto m : legal_alls)
    //        dammy += b.givesCheck1(m);
    //},
    //{
    //    for (auto m : legal_alls)
    //        dammy += b.givesCheck2(m);
    //});

    //BENCH(num3, "seeGe",
    //{
    //    for (auto m : legal_alls)
    //        dammy += b.seeGe1(m, SCORE_ZERO);
    //},
    //{
    //    for (auto m : legal_alls)
    //        dammy += b.seeGe2(m, SCORE_ZERO);
    //});

    //BENCH(num*10, "sliderBlockers",
    //{
    //    dam0 ^= b.sliderBlockers<true>(BLACK, b.kingSquare(BLACK));
    //},
    //{
    //    dammy ^= sliderBlockers(b.kingSquare(BLACK), BLACK, b.state()->king_ray[BLACK]);
    //});

    MoveStack tmp[MAX_MOVES];
    MoveStack* mp = tmp;
    MoveStack tmp2[MAX_MOVES];
    MoveStack* mp2 = tmp2;
    Move tmp3[MAX_MOVES];
    Move* mp3 = tmp3;
    __m256i tmp4[40];
    __m256i* mp4 = tmp4;

    BENCH(num, "generateQuietCheck",
    { 
        mp2 = tmp2;
        mp2 = generate<QUIET_CHECKS>(tmp2, b);
    },
    {
        mp = tmp;
        mp = generateQuietCheck(b, tmp);
    });

    BENCH(num, "generateDrop",
    {
        mp2 = tmp2;
        mp2 = generate<DROP>(tmp2, b);
    },
    {
        mp3 = tmp3;
        mp3 = generateDrop(b, tmp3);
    });

    BENCH(num, "generateCapture",
    {
        mp2 = tmp2;
        mp2 = generate<CAPTURE_PLUS_PAWN_PROMOTE>(tmp2, b);
    },
    {
        mp4 = tmp4;
        mp4 = generateOnBoard<CAPTURE_PLUS_PAWN_PROMOTE>(b, tmp4);
    });

    BENCH(num, "generateNoCapture",
    {
        mp2 = tmp2;
        mp2 = generate<NO_CAPTURE_MINUS_PAWN_PROMOTE>(tmp2, b);
    },
    {
        mp4 = tmp4;
        mp4 = generateOnBoard<NO_CAPTURE_MINUS_PAWN_PROMOTE>(b, tmp4);
    });

#if 0
    BENCH(num, "isDeclareWin",
    {
        dammy ^= b.isDeclareWin1();
    },
    {
        dammy ^= b.isDeclareWin2();
    });

    Move* mlist = new Move[284371];
    int mc = allMoves(b, mlist);

    BENCH(10000, "pseudoLegal",
    {
        for (int i = 0; i < mc; i++)
            dammy += b.pseudoLegal1(mlist[i]);
    },
    {
        for (int i = 0; i < mc; i++)
            dammy += b.pseudoLegal2(mlist[i]);
    });

    MoveList<LEGAL_ALL> noe(b);

    BENCH(10000000, "legal",
    {
        for (auto m : noe)
            dammy += b.legal1(m);
    },
    {
        for (auto m : noe)
            dammy += b.legal2(m);
    });
#endif
    std::cout << dam0.b(0);
    std::cout << (int)dam1.m256i_i8[0];
    std::cout << dammy;
}

#else // USE_BITBOARD

bool Board::seeGe1(const Move, const Score) const { assert(false); return false; }
bool Board::isDeclareWin1() const { assert(false); return false; }
bool Board::isPawnDropCheckMate1(const Turn t, const Square sq, const Square king_square) const { assert(false); return false; }
bool Board::pseudoLegal1(const Move move) const { assert(false); return false; }
bool Board::canPieceCapture1(const Turn t, const Square sq, const Square ksq) const { assert(false); return false; }
bool Board::givesCheck1(Move m) const { assert(false); return false; }
bool Board::existAttacker1(const Turn t, const Square sq) const { assert(false); return false; }
bool Board::inCheck1() const { assert(false); return false; }
Move Board::mate1ply1() const { assert(false); return MOVE_NONE; }
template <bool NotAlwaysDrop> bool Board::legal1(const Move move) const { assert(false); return false; }
#endif // USE_BITBOARD
#else // USE_BYTEBOARD
bool Board::seeGe2(const Move, const Score) const { assert(false); return false; }
bool Board::isDeclareWin2() const { assert(false); return false; }
bool Board::isPawnDropCheckMate2(const Turn t, const Square sq, const Square king_square) const { assert(false); return false; }
bool Board::pseudoLegal2(const Move move) const { assert(false); return false; }
bool Board::canPieceCapture2(const Turn t, const Square sq, const Square ksq) const { assert(false); return false; }
bool Board::givesCheck2(Move m) const { assert(false); return false; }
bool Board::existAttacker2(const Turn t, const Square sq) const { assert(false); return false; }
bool Board::inCheck2() const { assert(false); return false; }
Move Board::mate1ply2() const { assert(false); return MOVE_NONE; }
template <bool NotAlwaysDrop> bool Board::legal2(const Move move) const { assert(false); return false; }
#endif
#ifdef USE_BYTEBOARD
const bool UseModule2 = true;
#else
const bool UseModule2 = false;
#endif

bool Board::seeGe(const Move m, const Score s) const { return UseModule2 ? seeGe2(m, s) : seeGe1(m, s); }
bool Board::isDeclareWin() const { return UseModule2 ? isDeclareWin2() : isDeclareWin1(); }
bool Board::pseudoLegal(const Move move) const { return UseModule2 ? pseudoLegal2(move) : pseudoLegal1(move); }
bool Board::canPieceCapture(const Turn t, const Square sq, const Square ksq) const { return UseModule2 ? canPieceCapture2(t, sq, ksq) : canPieceCapture1(t, sq, ksq); }
bool Board::existAttacker(const Turn t, const Square sq) const { return UseModule2 ? existAttacker2(t, sq) : existAttacker1(t, sq); }
bool Board::givesCheck(Move m) const { return UseModule2 ? givesCheck2(m) : givesCheck1(m); }
bool Board::inCheck() const { return UseModule2 ? inCheck2() : inCheck1(); }
Move Board::mate1ply() const { return UseModule2 ? mate1ply2() : mate1ply1(); }

bool Board::isPawnDropCheckMate(const Turn t, const Square sq, const Square king_square) const {
    return UseModule2 ? isPawnDropCheckMate2(t, sq, king_square) : isPawnDropCheckMate1(t, sq, king_square);
}

template <bool NotAlwaysDrop>
bool Board::legal(const Move move) const { return UseModule2 ? legal2<NotAlwaysDrop>(move) : legal1<NotAlwaysDrop>(move); }
template bool Board::legal<true>(const Move move) const;
template bool Board::legal<false>(const Move move) const;

// 敵味方に関係なくpinされている駒のbitboardをセットする。
template <bool DoNullMove>
void Board::setCheckInfo(StateInfo* si) const
{
    if (!DoNullMove)
    {
#ifdef USE_BITBOARD
        si->blockers_for_king[WHITE] = sliderBlockers(BLACK, kingSquare(WHITE), &si->pinners_for_king[WHITE]);
        si->blockers_for_king[BLACK] = sliderBlockers(WHITE, kingSquare(BLACK), &si->pinners_for_king[BLACK]);
#endif
#ifdef USE_BYTEBOARD
        for (auto t : Turns)
        {
            si->king_neighbor[t] = bb_.pack<NEIGHBOR>(kingSquare(t));
            si->king_ray[t] = bb_.pack<RAY>(kingSquare(t));
            si->slider_blockers[t] = ::sliderBlockers(kingSquare(t), t, si->king_ray[t]);
        }

        si->checker_knights = attackBit<KNIGHTS>(~turn(), kingSquare(turn()), si->king_neighbor[turn()]);
        si->checker_no_knights = attackBit<NO_KNIGHTS>(~turn(), kingSquare(turn()), si->king_ray[turn()]);
#endif
    }
#ifdef USE_BYTEBOARD
    si->reach_sliders = strikeMask<RAY>(si->king_ray[~turn()], kingSquare(~turn()));
#endif
#ifdef USE_BITBOARD
    const Turn enemy = ~turn();
    Square ksq = kingSquare(enemy);
    Bitboard occ = bbOccupied();

    const Bitboard file_attack = attacks<DIRECT_FILE>(ksq, occ);
    const Bitboard lance_attack = file_attack & frontMask(enemy, ksq);
    const Bitboard rook_attack = file_attack | attacks<DIRECT_RANK>(ksq, occ);

    si->check_sq[PAWN] = pawnAttack(enemy, ksq);
    si->check_sq[LANCE] = lance_attack;
    si->check_sq[KNIGHT] = knightAttack(enemy, ksq);
    si->check_sq[SILVER] = silverAttack(enemy, ksq);
    si->check_sq[BISHOP] = bishopAttack(ksq, occ);
    si->check_sq[ROOK] = rook_attack;
    si->check_sq[GOLD] = goldAttack(enemy, ksq);
    si->check_sq[KING] = allZeroMask();
    si->check_sq[HORSE] = si->check_sq[BISHOP] | kingAttack(ksq);
    si->check_sq[DRAGON] = si->check_sq[ROOK] | kingAttack(ksq);
    si->check_sq[PRO_PAWN] = si->check_sq[PRO_LANCE] = si->check_sq[PRO_KNIGHT] = si->check_sq[PRO_SILVER] = si->check_sq[GOLD];
#endif
}

// CheckInfoのセットと、敵味方に関係なくpinされている駒のbitboardをセットする。
template void Board::setCheckInfo<true >(StateInfo* si) const;
template void Board::setCheckInfo<false>(StateInfo* si) const;
