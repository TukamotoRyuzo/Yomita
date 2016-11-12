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

#include "bitop.h"
#include "turn.h"
#include "square.h"

enum Index { HIGH, LOW, ID_MAX };

class Bitboard
{
public:
    Bitboard() {}
    Bitboard(const uint64_t v0, const uint64_t v1) { b_[0] = v0; b_[1] = v1; }
    uint64_t b(const int index) const { return b_[index]; }
    void clear() { b_[0] = b_[1] = 0; }

#if defined (HAVE_SSE2) || defined (HAVE_SSE4)
    Bitboard& operator = (const Bitboard& b) { _mm_store_si128(&m_, b.m_); return *this; }
    Bitboard(const Bitboard& bb) { _mm_store_si128(&m_, bb.m_); }
    Bitboard operator ~ () const { Bitboard tmp; _mm_store_si128(&tmp.m_, _mm_andnot_si128(m_, _mm_set1_epi8(static_cast<char>(0xffu)))); return tmp; }
    Bitboard operator &= (const Bitboard& rhs) { _mm_store_si128(&m_, _mm_and_si128(m_, rhs.m_)); return *this; };
    Bitboard operator |= (const Bitboard& rhs) { _mm_store_si128(&m_, _mm_or_si128(m_, rhs.m_)); return *this; };
    Bitboard operator ^= (const Bitboard& rhs) { _mm_store_si128(&m_, _mm_xor_si128(m_, rhs.m_)); return *this; };
    Bitboard operator <<= (const int i) { _mm_store_si128(&m_, _mm_slli_epi64(m_, i)); return *this; };
    Bitboard operator >>= (const int i) { _mm_store_si128(&m_, _mm_srli_epi64(m_, i)); return *this; };
    Bitboard andEqualNot(const Bitboard& bb) { _mm_store_si128(&m_, _mm_andnot_si128(bb.m_, m_)); return *this; };
    Bitboard notThisAnd(const Bitboard& bb) { Bitboard temp; _mm_store_si128(&temp.m_, _mm_andnot_si128(m_, bb.m_)); return temp; };
#else
    Bitboard operator ~ () const { return Bitboard(~b(0), ~b(1)); };
    Bitboard operator &= (const Bitboard& rhs) { b_[0] &= rhs.b(0); b_[1] &= rhs.b(1); return *this; };
    Bitboard operator |= (const Bitboard& rhs) { b_[0] |= rhs.b(0); b_[1] |= rhs.b(1); return *this; };
    Bitboard operator ^= (const Bitboard& rhs) { b_[0] ^= rhs.b(0); b_[1] ^= rhs.b(1); return *this; };
    Bitboard operator <<= (const int i) { b_[0] <<= i; b_[1] <<= i; return *this; };
    Bitboard operator >>= (const int i) { b_[0] >>= i; b_[1] >>= i; return *this; };
    Bitboard andEqualNot(const Bitboard& bb) { return (*this) &= ~bb; };
    Bitboard notThisAnd(const Bitboard& bb) { return ~(*this) & bb; };
#endif
    Bitboard operator & (const Bitboard& rhs) const { return Bitboard(*this) &= rhs; }
    Bitboard operator | (const Bitboard& rhs) const { return Bitboard(*this) |= rhs; }
    Bitboard operator ^ (const Bitboard& rhs) const { return Bitboard(*this) ^= rhs; }
    Bitboard operator << (const int i) const { return Bitboard(*this) <<= i; }
    Bitboard operator >> (const int i) const { return Bitboard(*this) >>= i; }
#ifdef HAVE_SSE4
    bool operator == (const Bitboard& rhs) const { return (_mm_testc_si128(_mm_cmpeq_epi8(m_, rhs.m_), _mm_set1_epi8(static_cast<char>(0xffu))) ? true : false); };
    operator bool() const { return !(_mm_testz_si128(m_, _mm_set1_epi8(static_cast<char>(0xffu)))); };
    bool andIsFalse(const Bitboard& bb) const { return _mm_testz_si128(m_, bb.m_); };
#else
    bool operator == (const Bitboard& rhs) const { return (b(0) == rhs.b(0)) && (b(1) == rhs.b(1)); };
    operator bool() const { return (b_[0] || b_[1]); };
    bool andIsFalse(const Bitboard& bb) const { return !(*this & bb); };
#endif

    bool operator != (const Bitboard& rhs) const { return !(*this == rhs); }

    // Squareとのoperator
    Bitboard operator & (const Square sq) const;
    Bitboard operator | (const Square sq) const;
    Bitboard operator ^ (const Square sq) const;
    Bitboard& operator |= (const Square sq);
    Bitboard& operator ^= (const Square sq);

    // どちらかが必ず1になっていることが前提条件。その条件を満たさない状況で呼び出すとassert。
    // Clearをtrueにして呼び出すと見つけたビットを0にする。
    template<bool Clear = true> Square firstOne();

    // 立っているビットの数を返す。
    int count() const { return popCount(b(0)) + popCount(b(1) & 0x7fffe00000000000ULL); }

    // merge()はRANK2～RANK8までの情報が入った64bit変数を作る
    // b(0) >> 9をするとRANK_2が0～8ビット目にくる。RANK_1は桁あふれする。
    // b(1) << 9をするとRANK_3が9～17ビット目に来る。RANK_9は桁あふれする。
    // b_[1]のA1にあたる部分(54bit目)が63ビット目にくるが、pextの引数に使うだけなので影響がない。無視する。
    // 縦型のbitboardに比べてシフト2回分損をしているが、この方法ならpextの引数にする前に調べなければならないマスが
    // 1になったbitboardとの&を取らなくていいというメリットもある。
    uint64_t merge() const { return (b(0) >> 9) | (b(1) << 9); }

    // 歩のいる9bitのパターンを与えると、その歩のいない筋が1のbitboardを返す。
    // 先手ならRANK_9, 後手ならRANK_1が0になっているので、1段目への歩打もはじける。
    Bitboard pawnDropable(const Turn t) const;

    // range based forのためのoperator
    Square operator * () { return firstOne(); }
    void operator ++ () {};

    // デバッグ用。ビットボードのレイアウトを見たいときに使う
    friend std::ostream& operator << (std::ostream& ofs, const Bitboard& b);

private:
#if defined (HAVE_SSE2) || defined (HAVE_SSE4)
    union
    {
        uint64_t b_[2];
        __m128i m_;
    };
#else
    uint64_t b_[2];
#endif

    // b_[0]
    //  0  1  2  3  4  5  6  7  8
    //  9 10 11 12 13 14 15 16 17
    // 18 19 20 21 22 23 24 25 26 
    // 27 28 29 30 31 32 33 34 35
    // 36 37 38 39 40 41 42 43 44
    // 45 46 47 48 49 50 51 52 53
    // 54 55 56 57 58 59 60 61 62 
    //  -  -  -  -  -  -  -  -  -
    //  -  -  -  -  -  -  -  -  -

    // b_[1]
    //  -  -  -  -  -  -  -  -  -
    //  -  -  -  -  -  -  -  -  -
    //  0  1  2  3  4  5  6  7  8
    //  9 10 11 12 13 14 15 16 17
    // 18 19 20 21 22 23 24 25 26
    // 27 28 29 30 31 32 33 34 35
    // 36 37 38 39 40 41 42 43 44
    // 45 46 47 48 49 50 51 52 53
    // 54 55 56 57 58 59 60 61 62

    // Redundant Bitboard (RBB)
};

#ifdef HAVE_BMI2
extern __m256i YMM_TO[SQ_MAX];
#endif
extern const Bitboard FILE_MASK[FILE_MAX];
extern const Bitboard RANK_MASK[RANK_MAX];
extern const Bitboard SQUARE_MASK[SQ_MAX];
extern const Bitboard FRONT_MASK[TURN_MAX][RANK_MAX];

inline Bitboard mask(const File f) { return FILE_MASK[f]; }
inline Bitboard mask(const Rank r) { return RANK_MASK[r]; }
inline Bitboard mask(const Square sq) { return SQUARE_MASK[sq]; }
inline Bitboard fileMask(const Square sq) { return mask(fileOf(sq)); }
inline Bitboard rankMask(const Square sq) { return mask(rankOf(sq)); }
inline Bitboard frontMask(const Turn t, const Rank r) { return FRONT_MASK[t][r]; }
inline Bitboard frontMask(const Turn t, const Square sq) { return frontMask(t, rankOf(sq)); }

// Squareとのoperator
inline Bitboard Bitboard::operator & (const Square sq) const { return *this & mask(sq); }
inline Bitboard Bitboard::operator | (const Square sq) const { return *this | mask(sq); }
inline Bitboard Bitboard::operator ^ (const Square sq) const { return *this ^ mask(sq); }
inline Bitboard& Bitboard::operator |= (const Square sq) { *this |= mask(sq); return *this; }
inline Bitboard& Bitboard::operator ^= (const Square sq) { *this ^= mask(sq); return *this; }

// 小駒の利き
extern Bitboard BB_PAWN_ATTACKS  [TURN_MAX][SQ_MAX];
extern Bitboard BB_KNIGHT_ATTACKS[TURN_MAX][SQ_MAX];
extern Bitboard BB_SILVER_ATTACKS[TURN_MAX][SQ_MAX];
extern Bitboard BB_GOLD_ATTACKS  [TURN_MAX][SQ_MAX];
extern Bitboard BB_KING_ATTACKS            [SQ_MAX];

// 飛車の利き。縦と横で別々。
// 飛車のテーブルをひとつの配列で表すと 16 * 495616 = 7929856byte = 7.7MBだが、
// file, rank別々だと16 * 81 * 128 * 2 = 331776byte = 324KBで済む。
// 一つの配列で表した場合と比較して、npsの低下は確認できなかった。
extern Bitboard BB_FILE_ATTACKS[SQ_MAX][128];
extern Bitboard BB_RANK_ATTACKS[SQ_MAX][128];

// 角の利き。これはDIAG1, DIAG2をひとつの配列で表す。
// 別々に用意すると 324KB だが、
// ひとつで表すと 16 * 20224 = 323584 = 316KB と、8KB少なくてすむ。
extern Bitboard BISHOP_ATTACK      [20224];
extern int      BISHOP_ATTACK_INDEX[SQ_MAX];

// 障害物を表す
extern Bitboard BB_OBSTACLE[SQ_MAX][SQ_MAX];

extern const uint64_t PEXT_MASK_FILE[SQ_MAX];
extern const uint64_t PEXT_MASK_DIAG[SQ_MAX];

// 何も邪魔な駒がないときの利き
extern Bitboard BB_LANCE_ATTACKS[TURN_MAX][SQ_MAX]; 
extern Bitboard BB_ROOK_ATTACKS[SQ_MAX];
extern Bitboard BB_EXCEPT_PAWN_FILE_MASK[TURN_MAX][512];

extern Bitboard ROOK_STEP_ATTACK[SQ_MAX];
extern Bitboard BISHOP_STEP_ATTACK[SQ_MAX];

extern Bitboard BB_LANCE_CHECKS [TURN_MAX][SQ_MAX];
extern Bitboard BB_KNIGHT_CHECKS[TURN_MAX][SQ_MAX];
extern Bitboard BB_SILVER_CHECKS[TURN_MAX][SQ_MAX];
extern Bitboard BB_GOLD_CHECKS  [TURN_MAX][SQ_MAX];

extern Bitboard BB_LINE[SQ_MAX][SQ_MAX];

// turn側から見て敵の陣地だけを取り出すマスク定数
extern const Bitboard BB_ENEMY_MASK[TURN_MAX];

// turn側から見て敵の陣地 + 1段を取り出すマスク定数
extern const Bitboard BB_PROMOTE_MASK[TURN_MAX];

#if 0
const size_t bbtable_size = sizeof(FILE_MASK)
+ sizeof(RANK_MASK)
+ sizeof(SQUARE_MASK)
+ sizeof(FRONT_MASK)
+ sizeof(BB_PAWN_ATTACKS)
+ sizeof(BB_KNIGHT_ATTACKS)
+ sizeof(BB_SILVER_ATTACKS)
+ sizeof(BB_GOLD_ATTACKS)
+ sizeof(BB_KING_ATTACKS)
+ sizeof(BB_FILE_ATTACKS)
+ sizeof(BB_RANK_ATTACKS)
+ sizeof(BISHOP_ATTACK)
+ sizeof(BISHOP_ATTACK_INDEX)
+ sizeof(BB_OBSTACLE)
+ sizeof(PEXT_MASK_FILE)
+ sizeof(PEXT_MASK_DIAG)
+ sizeof(BB_LANCE_ATTACKS)
+ sizeof(BB_ROOK_ATTACKS)
+ sizeof(BB_EXCEPT_PAWN_FILE_MASK)
+ sizeof(ROOK_STEP_ATTACK)
+ sizeof(BISHOP_STEP_ATTACK)
+ sizeof(BB_LANCE_CHECKS)
+ sizeof(BB_KNIGHT_CHECKS)
+ sizeof(BB_SILVER_CHECKS)
+ sizeof(BB_GOLD_CHECKS)
+ sizeof(BB_LINE)
+ sizeof(BB_ENEMY_MASK)
+ sizeof(BB_PROMOTE_MASK);

const size_t kb_size = bbtable_size >> 10;
#endif

// turn側から見て敵の陣地だけを取り出すマスク
inline Bitboard enemyMask(const Turn t) { return BB_ENEMY_MASK[t]; }

// turn側から見て敵の陣地 + 1段を取り出すマスク定数
inline Bitboard enemyMaskPlus1(const Turn t) { return BB_PROMOTE_MASK[t]; }

inline Bitboard Bitboard::pawnDropable(const Turn t) const
{
    uint64_t tmp = b(0) | b(1);
    tmp |= tmp >> 36;
    return BB_EXCEPT_PAWN_FILE_MASK[t][(tmp >> 18 | tmp >> 9 | tmp) & 511];
}

// 実際に使用する部分がすべて1になっているビットボード
inline Bitboard allOneMask()  { return Bitboard(0x7fffffffffffffffULL, 0x7fffffffffffffffULL); }

// 実際に使用する部分がすべて0
inline Bitboard allZeroMask() { return Bitboard(0, 0); };

// 小駒の利き
inline Bitboard pawnAttack  (const Turn t, const Square sq) { return BB_PAWN_ATTACKS  [t][sq]; }
inline Bitboard knightAttack(const Turn t, const Square sq) { return BB_KNIGHT_ATTACKS[t][sq]; }
inline Bitboard silverAttack(const Turn t, const Square sq) { return BB_SILVER_ATTACKS[t][sq]; }
inline Bitboard goldAttack  (const Turn t, const Square sq) { return BB_GOLD_ATTACKS  [t][sq]; }
inline Bitboard kingAttack  (              const Square sq) { return BB_KING_ATTACKS     [sq]; }

// xxxToEdgeは何も邪魔な駒がないときの利き
inline Bitboard  lanceAttackToEdge(const Turn t, const Square sq) { return BB_LANCE_ATTACKS[t][sq];                 }
inline Bitboard bishopAttackToEdge(              const Square sq) { return BISHOP_ATTACK[BISHOP_ATTACK_INDEX[sq]];  }
inline Bitboard   rookAttackToEdge(              const Square sq) { return BB_ROOK_ATTACKS    [sq];                 }
inline Bitboard  horseAttackToEdge(              const Square sq) { return bishopAttackToEdge(sq) | kingAttack(sq); }
inline Bitboard dragonAttackToEdge(              const Square sq) { return   rookAttackToEdge(sq) | kingAttack(sq); }

// 飛車、角の利きの小駒バージョン
inline Bitboard rookStepAttack(const Square sq) { return ROOK_STEP_ATTACK[sq]; }
inline Bitboard bishopStepAttack(const Square sq) { return BISHOP_STEP_ATTACK[sq]; }

// 王手テーブル
inline Bitboard  lanceCheck(const Turn t, const Square sq) { return BB_LANCE_CHECKS [t][sq]; }
inline Bitboard knightCheck(const Turn t, const Square sq) { return BB_KNIGHT_CHECKS[t][sq]; }
inline Bitboard silverCheck(const Turn t, const Square sq) { return BB_SILVER_CHECKS[t][sq]; }
inline Bitboard   goldCheck(const Turn t, const Square sq) { return BB_GOLD_CHECKS  [t][sq]; }

// 縦横斜めの利きを求める。
template <RelationType RT> inline Bitboard attacks(const Square from, const Bitboard& occupied)
{
    STATIC_ASSERT(RT == DIRECT_RANK || RT == DIRECT_FILE);

    // Rankの場合だけ、pextを使わずにマスクと右シフトで求める。
    if (RT == DIRECT_RANK)
    {
        // Squareがb_[0]とb_[1]のどちらに属しているか。どちらにも属しているなら0を優先する。
        const int index = static_cast<int>(from >= SQ_98);
        const int shift = index ? (from / 9 - 2) * 9 + 1 : from / 9 * 9 + 1;
        const int pat = ((occupied.b(index) & rankMask(from).b(index)) >> shift) & 127;
        return BB_RANK_ATTACKS[from][pat];
    }
    else
        return BB_FILE_ATTACKS[from][pext(occupied.merge(), PEXT_MASK_FILE[from])];
}

inline Bitboard  lanceAttack(const Turn t, const Square sq, const Bitboard& occupied) { return attacks<DIRECT_FILE>(sq, occupied) & frontMask(t, sq); }
inline Bitboard bishopAttack(const Square sq, const Bitboard& occupied) { return BISHOP_ATTACK[BISHOP_ATTACK_INDEX[sq] + pext(occupied.merge(), PEXT_MASK_DIAG[sq])]; }
inline Bitboard   rookAttack(const Square sq, const Bitboard& occupied) { return attacks<DIRECT_FILE>(sq, occupied) | attacks<DIRECT_RANK>(sq, occupied); }
inline Bitboard  horseAttack(const Square sq, const Bitboard& occupied) { return bishopAttack(sq, occupied) | kingAttack(sq); }
inline Bitboard dragonAttack(const Square sq, const Bitboard& occupied) { return   rookAttack(sq, occupied) | kingAttack(sq); }

// sq1とsq2の間が1になっているbitboardを返す。
inline Bitboard bbBetween(const Square sq1, const Square sq2) { return BB_OBSTACLE[sq1][sq2]; }
inline Bitboard bbLine(const Square sq1, const Square sq2) { return BB_LINE[sq1][sq2]; }

inline const Bitboard begin(const Bitboard& b) { return b; }
inline const Bitboard end(const Bitboard& b) { return allZeroMask(); }

inline int popLSB(uint64_t &b) 
{ 
    int index = bsf64(b); 
    b &= b - 1; 
    //b = _blsr_u64(b);
    return index; 
}

template <bool Clear> inline Square Bitboard::firstOne()
{
    if (b_[0])
    {
        const int index = bsf64(b_[0]);

        // bitを消すときb_[0]とb_[1]が重なっている場合は二つ消すことに注意
        if (Clear)
            andEqualNot(mask(Square(index)));

        return Square(index);
    }
    else
    {
        // 45～62ビット目だけを取り出すマスクは必要なく、そのままbsfにかければよい。
        const int index = (Clear ? popLSB(b_[1]) : bsf64(b_[1])) + 18;
        
        // Bitboardがすべて0ではないことを前提としているので、ここでindexが63になるはずはない。
        assert(index > 44 + 18 && index < 63 + 18);
        
        return Square(index);
    }
}

// どちらか片方だけを探す場合。この場合、Clearがtrueなら見つけたビットをclearする
// firstOne系ではこれが一番高速なのでできればこれを使うようにコーディングしたい。
template <Index Id, bool Clear = true> inline Square firstOne(uint64_t& mask)
{
    const int index = Clear ? popLSB(mask) : bsf64(mask);
    return Square(Id == HIGH ? index : index + 18);
}
