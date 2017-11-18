#pragma once

#include "platform.h"
#include "types.h"

#pragma warning(disable: 4789)

#ifdef USE_BYTEBOARD
// Byteboard
// 将棋におけるByteboardは、駒を一つ1byteとしてそれを96byte分用意するデータ構造である。81bit目~95bit目は使わない。
// 普通の駒配列を用いた場合との違いは、この96byteの配列を32byte変数3つで構成されていると考える点と、
// 駒を表す1byteは単なる駒の識別番号ではなく、駒の利きを表すものであるという点である。
// このようなデータ構造にすることで、avx2が使えるCPUであれば32マス分を一気に処理することができる。

// cf. たこっとのアピール文書, やねうら王公式サイト
// 各マスへの利きの取得 http://www2.computer-shogi.org/wcsc26/appeal/takotto/appeal.html
// SEEの実装について http://denou.jp/tournament2016/img/PR/takotto.pdf
// http://yaneuraou.yaneu.com/2016/04/03/%E3%81%9F%E3%81%93%E3%81%A3%E3%81%A8%E3%81%AEbyteboard%E3%81%AF%E3%82%B3%E3%83%B3%E3%83%94%E3%83%A5%E3%83%BC%E3%82%BF%E3%83%BC%E5%B0%86%E6%A3%8B%E3%81%AE%E9%9D%A9%E5%91%BD%E5%85%90%E3%81%AB%E3%81%AA/

// 駒のビット表記
// EGSR BLNP
// 0000 0001 0x01 PAWN
// 0000 0010 0x02 KNIGHT
// 0000 0100 0x04 LANCHE
// 0000 1000 0x08 BISHOP
// 0001 0000 0x10 ROOK
// 0010 0000 0x20 SILVER
// 0100 0000 0x40 GOLD(=TGOLD)
// 1000 0000 0x80 ENEMY
// 0110 0000 0x60 KING
// 0100 1000 0x48 HORSE
// 0111 0000 0x70 DRAGON

// 成り駒の区別ができれば完璧だが、PB_GOLD以外で立たせられるbitがPB_PAWNしかないので難しい。
// PieceBitは駒の利きを表すものなので、PB_GOLD | PB_LANCEのようにしてしまうと
// 金+香の動きができる駒ということになってしまう。
enum PieceBit : uint8_t
{
    PB_EMPTY = 0x00,
    PB_PAWN = 0x01,
    PB_LANCE = 0x04,
    PB_KNIGHT = 0x02,
    PB_SILVER = 0x20,
    PB_GOLD = 0x40,
    PB_BISHOP = 0x08,
    PB_ROOK = 0x10,
    PB_KING = 0x60,
    PB_PRO_PAWN = PB_GOLD,
    PB_PRO_LANCE = PB_GOLD,
    PB_PRO_KNIGHT = PB_GOLD,
    PB_PRO_SILVER = PB_GOLD,
    PB_HORSE = 0x48,
    PB_DRAGON = 0x70,
    PB_ENEMY = 0x80,
};

ENABLE_OPERATORS_ON(PieceBit);
extern const PieceBit PIECE_TO_PIECEBIT[];
inline PieceBit toPieceBit(Piece p) { return PIECE_TO_PIECEBIT[p]; }

// packされた32byteがどのタイプなのかを表す定数。
enum PackType { NEIGHBOR, CROSS, DIAG, RAY, CHECKABLE_BLACK, CHECKABLE_WHITE };

// packされた32byteに対してどの駒の利きを求めるかを表す定数。
enum AttackerType { NEIGHBORS, KNIGHTS, SLIDERS, NO_KNIGHTS };

// avx命令は見づらいのでせめてbit演算くらいはoperatorを定義する。
inline __m256i  operator |  (__m256i  a, __m256i b) { return  _mm256_or_si256(a, b); }
inline __m256i  operator &  (__m256i  a, __m256i b) { return _mm256_and_si256(a, b); }
inline __m256i  operator ^  (__m256i  a, __m256i b) { return _mm256_xor_si256(a, b); }
inline __m256i& operator |= (__m256i& a, __m256i b) { return a = a | b; }
inline __m256i& operator &= (__m256i& a, __m256i b) { return a = a & b; }
inline __m256i& operator ^= (__m256i& a, __m256i b) { return a = a ^ b; }
inline bool     operator == (__m256i  a, __m256i b) { return  _mm256_testc_si256(_mm256_cmpeq_epi8(a, b), _mm256_cmpeq_epi8(_mm256_setzero_si256(), _mm256_setzero_si256())); }
inline bool     operator != (__m256i  a, __m256i b) { return !_mm256_testc_si256(_mm256_cmpeq_epi8(a, b), _mm256_cmpeq_epi8(_mm256_setzero_si256(), _mm256_setzero_si256())); }

class Byteboard
{
protected:

    union
    {
        // p_[96]
        //  0  1  2  3  4  5  6  7  8
        //  9 10 11 12 13 14 15 16 17
        // 18 19 20 21 22 23 24 25 26 
        // 27 28 29 30 31 32 33 34 35
        // 36 37 38 39 40 41 42 43 44
        // 45 46 47 48 49 50 51 52 53
        // 54 55 56 57 58 59 60 61 62 
        // 63 64 65 66 67 68 69 70 71
        // 72 73 74 75 76 77 78 79 80

        // b_[3]
        // [0] :  0 ~ 31
        // [1] : 32 ~ 63
        // [2] : 64 ~ 80

        __m256i b_[3];
        PieceBit p_[96];
    };

public:

    Byteboard() {};
    PieceBit pieceBit(Square sq) const { return p_[sq]; }
    void setPieceBit(Square sq, PieceBit pb) { p_[sq] = pb; }

    // Byteboardから任意の位置にあるPieceBitを最大16マス分packして返す。戻り値の上位16byteはゴミ。
    __m256i packLow(const Byteboard& shuffle) const;

    // 上位16byteにpackする。下位16byteはゴミ。DIAG用。
    __m256i packHigh(const Byteboard& shuffle) const;

    // NEIGHBOR : sqの周り12マスのPieceBitを32byteにpackして返す。
    // CROSS : sqから飛の利き上にあるPieceBitを32byteにpackして返す。
    // DIAG  : sqから角の利き上にあるPieceBitを32byteにpackして返す。
    // RAY   : sqから飛角の利き上にあるPieceBitを32byteにpackして返す。
    template <PackType PT> __m256i pack(Square sq) const;

    __m256i turnField(const Turn t) const;

    // Byteboard上でemptyな所をBitboard化する。下位81bit以外は0。
    __m256i emptys() const;
    static Byteboard one();
};

__m256i* generateMoveFrom(const Byteboard& bb, Square from, Piece p, __m256i* mlist);
#endif