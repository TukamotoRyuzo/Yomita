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

#include "piece.h"
#include "turn.h"

// handのデータ構造は、Aperyを参考にしています。
// 手駒の優劣判定を高速に行うため、bit間を1bitあける。
// 00000000 00000000 00000000 00000011 BISHOP
// 00000000 00000000 00000000 00011000 ROOK
// 00000000 00000000 00000111 11000000 PAWN
// 00000000 00000000 01110000 00000000 LANCE
// 00000000 00000111 00000000 00000000 KNIGHT
// 00000000 01110000 00000000 00000000 SILVER
// 00000111 00000000 00000000 00000000 GOLD
// 00001000 10001000 10001000 00100100 BORROW_MASK

class Hand
{
public:

    Hand() : hand_(0) {};
    void clear() { hand_ = 0; }
    operator uint32_t() const { return hand_; }

    // 取った駒を持ち駒に加える,打った駒を持ち駒から減らす
    void plus(const PieceType p) { assert(p > EMPTY && p < KING); hand_ += HAND_INCREMENT[p - 1]; }
    void minus(const PieceType p) { assert(p > EMPTY && p < KING); hand_ -= HAND_INCREMENT[p - 1]; }
    void set(const PieceType p, int num) { assert(p > EMPTY && p < KING); hand_ |= num << HAND_SHIFT[p - 1]; }

    // 指定された持ち駒を持っているかどうか。 先手後手の同じ駒同士の枚数を比較するときも使えるのでintで返す
    uint32_t exists(const PieceType p) const { assert(p > EMPTY && p < KING);  return hand_ & HAND_MASK[p - 1]; }
    uint32_t existsExceptPawn() const { return hand_ & EXCEPT_PAWN_MASK; }

    // 枚数を返す
    int count(const PieceType p) const { assert(p > EMPTY && p < KING); return (hand_ & HAND_MASK[p - 1]) >> HAND_SHIFT[p - 1]; }

    // 引数のHandより優れていたらtrue
    // hのほうがどれかひとつでも多く持っていればBORROWのbitが立つ。
    bool isSuperior(const Hand h) const 
    { 
        if (((*this - h) & BORROW_MASK) == 0)
        {
            return true;
        }

        constexpr uint32_t psg = PAWN_MASK | SILVER_MASK | GOLD_MASK;

        // 桂馬と角の枚数が違う。
        if ((hand_ ^ h.hand_) & (KNIGHT_MASK | BISHOP_MASK))
        {
            return false;
        }

        // 歩が一枚減っている
        if ((h.hand_ & PAWN_MASK) == ((hand_ & PAWN_MASK) + (1 << PAWN_SHIFT)))
        {
            // 香、桂、銀、金、飛車のうちどれか優等である。
            if (((hand_ ^ h.hand_) & ~PAWN_MASK)
                && (((hand_ & ~PAWN_MASK) - (h.hand_ & ~PAWN_MASK)) & BORROW_MASK) == 0)
            {
                return true;
            }
        }
        
        // 香が一枚減っていて、歩、銀、金の枚数は同じで飛車を多く持っていれば優等
        else if ((h.hand_ & LANCE_MASK) == ((hand_ & LANCE_MASK) + (1 << LANCE_SHIFT))
            && (h.hand_ & psg) == (hand_ & psg)
            && (h.hand_ & ROOK_MASK) < (hand_ & ROOK_MASK))
        {
            return true;
        }

        return false;
    }
#ifdef HELPER
    bool isSuperior2(const Hand& h)
    {
        if (((*this - h) & BORROW_MASK) == 0)
            return true;

        if (count(KNIGHT) != h.count(KNIGHT) || count(BISHOP) != h.count(BISHOP))
            return false;

        // 歩が1枚減って、香、銀、金、飛が1枚増えたなら優等
        if (h.count(PAWN) == count(PAWN) + 1)
        {
            if (count(LANCE) >= h.count(LANCE)
                && count(SILVER) >= h.count(SILVER)
                && count(GOLD) >= h.count(GOLD)
                && count(ROOK) >= h.count(ROOK)
                && (count(LANCE) > h.count(LANCE)
                    || count(SILVER) > h.count(SILVER)
                    || count(GOLD) > h.count(GOLD)
                    || count(ROOK) > h.count(ROOK)))
            {
                return true;
            }
        }

        // 香が1枚減って、飛車が1枚増えたなら優等
        else if (h.count(LANCE) == count(LANCE) + 1)
        {
            if (count(ROOK) > h.count(ROOK)
                && count(PAWN) >= h.count(PAWN)
                && count(SILVER) >= h.count(SILVER)
                && count(GOLD) >= h.count(GOLD))
            {
                return true;
            }
        }

        return false;
    }
#endif
    // 各駒のあるなしを下位ビットから1ビットずつ並べて返す。
    uint32_t existsBit() const { return (uint32_t)pext(hand_ + EXISTS_MASK, BORROW_MASK); }

    // existsBitの戻り値から歩、香、桂のあるなしを取り出すマスク。
    static constexpr uint32_t pMask() { return 1 << 2; }
    static constexpr uint32_t lMask() { return 1 << 3; }
    static constexpr uint32_t kMask() { return 1 << 4; }
    static constexpr uint32_t plkMask() { return pMask() | lMask() | kMask(); }
    
private:

    uint32_t hand_;

    static const int BISHOP_SHIFT = 0;
    static const int ROOK_SHIFT   = 3;
    static const int PAWN_SHIFT   = 6;
    static const int LANCE_SHIFT  = 12;
    static const int KNIGHT_SHIFT = 16;
    static const int SILVER_SHIFT = 20;
    static const int GOLD_SHIFT   = 24;

    static const uint32_t BISHOP_MASK = 0x03 << BISHOP_SHIFT;
    static const uint32_t ROOK_MASK   = 0x03 << ROOK_SHIFT;
    static const uint32_t GOLD_MASK   = 0x07 << GOLD_SHIFT;
    static const uint32_t PAWN_MASK   = 0x1f << PAWN_SHIFT;
    static const uint32_t LANCE_MASK  = 0x07 << LANCE_SHIFT;
    static const uint32_t KNIGHT_MASK = 0x07 << KNIGHT_SHIFT;
    static const uint32_t SILVER_MASK = 0x07 << SILVER_SHIFT;
    static const uint32_t EXCEPT_PAWN_MASK = BISHOP_MASK | ROOK_MASK | LANCE_MASK | KNIGHT_MASK | SILVER_MASK | GOLD_MASK;
    static const uint32_t EXISTS_MASK = PAWN_MASK | EXCEPT_PAWN_MASK;
    static const uint32_t BORROW_MASK = (1 << (ROOK_SHIFT   - 1)) |
                                        (1 << (PAWN_SHIFT   - 1)) |
                                        (1 << (LANCE_SHIFT  - 1)) |
                                        (1 << (KNIGHT_SHIFT - 1)) |
                                        (1 << (SILVER_SHIFT - 1)) |
                                        (1 << (GOLD_SHIFT   - 1)) |
                                        (GOLD_MASK + (1 << GOLD_SHIFT));
    static const int HAND_SHIFT[HAND_MAX];
    static const uint32_t HAND_MASK[HAND_MAX];
    static const uint32_t HAND_INCREMENT[HAND_MAX];
};

#ifdef HELPER
std::ostream& operator << (std::ostream &os, const Hand& h);
#endif