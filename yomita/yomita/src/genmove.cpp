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

#ifdef USE_BITBOARD
#include "move.h"
#include "common.h"

// 指し手生成祭り→4.5M/s (i7-6700HQ 2.6GHz)

// 持ち駒打ちをswitchで40分岐するか
#define UNROLL

// 持ち駒打ちでAVX2命令を使うか
#ifdef HAVE_BMI2
// MSVC で無い場合は、YMM を使用しない
#if defined(_MSC_VER)
#define USE_YMM
#endif
#endif

// プロトタイプ宣言 : generateLegalのため
template <MoveType MT, Turn T, bool ALL> MoveStack* generate(MoveStack* mlist, const Board& b);

namespace
{
    // 歩を動かす手
    template <MoveType MT, Turn T, bool ALL>
    inline MoveStack* generatePawnMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        const Square behind = T == BLACK ? SQ_D : SQ_U;
        const Piece P = T == BLACK ? B_PAWN : W_PAWN;
        const Bitboard bb_from = b.bbPiece(PAWN, T);
        uint64_t to_4_9 = (T == BLACK ? bb_from.b(T_LOW) >> 9 : bb_from.b(T_LOW) << 9) & target.b(T_LOW) & SELF_MASK[T];

        while (to_4_9)
        {
            const Square to = firstOne<T_LOW>(to_4_9);
            const Square from = to + behind;
            mlist++->move = makeMove<MT>(from, to, P, b);
        }

        if (MT != NO_CAPTURE_MINUS_PAWN_PROMOTE)
        {
            uint64_t to_1_3 = (T == BLACK ? bb_from.b(T_HIGH) >> 9 : bb_from.b(T_HIGH) << 9) & target.b(T_HIGH) & ENEMY_MASK[T];

            while (to_1_3)
            {
                const Square to = firstOne<T_HIGH>(to_1_3);
                const Square from = to + behind;
                mlist++->move = makeMovePromote<MT>(from, to, P, b);

                if (ALL)
                {
                    const Rank t_rank1 = (T == BLACK ? RANK_1 : RANK_9);

                    if (rankOf(to) != t_rank1)
                        mlist++->move = makeMove<MT>(from, to, P, b);
                }
            }
        }

        return mlist;
    }

    // 竜馬を動かす手
    template <MoveType MT, Turn T, PieceType PT, bool ALL>
    inline MoveStack* generateHorseDragonMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Piece p = PT == HORSE ? T == BLACK ? B_HORSE  : W_HORSE
                                    : T == BLACK ? B_DRAGON : W_DRAGON;
        auto attack = PT == HORSE ? horseAttack : dragonAttack;
        Bitboard bb_from = b.bbPiece(PT, T);

        Square from, to;
        FORBB(bb_from, from, {
            Bitboard bb_to = attack(from, b.bbOccupied()) & target;
            FORBB(bb_to, to, mlist++->move = makeMove<MT>(from, to, p, b));
        });

        return mlist;
    }

    // 飛車角を動かす手
    template <MoveType MT, Turn T, PieceType PT, bool ALL>
    inline MoveStack* generateBishopRookMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        const Piece p = PT == BISHOP ? T == BLACK ? B_BISHOP : W_BISHOP
                                     : T == BLACK ? B_ROOK : W_ROOK;
        auto attack = PT == BISHOP ? bishopAttack : rookAttack;

        // fromが敵陣か、非敵陣かで分ける。
        Bitboard bb_from = b.bbPiece(PT, T);

        uint64_t from_1_3 = bb_from.b(T_HIGH) & ENEMY_MASK[T];

        while (from_1_3) // fromが敵陣なら、必ず成れる。
        {
            const Square from = firstOne<T_HIGH>(from_1_3);
            Bitboard bb_to = attack(from, b.bbOccupied());
            uint64_t to_1_7 = bb_to.b(T_HIGH) & target.b(T_HIGH);

            while (to_1_7)
            {
                const Square to = firstOne<T_HIGH>(to_1_7);
                mlist++->move = makeMovePromote<MT>(from, to, p, b);

                if (ALL)
                    mlist++->move = makeMove<MT>(from, to, p, b);
            }

            uint64_t to_8_9 = bb_to.b(T_LOW) & target.b(T_LOW) & EXCEPT_MASK[T_LOW];

            while (to_8_9)
            {
                const Square to = firstOne<T_LOW>(to_8_9);
                mlist++->move = makeMovePromote<MT>(from, to, p, b);

                if (ALL)
                    mlist++->move = makeMove<MT>(from, to, p, b);
            }
        }
        
        uint64_t from_4_9 = bb_from.b(T_LOW) & SELF_MASK[T];

        while (from_4_9) // fromが非敵陣
        {
            const Square from = firstOne<T_LOW>(from_4_9);
            Bitboard bb_to = attack(from, b.bbOccupied());

            uint64_t to_1_3 = bb_to.b(T_HIGH) & target.b(T_HIGH) & ENEMY_MASK[T];

            while (to_1_3) // 移動先が敵陣
            {
                const Square to = firstOne<T_HIGH>(to_1_3);
                mlist++->move = makeMovePromote<MT>(from, to, p, b);

                if (ALL)
                    mlist++->move = makeMove<MT>(from, to, p, b);
            }
            
            uint64_t to_4_9 = bb_to.b(T_LOW) & target.b(T_LOW) & SELF_MASK[T];

            while (to_4_9) // 移動先も非敵陣
                mlist++->move = makeMove<MT>(from, firstOne<T_LOW>(to_4_9), p, b);
        }

        return mlist;
    }

    // TGoldを動かす手
    template <MoveType MT, Turn T, bool ALL>
    inline MoveStack* generateGoldMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        Bitboard bb_from = b.bbGold(T);

        uint64_t from_1_3 = bb_from.b(T_HIGH) & ENEMY_MASK[T];

        while (from_1_3)
        {
            const Square from = firstOne<T_HIGH>(from_1_3);

            // toはHIGH盤面に収まっている。
            uint64_t to_1_4 = goldAttack(T, from).b(T_HIGH) & target.b(T_HIGH);

            while (to_1_4)
                mlist++->move = makeMove<MT>(from, firstOne<T_HIGH>(to_1_4), b.piece(from), b);
        }

        uint64_t from_4_9 = bb_from.b(T_LOW) & SELF_MASK[T];

        while (from_4_9)
        {
            const Square from = firstOne<T_LOW>(from_4_9);

            // toはLOW盤面に収まっている。
            uint64_t to_3_9 = goldAttack(T, from).b(T_LOW) & target.b(T_LOW);

            while (to_3_9)
                mlist++->move = makeMove<MT>(from, firstOne<T_LOW>(to_3_9), b.piece(from), b);
        }

        return mlist;
    }

    // 香を動かす手
    template <MoveType MT, Turn T, bool ALL>
    inline MoveStack* generateLanceMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        const bool isCapture = MT == CAPTURE_PLUS_PAWN_PROMOTE;
        const Piece p = T == BLACK ? B_LANCE : W_LANCE;
        Bitboard bb_from = b.bbPiece(LANCE, T);
        uint64_t from_3_9 = bb_from.b(T_LOW);

        while (from_3_9)
        {
            const Square from = firstOne<T_LOW>(from_3_9);
            const Bitboard bb_to = lanceAttack(T, from, b.bbOccupied());
            bool gen = false;
            uint64_t to_4_9 = bb_to.b(T_LOW) & target.b(T_LOW) & SELF_MASK[T];

            while (to_4_9) // 移動先も非敵陣
            {
                const Square to = firstOne<T_LOW, !isCapture>(to_4_9);
                mlist++->move = makeMove<MT>(from, to, p, b);

                if (isCapture) // 駒を取る手なら、一手しか生成できないはず
                {
                    gen = true;
                    assert((to_4_9 & (to_4_9 - 1)) == 0);
                    break;
                }
            }

            if (isCapture && gen)
                continue;

            uint64_t to_1_3 = bb_to.b(T_HIGH) & target.b(T_HIGH) & ENEMY_MASK[T];

            while (to_1_3) // 移動先が敵陣
            {
                const Square to = firstOne<T_HIGH, !isCapture>(to_1_3);
                mlist++->move = makeMovePromote<MT>(from, to, p, b);

                if (ALL)
                {
                    if (isBehind(T, RANK_1, to)) // 1段目の不成は除く。
                        mlist++->move = makeMove<MT>(from, to, p, b);
                }
                else if (MT != NO_CAPTURE_MINUS_PAWN_PROMOTE)
                {
                    if (isBehind(T, RANK_2, to)) // 2段目の不成は除く。
                        mlist++->move = makeMove<MT>(from, to, p, b);
                }

                if (isCapture) // 駒を取る手なら、一手しか生成できないはず
                {
                    assert((to_1_3 & (to_1_3 - 1)) == 0);
                    break;
                }
            }
        }

        uint64_t from_1_2 = bb_from.b(T_HIGH) & EXCEPT_MASK[T_HIGH];
        uint64_t to_1_2 = (T == BLACK ? from_1_2 >> 9 : from_1_2 << 9) & target.b(T_HIGH);

        while (to_1_2)
        {
            const Square to = firstOne<T_HIGH>(to_1_2);
            const Square from = to + (T == BLACK ? 9 : -9);
            mlist++->move = makeMovePromote<MT>(from, to, p, b);
        }

        return mlist;
    }

    // 桂馬を動かす手
    // RBBは桂馬の指し手生成を高速化するのに非常に都合がいい。
    template <MoveType MT, Turn T, bool ALL>
    inline MoveStack* generateKnightMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        const Piece p = T == BLACK ? B_KNIGHT : W_KNIGHT;
        uint64_t from_3_9 = b.bbType(KNIGHT).b(T_LOW) & b.bbTurn(T).b(T_LOW);

        while (from_3_9)
        {
            const Square from = firstOne<T_LOW>(from_3_9);

            uint64_t to_1_7 = knightAttack(T, from).b(T_HIGH) & target.b(T_HIGH);

            uint64_t to_1_3 = to_1_7 & ENEMY_MASK[T];

            while (to_1_3) // 成りを生成
                mlist++->move = makeMovePromote<MT>(from, firstOne<T_HIGH>(to_1_3), p, b);

            to_1_7 &= TO3_7_MASK_HIGH[T];

            while (to_1_7) // 不成を生成
                mlist++->move = makeMove<MT>(from, firstOne<T_HIGH>(to_1_7), p, b);
        }

        return mlist;
    }

    // 銀を動かす手
    template <MoveType MT, Turn T, bool ALL>
    inline MoveStack* generateSilverMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        const Piece p = T == BLACK ? B_SILVER : W_SILVER;

        Bitboard bb_from = b.bbPiece(SILVER, T);
        uint64_t from_1_3 = bb_from.b(T_HIGH) & ENEMY_MASK[T];

        while (from_1_3) // 敵陣の銀
        {
            const Square from = firstOne<T_HIGH>(from_1_3);
            uint64_t to_1_7 = silverAttack(T, from).b(T_HIGH) & target.b(T_HIGH);

            while (to_1_7) // 移動先はHIGH盤面に収まっている。
            {
                const Square to = firstOne<T_HIGH>(to_1_7);
                mlist++->move = makeMovePromote<MT>(from, to, p, b);
                mlist++->move = makeMove<MT>(from, to, p, b);
            }
        }

        uint64_t from_4_9 = bb_from.b(T_LOW) & SELF_MASK[T];

        while (from_4_9) // 非敵陣の銀
        {
            const Square from = firstOne<T_LOW>(from_4_9);
            uint64_t to_3_9 = silverAttack(T, from).b(T_LOW) & target.b(T_LOW);
            uint64_t to_3 = to_3_9 & RANK3_MASK_LOW[T];

            while (to_3_9)
                mlist++->move = makeMove<MT>(from, firstOne<T_LOW>(to_3_9), p, b);

            while (to_3)
                mlist++->move = makeMovePromote<MT>(from, firstOne<T_LOW>(to_3), p, b); 
        }

        return mlist;
    }

    // 王を動かす手
    template <MoveType MT, Turn T, bool ALL>
    inline MoveStack* generateKingMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        const Piece p = T == BLACK ? B_KING : W_KING;
        const Square from = b.kingSquare(T);

        if (canPromote(T, from))
            for (uint64_t to_1_7 = kingAttack(from).b(T_HIGH) & target.b(T_HIGH); to_1_7; mlist++->move = makeMove<MT>(from, firstOne<T_HIGH>(to_1_7), p, b));
        else
            for (uint64_t to_3_9 = kingAttack(from).b(T_LOW ) & target.b(T_LOW ); to_3_9; mlist++->move = makeMove<MT>(from, firstOne<T_LOW >(to_3_9), p, b));

        return mlist;
    }

    // MTで指定された指し手を生成する。玉以外。
    // 第3引数のtargetは渡すときに駒取りなら駒のあるところ、そうでないなら駒のないところになっている
    template <MoveType MT, Turn T, bool ALL>
    inline MoveStack* generateOtherMove(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        mlist = generateBishopRookMove <MT, T, BISHOP, ALL>(mlist, b, target);
        mlist = generateBishopRookMove <MT, T, ROOK,   ALL>(mlist, b, target);
        mlist = generateHorseDragonMove<MT, T, HORSE,  ALL>(mlist, b, target);
        mlist = generateHorseDragonMove<MT, T, DRAGON, ALL>(mlist, b, target);
        mlist = generateSilverMove     <MT, T,         ALL>(mlist, b, target);
        mlist = generateGoldMove       <MT, T,         ALL>(mlist, b, target);
        mlist = generateLanceMove      <MT, T,         ALL>(mlist, b, target);
        mlist = generateKnightMove     <MT, T,         ALL>(mlist, b, target);
        return mlist;
    }

    // toの位置での取り返しの手
    template <Turn T>
    inline MoveStack* generateRecapture(MoveStack* mlist, const Board& b, const Square to)
    {
        constexpr Rank TRank3 = T == BLACK ? RANK_3 : RANK_7;
        Bitboard bb = b.attackers(T, to);
        Square from;
        bool can_promote_to = canPromote(T, to);
        Piece c = b.piece(to);

        // TODO:香車、桂馬、銀の不成を生成していない。
        FORBB(bb, from, {
            const Piece p = b.piece(from);
            if (isNoPromotable(p))
                mlist++->move = makeMove(from, to, p, c, false);
            else
            {
                bool can_promote_from = canPromote(T, from);
                if (can_promote_from || can_promote_to)
                {
                    mlist++->move = makeMove(from, to, p, c, true);

                    if (typeOf(p) == SILVER
                        || ((typeOf(p) == LANCE || typeOf(p) == KNIGHT) && rankOf(to) == TRank3))
                        mlist++->move = makeMove(from, to, p, c, false);
                }
                else
                {
                    mlist++->move = makeMove(from, to, p, c, false);
                }
            }
        });

        return mlist;
    }

    // 駒を打つ手
    // 持ち駒が多くなってくるとここの処理が半分以上を占める。
#ifdef UNROLL
    // 駒を打つ手だが、40通りにswitchで分岐したもの
    // 歩の有無(2通り)*香車の有無(2通り)*桂馬の有無(2通り)*それ以外の駒の枚数(0～4の5通り) = 40通り
    template <Turn T>
    inline MoveStack* generateDrop(MoveStack* mlist, const Board& b, const Bitboard &target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        const Rank TRANK1 = T == BLACK ? RANK_1 : RANK_9;
        constexpr Move dp = makeDrop(T == BLACK ? B_PAWN   : W_PAWN);
        constexpr Move dl = makeDrop(T == BLACK ? B_LANCE  : W_LANCE);
        constexpr Move dk = makeDrop(T == BLACK ? B_KNIGHT : W_KNIGHT);
        constexpr uint32_t p = Hand::pMask(), l = Hand::lMask(), k = Hand::kMask();
        
        int hand_num = 0;
        Move pc[7];
        Square to;
        const uint32_t hand_bit = b.hand(T).existsBit();

        if (hand_bit & (1 << 0)) { pc[hand_num++] = makeDrop(T == BLACK ? B_BISHOP : W_BISHOP); }
        if (hand_bit & (1 << 1)) { pc[hand_num++] = makeDrop(T == BLACK ? B_ROOK   : W_ROOK);   }
        if (hand_bit & (1 << 5)) { pc[hand_num++] = makeDrop(T == BLACK ? B_SILVER : W_SILVER); }
        if (hand_bit & (1 << 6)) { pc[hand_num++] = makeDrop(T == BLACK ? B_GOLD   : W_GOLD);   }

#ifdef USE_YMM
        __m256i move256, move256_2;
#endif
        switch (hand_bit & Hand::plkMask())
        {
        case 0: // 歩香桂を持っていない場合
            switch (hand_num)
            {
            case 0: break; // それ以外もないなら終わり。
            case 1: FORBB(target, to, UROL1(mlist++->move = Move(pc[i] | to))); break;
#ifdef USE_YMM
            case 2: 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]); 
                FORBB(target, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); break;
            case 3: 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]); 
                FORBB(target, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); break;
            case 4: 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]); 
                FORBB(target, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); break;
#else
            case 2: FORBB(target, to, UROL2(mlist++->move = Move(pc[i] | to))); break;
            case 3: FORBB(target, to, UROL3(mlist++->move = Move(pc[i] | to))); break;
            case 4: FORBB(target, to, UROL4(mlist++->move = Move(pc[i] | to))); break;
#endif
            default: UNREACHABLE;
            }
            break;
        case p: // 歩を持っている場合
        {
            // 歩が打てる場所
            Bitboard target_p = b.bbPiece(PAWN, T).pawnDropable(T) & target;

            // 歩が打てない場所で開いてる場所
            Bitboard target_o = target & ~target_p;
            pc[hand_num] = dp;

            switch (hand_num)
            {
            case 0: FORBB(target_p, to, mlist++->move = Move(dp | to)); break;
#ifdef USE_YMM
            case 1:
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FORBB(target_o, to, mlist++->move = Move(pc[0] | to)); break;
            case 2: 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FORBB(target_o, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); break;
            case 3: 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; });
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FORBB(target_o, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); break;
            case 4:
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4;
                mlist++->move = Move(pc[0] | to); });
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FORBB(target_o, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); break;
#else
            case 1: FORBB(target_p, to, UROL2(mlist++->move = Move(pc[i] | to))); FORBB(target_o, to, UROL1(mlist++->move = Move(pc[i] | to))); break;
            case 2: FORBB(target_p, to, UROL3(mlist++->move = Move(pc[i] | to))); FORBB(target_o, to, UROL2(mlist++->move = Move(pc[i] | to))); break;
            case 3: FORBB(target_p, to, UROL4(mlist++->move = Move(pc[i] | to))); FORBB(target_o, to, UROL3(mlist++->move = Move(pc[i] | to))); break;
            case 4: FORBB(target_p, to, UROL5(mlist++->move = Move(pc[i] | to))); FORBB(target_o, to, UROL4(mlist++->move = Move(pc[i] | to))); break;
#endif
            default:
                UNREACHABLE;
            }
            break;
        }
        case l: // 香車を持っている場合
        {
            Bitboard target_l = target & ~mask(TRANK1);
            uint64_t target_o64 = target.b(T_HIGH) & RANK1_MASK_HIGH[T];
            pc[hand_num] = dl;

            switch (hand_num)
            {
            case 0: FORBB(target_l, to, mlist++->move = Move(dl | to)); break;
#ifdef USE_YMM
            case 1: 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FORBB(target_l, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FOR64(target_o64, to, T_HIGH, mlist++->move = Move(pc[0] | to)); break;
            case 2: 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FORBB(target_l, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); break;
            case 3:  
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FORBB(target_l, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); break;
            case 4: 
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FORBB(target_l, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); });
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); break;
#else
            case 1: FORBB(target_l, to, UROL2(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to))); break;
            case 2: FORBB(target_l, to, UROL3(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to))); break;
            case 3: FORBB(target_l, to, UROL4(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to))); break;
            case 4: FORBB(target_l, to, UROL5(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to))); break;
#endif
            default:
                UNREACHABLE;
            }
            break;
        }
        case p | l:
        {
            Bitboard target_p = b.bbPiece(PAWN, T).pawnDropable(T) & target;

            // 香車だけが打てる場所
            Bitboard target_l = target & ~mask(TRANK1) & ~target_p;

            // 歩も香も打てない場所
            uint64_t target_o64 = target.b(T_HIGH) & RANK1_MASK_HIGH[T];

            pc[hand_num] = dl;
            pc[hand_num + 1] = dp;

            switch (hand_num)
            {
#ifdef USE_YMM
            case 0: 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FORBB(target_l, to, mlist++->move = Move(pc[0] | to)); break;
            case 1: 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FORBB(target_l, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); 
                FOR64(target_o64, to, T_HIGH, mlist++->move = Move(pc[0] | to)); break;
            case 2: 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FORBB(target_l, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); break;
            case 3:
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); }); 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FORBB(target_l, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); break;
            case 4: 
                move256 = _mm256_set_epi64x(pc[5], pc[4], pc[3], pc[2]), move256_2 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FORBB(target_p, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                                      _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256_2)); mlist += 2; }); 
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FORBB(target_l, to, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4;
                mlist++->move = Move(pc[0] | to); }); 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]); 
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); break;
#else
            case 0: FORBB(target_p, to, UROL2(mlist++->move = Move(pc[i] | to)));  FORBB(target_l, to, UROL1(mlist++->move = Move(pc[i] | to))); break;
            case 1: FORBB(target_p, to, UROL3(mlist++->move = Move(pc[i] | to)));  FORBB(target_l, to, UROL2(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to)));  break;
            case 2: FORBB(target_p, to, UROL4(mlist++->move = Move(pc[i] | to)));  FORBB(target_l, to, UROL3(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to)));  break;
            case 3: FORBB(target_p, to, UROL5(mlist++->move = Move(pc[i] | to)));  FORBB(target_l, to, UROL4(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to)));  break;
            case 4: FORBB(target_p, to, UROL6(mlist++->move = Move(pc[i] | to)));  FORBB(target_l, to, UROL5(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to)));  break;
#endif
            default:
                UNREACHABLE;
            }
            break;
        }
        case k:
        {
            uint64_t target_k64 = target.b(T_LOW);
            uint64_t target_o64 = target.b(T_HIGH) & EXCEPT_MASK[T_HIGH];
            pc[hand_num] = dk;

            switch (hand_num)
            {
            case 0: FOR64(target_k64, to, T_LOW, UROL1(mlist++->move = Move(pc[i] | to))); break;
#ifdef USE_YMM
            case 1: 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_k64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FOR64(target_o64, to, T_HIGH, mlist++->move = Move(pc[0] | to)); break;
            case 2: 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_k64, to, T_LOW,  { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); break;
            case 3: 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_k64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); break;
            case 4: 
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FOR64(target_k64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); }); 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); break;
#else
            case 1: FOR64(target_k64, to, T_LOW, UROL2(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to))); break;
            case 2: FOR64(target_k64, to, T_LOW, UROL3(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to))); break;
            case 3: FOR64(target_k64, to, T_LOW, UROL4(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to))); break;
            case 4: FOR64(target_k64, to, T_LOW, UROL5(mlist++->move = Move(pc[i] | to))); FOR64(target_o64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to))); break;
#endif
            default:
                UNREACHABLE;
            }
            break;
        }
        case k | p:
        {
            Bitboard target_p = b.bbPiece(PAWN, T).pawnDropable(T) & target;

            // 歩と桂馬両方打てる場所(=LOW盤面かつ歩が打てる場所）
            uint64_t target_pk64 = target_p.b(T_LOW);

            // 歩だけ打てる場所(=2段目）
            uint64_t target_p64 = target_p.b(T_HIGH) & RANK2_MASK_HIGH[T];

            // 桂馬だけ打てる場所
            uint64_t target_k64 = target.b(T_LOW) & ~target_pk64;

            // 歩も桂馬も打てない場所(=2段目以上で歩が打てない場所)
            uint64_t target_o64 = target.b(T_HIGH) & EXCEPT_MASK[T_HIGH] & ~target_p64;

            pc[hand_num] = dk;
            pc[hand_num + 1] = dp;

            switch (hand_num)
            {
#ifdef USE_YMM
            case 0: 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_pk64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FOR64(target_k64, to, T_LOW,  mlist++->move = Move(dk | to));
                FOR64(target_p64, to, T_HIGH, mlist++->move = Move(dp | to)); break;
            case 1: 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_pk64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); 
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_k64, to, T_LOW,  { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); 
                move256 = _mm256_set_epi64x(0, 0, pc[2], pc[0]);
                FOR64(target_p64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FOR64(target_o64, to, T_HIGH, mlist++->move = Move(pc[0] | to)); break;
            case 2: 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_pk64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_k64,  to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); 
                move256 = _mm256_set_epi64x(0, pc[3], pc[1], pc[0]);
                FOR64(target_p64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); break;
            case 3:
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FOR64(target_pk64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); }); 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_k64,  to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); 
                move256 = _mm256_set_epi64x(pc[4], pc[2], pc[1], pc[0]);
                FOR64(target_p64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); 
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); break;
            case 4: 
                move256 = _mm256_set_epi64x(pc[5], pc[4], pc[3], pc[2]), move256_2 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_pk64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                                                _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256_2)); mlist += 2; }); 
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FOR64(target_k64,  to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); }); 
                move256 = _mm256_set_epi64x(pc[5], pc[3], pc[2], pc[1]);
                FOR64(target_p64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); }); 
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); break;
#else
            case 0:
                FOR64(target_pk64, to, T_LOW, UROL2(mlist++->move = Move(pc[i] | to)));
                FOR64(target_k64, to,  T_LOW, UROL1(mlist++->move = Move(pc[i] | to))); pc[hand_num] = dp;
                FOR64(target_p64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to)));
                break;
            case 1:
                FOR64(target_pk64, to, T_LOW, UROL3(mlist++->move = Move(pc[i] | to)));
                FOR64(target_k64, to,  T_LOW, UROL2(mlist++->move = Move(pc[i] | to))); pc[hand_num] = dp;
                FOR64(target_p64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to)));
                break;
            case 2:
                FOR64(target_pk64, to, T_LOW, UROL4(mlist++->move = Move(pc[i] | to)));
                FOR64(target_k64, to,  T_LOW, UROL3(mlist++->move = Move(pc[i] | to))); pc[hand_num] = dp;
                FOR64(target_p64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to)));
                break;
            case 3:
                FOR64(target_pk64, to, T_LOW, UROL5(mlist++->move = Move(pc[i] | to)));
                FOR64(target_k64, to,  T_LOW, UROL4(mlist++->move = Move(pc[i] | to))); pc[hand_num] = dp;
                FOR64(target_p64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to)));
                break;
            case 4:
                FOR64(target_pk64, to, T_LOW, UROL6(mlist++->move = Move(pc[i] | to)));
                FOR64(target_k64,  to, T_LOW, UROL5(mlist++->move = Move(pc[i] | to))); pc[hand_num] = dp;
                FOR64(target_p64, to, T_HIGH, UROL5(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to)));
                break;
#endif
            default:
                UNREACHABLE;
            }
            break;
        }
        case k | l:
        {
            // 桂馬が打てる場所(=香車も打てる）
            uint64_t target_k64 = target.b(T_LOW);

            // 桂馬が打てない場所で香車が打てる場所(=2段目)
            uint64_t target_l64 = target.b(T_HIGH) & RANK2_MASK_HIGH[T];

            // 桂馬も香車も打てない場所
            uint64_t target_o64 = target.b(T_HIGH) & RANK1_MASK_HIGH[T];

            pc[hand_num] = dl;
            pc[hand_num + 1] = dk;

            switch (hand_num)
            {
#ifdef USE_YMM
            case 0:
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_k64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FOR64(target_l64, to, T_HIGH, mlist++->move = Move(dl | to)); break;
            case 1:
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_k64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_l64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FOR64(target_o64, to, T_HIGH, mlist++->move = Move(pc[0] | to)); break;
            case 2:
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_k64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; });
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_l64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); break;
            case 3:
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FOR64(target_k64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4;
                mlist++->move = Move(pc[0] | to); });
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_l64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; });
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); break;
            case 4:
                move256 = _mm256_set_epi64x(pc[5], pc[4], pc[3], pc[2]), move256_2 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_k64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4;
                                               _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256_2)); mlist += 2; });
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FOR64(target_l64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); });
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); break;
#else
            case 0: 
                FOR64(target_k64, to, T_LOW,  UROL2(mlist++->move = Move(pc[i] | to))); 
                FOR64(target_l64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to)));
                break;
            case 1:
                FOR64(target_k64, to, T_LOW,  UROL3(mlist++->move = Move(pc[i] | to))); 
                FOR64(target_l64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to)));				
                break;
            case 2:
                FOR64(target_k64, to, T_LOW,  UROL4(mlist++->move = Move(pc[i] | to)));
                FOR64(target_l64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to)));
                break;
            case 3:
                FOR64(target_k64, to, T_LOW,  UROL5(mlist++->move = Move(pc[i] | to)));
                FOR64(target_l64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to)));
                break;
            case 4:
                FOR64(target_k64, to, T_LOW,  UROL6(mlist++->move = Move(pc[i] | to)));
                FOR64(target_l64, to, T_HIGH, UROL5(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to)));
                break;
#endif
            default:
                UNREACHABLE;
            }
            break;
        }
        case p | k | l:
        {
            // 歩が打てる場所
            Bitboard target_p = b.bbPiece(PAWN, T).pawnDropable(T) & target;

            // 歩と桂馬と香車すべてが打てる場所
            uint64_t target_pkl64 = target_p.b(T_LOW);

            // 歩と香車だけ打てる場所(=2段目）
            uint64_t target_pl64 = target_p.b(T_HIGH) & RANK2_MASK_HIGH[T];

            // 桂馬と香車だけ打てる場所
            uint64_t target_kl64 = target.b(T_LOW) & ~target_pkl64;

            // 香車だけ打てる場所
            uint64_t target_l64 = target.b(T_HIGH) & RANK2_MASK_HIGH[T] & ~target_pl64;

            // 歩も香車も桂馬も打てない場所(=1段目)
            uint64_t target_o64 = target.b(T_HIGH) & RANK1_MASK_HIGH[T];

            pc[hand_num] = dl;
            pc[hand_num + 1] = dp;
            pc[hand_num + 2] = dk;

            switch (hand_num)
            {
#ifdef USE_YMM
            case 0:
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_pkl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_pl64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                move256 = _mm256_set_epi64x(0, 0, pc[2], pc[0]);
                FOR64(target_kl64, to, T_LOW,  { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; });
                FOR64(target_l64, to, T_HIGH, mlist++->move = Move(pc[0] | to)); break;
            case 1:
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_pkl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; });
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_pl64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, pc[3], pc[1], pc[0]);
                FOR64(target_kl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_l64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); 
                FOR64(target_o64, to, T_HIGH, mlist++->move = Move(pc[0] | to)); break;
            case 2:
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FOR64(target_pkl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); });
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_pl64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; });
                move256 = _mm256_set_epi64x(pc[4], pc[2], pc[1], pc[0]);
                FOR64(target_kl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; });
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_l64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; });
                move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 2; }); break;
            case 3:
                move256 = _mm256_set_epi64x(pc[5], pc[4], pc[3], pc[2]), move256_2 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_pkl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                                                 _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256_2)); mlist += 2; });
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FOR64(target_pl64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); });
                move256 = _mm256_set_epi64x(pc[5], pc[3], pc[2], pc[1]);
                FOR64(target_kl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); });
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_l64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; });
                move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 3; }); break;
            case 4:
                move256 = _mm256_set_epi64x(pc[6], pc[5], pc[4], pc[3]), move256_2 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]);
                FOR64(target_pkl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4;
                                                 _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256_2)); mlist += 3; });
                move256 = _mm256_set_epi64x(pc[5], pc[4], pc[3], pc[2]); move256_2 = _mm256_set_epi64x(0, 0, pc[1], pc[0]);
                FOR64(target_pl64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                                                 _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256_2)); mlist += 2; });
                move256 = _mm256_set_epi64x(pc[6], pc[4], pc[3], pc[2]);
                FOR64(target_kl64, to, T_LOW, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4;
                                                _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256_2)); mlist += 2; });
                move256 = _mm256_set_epi64x(pc[4], pc[3], pc[2], pc[1]);
                FOR64(target_l64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; 
                mlist++->move = Move(pc[0] | to); });
                move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                FOR64(target_o64, to, T_HIGH, { _mm256_storeu_si256((__m256i*)mlist, _mm256_or_si256(YMM_TO[to], move256)); mlist += 4; }); break;
#else
            case 0:
                FOR64(target_pkl64, to, T_LOW, UROL3(mlist++->move = Move(pc[i] | to)));
                FOR64(target_pl64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to))); pc[hand_num + 1] = dk;
                FOR64(target_kl64, to,  T_LOW, UROL2(mlist++->move = Move(pc[i] | to))); 
                FOR64(target_l64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to)));
                break;
            case 1:
                FOR64(target_pkl64, to, T_LOW, UROL4(mlist++->move = Move(pc[i] | to)));
                FOR64(target_pl64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to))); pc[hand_num + 1] = dk;
                FOR64(target_kl64, to, T_LOW, UROL3(mlist++->move = Move(pc[i] | to)));
                FOR64(target_l64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL1(mlist++->move = Move(pc[i] | to)));
                break;
            case 2:
                FOR64(target_pkl64, to, T_LOW, UROL5(mlist++->move = Move(pc[i] | to)));
                FOR64(target_pl64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to))); pc[hand_num + 1] = dk;
                FOR64(target_kl64, to, T_LOW, UROL4(mlist++->move = Move(pc[i] | to)));
                FOR64(target_l64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL2(mlist++->move = Move(pc[i] | to)));
                break;
            case 3:
                FOR64(target_pkl64, to, T_LOW, UROL6(mlist++->move = Move(pc[i] | to)));
                FOR64(target_pl64, to, T_HIGH, UROL5(mlist++->move = Move(pc[i] | to))); pc[hand_num + 1] = dk;
                FOR64(target_kl64, to, T_LOW, UROL5(mlist++->move = Move(pc[i] | to)));
                FOR64(target_l64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL3(mlist++->move = Move(pc[i] | to)));
                break;
            case 4:
                FOR64(target_pkl64, to, T_LOW, UROL7(mlist++->move = Move(pc[i] | to)));
                FOR64(target_pl64, to, T_HIGH, UROL6(mlist++->move = Move(pc[i] | to))); pc[hand_num + 1] = dk;
                FOR64(target_kl64, to, T_LOW, UROL6(mlist++->move = Move(pc[i] | to)));
                FOR64(target_l64, to, T_HIGH, UROL5(mlist++->move = Move(pc[i] | to)));
                FOR64(target_o64, to, T_HIGH, UROL4(mlist++->move = Move(pc[i] | to)));
                break;
#endif
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

#else
    template <Turn T>
    inline MoveStack* generateDrop(MoveStack* mlist, const Board& b, const Bitboard& target)
    {
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW  = T == BLACK ? LOW : HIGH;
        const Hand hand = b.hand(T);
        const Piece P = T == BLACK ? B_PAWN : W_PAWN;

        if (hand.exists(PAWN)) // 歩(打ち歩詰めも生成する)
        {
            const Move drop = makeDrop(P);
            Bitboard pawn_to = b.bbPiece(PAWN, T).pawnDropable(T);

            for (uint64_t to_1_2 = pawn_to.b(T_HIGH) & target.b(T_HIGH) & EXCEPT_MASK[T_HIGH]; to_1_2; mlist++->move = Move(drop | firstOne<T_HIGH>(to_1_2)));			
            for (uint64_t to_3_9 = pawn_to.b(T_LOW) & target.b(T_LOW); to_3_9; mlist++->move = Move(drop | firstOne<T_LOW>(to_3_9)));
        }

        if (hand.existsExceptPawn())
        {
            int hand_num = 0;
            Move pc[6];

            // 桂馬、香車、それ以外の順番で格納する（駒を打てる位置が限定的な順）
            if (hand.exists(KNIGHT)) { pc[hand_num++] = makeDrop(T == BLACK ? B_KNIGHT : W_KNIGHT); } const int nk  = hand_num;
            if (hand.exists(LANCE))  { pc[hand_num++] = makeDrop(T == BLACK ? B_LANCE  : W_LANCE);  } const int nkl = hand_num;
            if (hand.exists(SILVER)) { pc[hand_num++] = makeDrop(T == BLACK ? B_SILVER : W_SILVER); }
            if (hand.exists(GOLD))   { pc[hand_num++] = makeDrop(T == BLACK ? B_GOLD   : W_GOLD);   }
            if (hand.exists(BISHOP)) { pc[hand_num++] = makeDrop(T == BLACK ? B_BISHOP : W_BISHOP); }
            if (hand.exists(ROOK))   { pc[hand_num++] = makeDrop(T == BLACK ? B_ROOK   : W_ROOK);   }

            uint64_t hand_to;
            Square to;

            // 3段目から9段目にかけて持ち駒を打つ手を生成
            switch (hand_num)
            {
            case 0: break;
            case 1: hand_to = target.b(T_LOW); FOR64(hand_to, to, T_LOW, UROL1(mlist++->move = Move(pc[i] | to))); break;
#ifdef USE_YMM
            case 2:
                hand_to = target.b(T_LOW);
                for (__m256i move256 = _mm256_set_epi64x(0, 0, pc[1], pc[0]); hand_to; mlist += 2)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_LOW>(hand_to)], move256);
                break;
            case 3:
                hand_to = target.b(T_LOW);
                for (__m256i move256 = _mm256_set_epi64x(0, pc[2], pc[1], pc[0]); hand_to; mlist += 3)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_LOW>(hand_to)], move256);
                break;
            case 4:
                hand_to = target.b(T_LOW);
                for (__m256i move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]); hand_to; mlist += 4)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_LOW>(hand_to)], move256);
                break;
            case 5:
                hand_to = target.b(T_LOW);
                for (__m256i move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]); hand_to; mlist += 5)
                {
                    const Square to = firstOne<T_LOW>(hand_to);
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[to], move256);
                    mlist[4].move = Move(pc[4] | to);
                }
                break;
            case 6:
                hand_to = target.b(T_LOW);
                __m256i move256 = _mm256_set_epi64x(pc[3], pc[2], pc[1], pc[0]);
                __m256i move256_2 = _mm256_set_epi64x(0, 0, pc[5], pc[4]);

                while (hand_to)
                {
                    const Square to = firstOne<T_LOW>(hand_to);
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[to], move256);
                    *(__m256i*)(mlist + 4) = _mm256_or_si256(YMM_TO[to], move256_2);
                    mlist += 6;
                }
                break;
#else
            case 2: hand_to = target.b(T_LOW); FOR64(hand_to, to, T_LOW, UROL2(mlist++->move = Move(pc[i] | to))); break;
            case 3:	hand_to = target.b(T_LOW); FOR64(hand_to, to, T_LOW, UROL3(mlist++->move = Move(pc[i] | to))); break;
            case 4: hand_to = target.b(T_LOW); FOR64(hand_to, to, T_LOW, UROL4(mlist++->move = Move(pc[i] | to))); break;
            case 5: hand_to = target.b(T_LOW); FOR64(hand_to, to, T_LOW, UROL5(mlist++->move = Move(pc[i] | to))); break;
            case 6:	hand_to = target.b(T_LOW); FOR64(hand_to, to, T_LOW, UROL6(mlist++->move = Move(pc[i] | to))); break;
#endif
            default: UNREACHABLE;
            }
            
            // 桂香以外の持ち駒に対する生成
            switch (hand_num - nkl)
            {
            case 0: break; // 桂馬、香車以外の持ち駒がない
            case 1: hand_to = target.b(T_HIGH) & RANK1_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL1(mlist++->move = Move(pc[nkl + i] | to))); break;
#ifdef USE_YMM
            case 2:
                hand_to = target.b(T_HIGH) & RANK1_MASK_HIGH[T];
                for (__m256i move256 = _mm256_set_epi64x(0, 0, pc[nkl + 1], pc[nkl + 0]); hand_to; mlist += 2)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_HIGH>(hand_to)], move256);
                break;
            case 3:
                hand_to = target.b(T_HIGH) & RANK1_MASK_HIGH[T];
                for (__m256i move256 = _mm256_set_epi64x(0, pc[nkl + 2], pc[nkl + 1], pc[nkl + 0]); hand_to; mlist += 3)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_HIGH>(hand_to)], move256);
                break;
            case 4:
            {
                hand_to = target.b(T_HIGH) & RANK1_MASK_HIGH[T];
                for (__m256i move256 = _mm256_set_epi64x(pc[nkl + 3], pc[nkl + 2], pc[nkl + 1], pc[nkl + 0]); hand_to; mlist += 4)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_HIGH>(hand_to)], move256);
                break;
            }
#else
            case 2: hand_to = target.b(T_HIGH) & RANK1_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL2(mlist++->move = Move(pc[nkl + i] | to))); break;
            case 3: hand_to = target.b(T_HIGH) & RANK1_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL3(mlist++->move = Move(pc[nkl + i] | to))); break;
            case 4: hand_to = target.b(T_HIGH) & RANK1_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL4(mlist++->move = Move(pc[nkl + i] | to))); break;
#endif
            default: UNREACHABLE;
            }

            // 桂馬以外の持ち駒があれば
            // 8段目に対して、桂馬以外の差し手を生成
            switch (hand_num - nk)
            {
            case 0: break; // 桂馬以外の持ち駒がない
            case 1: hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL1(mlist++->move = Move(pc[nk + i] | to))); break;
#ifdef USE_YMM
            case 2:
                hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T];
                for (__m256i move256 = _mm256_set_epi64x(0, 0, pc[nk + 1], pc[nk + 0]); hand_to; mlist += 2)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_HIGH>(hand_to)], move256);
                break;
            case 3:
                hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T];
                for (__m256i move256 = _mm256_set_epi64x(0, pc[nk + 2], pc[nk + 1], pc[nk + 0]); hand_to; mlist += 3)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_HIGH>(hand_to)], move256);
                break;
            case 4:
                hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T];
                for (__m256i move256 = _mm256_set_epi64x(pc[nk + 3], pc[nk + 2], pc[nk + 1], pc[nk + 0]); hand_to; mlist += 4)
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[firstOne<T_HIGH>(hand_to)], move256);
                break;
            case 5:
                hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T];
                for (__m256i move256 = _mm256_set_epi64x(pc[nk + 3], pc[nk + 2], pc[nk + 1], pc[nk + 0]); hand_to; mlist += 5)
                {
                    const Square to = firstOne<T_HIGH>(hand_to);
                    *(__m256i*)mlist = _mm256_or_si256(YMM_TO[to], move256);
                    mlist[4].move = Move(pc[nk + 4] | to);
                }
                break;
#else
            case 2: hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL2(mlist++->move = Move(pc[nk + i] | to))); break;
            case 3: hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL3(mlist++->move = Move(pc[nk + i] | to))); break;
            case 4: hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL4(mlist++->move = Move(pc[nk + i] | to))); break;
            case 5: hand_to = target.b(T_HIGH) & RANK2_MASK_HIGH[T]; FOR64(hand_to, to, T_HIGH, UROL5(mlist++->move = Move(pc[nk + i] | to))); break;
#endif
            default: UNREACHABLE;
            }
        }

        return mlist;
    }
#endif
    
    // king_squareにいる玉がcheck_squareにいる駒で王手されたとき、その玉が動けない場所のbitboardを求める
    inline Bitboard makeBannedKingTo(const Board& b, const Square check_sq, const Square king_sq)
    {
        assert(typeOf(b.piece(king_sq)) == KING && turnOf(b.piece(king_sq)) != turnOf(b.piece(check_sq)));
        const Piece pc  = b.piece(check_sq);

        switch (pc)
        {

        case B_PAWN: case B_KNIGHT: case W_PAWN: case W_KNIGHT: return allZeroMask();

        case B_SILVER:	return silverAttack(BLACK, check_sq);
        case B_LANCE:   return lanceAttackToEdge(BLACK, check_sq);
        case B_GOLD: case B_PRO_PAWN: case B_PRO_LANCE: case B_PRO_KNIGHT: case B_PRO_SILVER: return goldAttack(BLACK, check_sq);

        case W_SILVER:	return silverAttack(WHITE, check_sq);
        case W_LANCE:   return lanceAttackToEdge(WHITE, check_sq);
        case W_GOLD: case W_PRO_PAWN: case W_PRO_LANCE: case W_PRO_KNIGHT: case W_PRO_SILVER: return goldAttack(WHITE, check_sq);

        case B_BISHOP: case W_BISHOP:	return bishopAttackToEdge(check_sq);
        case B_ROOK:   case W_ROOK:		return rookAttackToEdge(check_sq);
        case B_HORSE:  case W_HORSE:	return horseAttackToEdge(check_sq);
        case B_DRAGON: case W_DRAGON:   
            // 斜めから王手したときは、玉の移動先と王手した駒の間に駒があることがあるので、dragonAttackToEdge(check_sq) は使えない。
            return relation(check_sq, king_sq) >= DIRECT_DIAG1 ? dragonAttack(check_sq, b.bbOccupied()) : dragonAttackToEdge(check_sq);

        case B_KING:   case W_KING:		return kingAttack(check_sq);

        default: UNREACHABLE;
        }

        return allZeroMask();
    }

    // 王手回避手
    template <Turn T, bool ALL>
    inline MoveStack* generateEvasion(MoveStack* mlist, const Board& b)
    {
        const Square king_sq = b.kingSquare(T);
        const Turn enemy = ~T;
        Bitboard bb_to_banned_king = allZeroMask();
        const Bitboard checkers = b.bbCheckers();
        int checkers_num = 0;
        Square check_sq;

        Bitboard bb = checkers;

        do { // 玉が逃げられない位置のbitboardを作成する。
            check_sq = bb.firstOne();
            assert(turnOf(b.piece(check_sq)) == enemy);
            ++checkers_num;
            bb_to_banned_king |= makeBannedKingTo(b, check_sq, king_sq);
        } while (bb);

        // 玉が動ける場所 == 王手している駒の利きがない && 自分の駒がない && 玉の動ける場所
        //bb = ~bb_to_banned_king & ~b.bbTurn(T) & kingAttack(king_sq);
        bb = bb_to_banned_king.notThisAnd(b.bbTurn(T).notThisAnd(kingAttack(king_sq)));

        // 移動先に敵の利きがあるか調べずに指し手を生成する。
        while (bb)
            mlist++->move = makeMove<EVASIONS>(king_sq, bb.firstOne(), T == BLACK ? B_KING : W_KING, b);
            
        // 両王手なら、玉を移動するしか回避方法はない。玉の移動は生成したので、ここで終了
        if (checkers_num >= 2)
            return mlist;

        // 王手している駒を玉以外で取る手の生成。pinされているかどうかは、move_pickerかsearchで調べる
        const Bitboard target1 = bbBetween(check_sq, king_sq);
        const Bitboard target2 = target1 | checkers;

        mlist = generatePawnMove <EVASIONS, T, ALL>(mlist, b, target2);
        mlist = generateOtherMove<EVASIONS, T, ALL>(mlist, b, target2);

        // 飛び駒の王手の場合、合駒をする手を生成
        if (target1)
            mlist = generateDrop<T>(mlist, b, target1);

        return mlist;
    }

    // 王手がかかっていない時の指し手生成。これは相手駒の利きのある地点に移動する自殺手と
    // pinされている駒を動かす自殺手を含む。
    template <Turn T, bool ALL>
    inline MoveStack* generateNoEvasion(MoveStack* mlist, const Board& b)
    {
        Bitboard target = b.bbEmpty();

        mlist = generateDrop<T>(mlist, b, target);

        // targetに敵の駒がいる場所も追加
        target ^= b.bbTurn(~T);
        mlist = generatePawnMove <NO_EVASIONS, T, ALL>(mlist, b, target);
        mlist = generateOtherMove<NO_EVASIONS, T, ALL>(mlist, b, target);
        mlist = generateKingMove <NO_EVASIONS, T, ALL>(mlist, b, target);

        return mlist;
    }

    // 連続王手の千日手以外の反則手を排除した合法手生成。
    // 重いので思考中には呼ばない。
    // ALLをtrueにするとEvasionのときに歩飛車角と、香車の2段目の不成も生成する
    template <Turn T, bool ALL>
    inline MoveStack* generateLegal(MoveStack* mlist, const Board& b)
    {
        MoveStack* curr = mlist;

        mlist = b.inCheck() ?
            generate<EVASIONS, T, ALL>(mlist, b) : generate<NO_EVASIONS, T, ALL>(mlist, b);

        // 玉の移動による自殺手と、pinされている駒の移動による自殺手を削除
        while (curr != mlist)
        {
            if (!b.pseudoLegal(curr->move) || !b.legal(curr->move))
                *curr = *(--mlist);
            else
                ++curr;
        }

        return mlist;
    }

    // 駒を取らない王手生成。
    template <Turn T, Index TK>
    MoveStack* generateNocapCheck(MoveStack* mlist, const Board& b, const Square ksq)
    {
        assert(!b.inCheck());

        const StateInfo* st = b.state();

        const Turn self = T;
        const Turn enemy = ~T;
        const Square tsouth = T == BLACK ? SQ_D : SQ_U;
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW = T == BLACK ? LOW : HIGH;

        // 駒を取らない王手生成なので、駒のないところがターゲット。
        const Bitboard target = b.bbEmpty();
        const uint64_t target64 = target.b(TK);
        const Hand h = b.hand(self);

        // 竜
        {
            Bitboard bb_from = b.bbPiece(DRAGON, self);

            if (bb_from)
            {
                Bitboard bb_target = target & st->check_sq[DRAGON];

                do {
                    const Square from = bb_from.firstOne();
                    Bitboard bb_to = bb_target & dragonAttack(from, b.bbOccupied());

                    // 玉が素抜かれるかどうかはlegalで。
                    while (bb_to)
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, bb_to.firstOne(), self == BLACK ? B_DRAGON : W_DRAGON, b);

                } while (bb_from);
            }
        }

        // 飛車
        {
            Bitboard bb_from = b.bbPiece(ROOK, self);
            uint64_t from123 = bb_from.b(T_HIGH) & ENEMY_MASK[T];

            // 敵陣からの移動

            if (from123)
            {
                Bitboard rook_target = target & st->check_sq[DRAGON];

                do {
                    const Square from = firstOne<T_HIGH>(from123);
                    Bitboard bb_to = rook_target & rookAttack(from, b.bbOccupied());

                    while (bb_to)
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, bb_to.firstOne(), self == BLACK ? B_ROOK : W_ROOK, b);

                } while (from123);
            }

            uint64_t from4_9 = bb_from.b(T_LOW) & SELF_MASK[T];


            // 敵陣ではないところからの移動
            if (from4_9)
            {
                uint64_t target_pro = st->check_sq[DRAGON].b(T_HIGH) & enemyMask(self).b(T_HIGH);
                Bitboard bb_target = enemyMask(self).notThisAnd(st->check_sq[ROOK]);

                do {
                    const Square from = firstOne<T_LOW>(from4_9);

                    // 飛車が動けば王手になるところ。敵陣なら、玉の斜めも含める。
                    Bitboard bb_to = target & rookAttack(from, b.bbOccupied());
                    uint64_t to64_123 = bb_to.b(T_HIGH) & target_pro;
                    bb_to &= bb_target;

                    while (bb_to)
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, bb_to.firstOne(), self == BLACK ? B_ROOK : W_ROOK, b);

                    while (to64_123)
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, firstOne<TK>(to64_123), self == BLACK ? B_ROOK : W_ROOK, b);
                } while (from4_9);
            }
        }
        // 馬
        {
            Bitboard bb_from = b.bbPiece(HORSE, self);

            if (bb_from)
            {
                Bitboard bb_target = target & st->check_sq[HORSE];

                do {
                    const Square from = bb_from.firstOne();
                    Bitboard bb_to = bb_target & horseAttack(from, b.bbOccupied());

                    while (bb_to)
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, bb_to.firstOne(), self == BLACK ? B_HORSE : W_HORSE, b);

                } while (bb_from);
            }
        }

        // 角
        {
            Bitboard bb_from = b.bbPiece(BISHOP, self);
            uint64_t from123 = bb_from.b(T_HIGH) & ENEMY_MASK[T];

            // 敵陣からの移動
            if (from123)
            {
                uint64_t target_pro = target.b(TK) & rookStepAttack(ksq).b(TK);
                Bitboard bb_target = target & st->check_sq[BISHOP];

                do {
                    const Square from = firstOne<T_HIGH>(from123);
                    Bitboard bishop_attack = bishopAttack(from, b.bbOccupied());
                    Bitboard bb_to = bb_target & bishop_attack;

                    while (bb_to)
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, bb_to.firstOne(), self == BLACK ? B_BISHOP : W_BISHOP, b);

                    // 成ることで王手になるところ。玉の十文字。
                    uint64_t to_cross64 = target_pro & bishop_attack.b(TK);

                    while (to_cross64)
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, firstOne<TK>(to_cross64), self == BLACK ? B_BISHOP : W_BISHOP, b);

                } while (from123);
            }

            uint64_t from4_9 = bb_from.b(T_LOW) & SELF_MASK[T];

            // 敵陣ではないところからの移動

            if (from4_9)
            {
                Bitboard bb_target = enemyMask(self).notThisAnd(st->check_sq[BISHOP]);
                uint64_t target_pro = st->check_sq[HORSE].b(T_HIGH) & enemyMask(self).b(T_HIGH);

                do {
                    const Square from = firstOne<T_LOW>(from4_9);

                    // 角が動けば王手になるところ。敵陣なら、玉の十文字も含める。
                    Bitboard bb_to = target & bishopAttack(from, b.bbOccupied());
                    uint64_t to64_123 = bb_to.b(T_HIGH) & target_pro;
                    bb_to &= bb_target;

                    while (bb_to)
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, bb_to.firstOne(), self == BLACK ? B_BISHOP : W_BISHOP, b);

                    while (to64_123)
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, firstOne<T_HIGH>(to64_123), self == BLACK ? B_BISHOP : W_BISHOP, b);

                } while (from4_9);
            }
        }

        // 駒移動 (両王手も含む）

        // 金、成金
        {
            uint64_t from64 = b.bbGold().b(TK) & b.bbTurn(self).b(TK) & goldCheck(self, ksq).b(TK);

            if (from64)
            {
                uint64_t gold_target = target.b(TK) & st->check_sq[GOLD].b(TK);

                do {
                    const Square from = firstOne<TK>(from64);
                    uint64_t to64 = gold_target & goldAttack(self, from).b(TK);

                    while (to64)
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), b.piece(from), b);
                } while (from64);
            }
        }

        // 銀
        {
            uint64_t from64 = b.bbPiece(SILVER, self).b(TK) & silverCheck(self, ksq).b(TK);

            if (from64)
            {
                // 成らなくても王手になる場所
                uint64_t chk = st->check_sq[SILVER].b(TK);

                // 成って王手になる場所
                uint64_t pro_chk = st->check_sq[GOLD].b(TK) & enemyMaskPlus1(self).b(TK);

                do
                {
                    const Square from = firstOne<TK>(from64);
                    uint64_t to64 = target64 & silverAttack(self, from).b(TK);
                    uint64_t to_pro = to64 & pro_chk;
                    to64 &= chk;

                    while (to_pro)
                    {
                        const Square to = firstOne<TK>(to_pro);

                        if (canPromote(self, from) || canPromote(self, to))
                            mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_SILVER : W_SILVER, b);
                    }

                    while (to64)
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_SILVER : W_SILVER, b);

                } while (from64);
            }
        }

        // 桂馬
        {
            // 桂馬はLOW盤面にしか存在できない
            uint64_t from64 = b.bbPiece(KNIGHT, self).b(T_LOW) & knightCheck(self, ksq).b(T_LOW);

            if (from64)
            {
                uint64_t chk_pro = target64 & st->check_sq[GOLD].b(TK) & enemyMask(self).b(TK);
                uint64_t chk = target64 & st->check_sq[KNIGHT].b(TK);

                do {
                    const Square from = firstOne<T_LOW>(from64);
                    uint64_t to64 = ~b.bbTurn(self).b(TK) & knightAttack(self, from).b(TK);
                    uint64_t to_pro = to64 & chk_pro;
                    to64 &= chk;

                    while (to_pro)
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, firstOne<TK>(to_pro), self == BLACK ? B_KNIGHT : W_KNIGHT, b);

                    while (to64)
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_KNIGHT : W_KNIGHT, b);

                } while (from64);
            }
        }

        // 香車
        {
            Bitboard bb_from = b.bbPiece(LANCE, self) & lanceCheck(self, ksq);
            const Rank TRank2 = self == BLACK ? RANK_2 : RANK_8;

            if (bb_from)
            {
                // 香車を動かす手で駒を取らない王手は、成りしかない。
                const uint64_t chk_pro = st->check_sq[GOLD].b(TK) & enemyMask(self).b(TK);

                do {
                    const Square from = bb_from.firstOne();
                    uint64_t to_pro = target64 & lanceAttack(self, from, b.bbOccupied()).b(TK) & chk_pro;

                    while (to_pro)
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, firstOne<TK>(to_pro), self == BLACK ? B_LANCE : W_LANCE, b);

                } while (bb_from);
            }
        }

        // 歩 成りは生成しない
        {
            const Rank krank = rankOf(ksq);

            if (isBehind(enemy, RANK_2, krank))
            {
                const Square to = ksq + tsouth;
                const Square from = to + tsouth;

                if (b.piece(from) == (self == BLACK ? B_PAWN : W_PAWN) && (target64 & mask(to).b(TK)))
                {
                    if (isBehind(self, RANK_2, krank))
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_PAWN : W_PAWN, b);
                }
            }
        }

        // 後はあき王手。両王手は生成してはいけない。
        Bitboard cand = b.discoveredCheckCandidates();

        while (cand)
        {
            const Square from = cand.firstOne();
            const Piece pc = b.piece(from);
            const PieceType pt = typeOf(pc);
            Bitboard line_bb = bbLine(ksq, from);

            // とりあえずこんなもの。
            Bitboard bb_to = target
                & ~line_bb // その駒と玉のライン上以外
                & attackAll(pc, from, b.bbOccupied());

            if (pt < GOLD && pt > PAWN)
            {
                if (canPromote(self, from))
                {
                    // 成りで動ける場所
                    uint64_t to64 = bb_to.b(T_HIGH) & ~st->check_sq[promotePieceType(pt)].b(T_HIGH);

                    while (to64)
                    {
                        const Square to = firstOne<T_HIGH>(to64);
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, pc, b);
                    }
                }
                else
                {
                    // 成りで動ける場所
                    uint64_t to64 = bb_to.b(T_HIGH) & ~st->check_sq[promotePieceType(pt)].b(T_HIGH) & enemyMask(self).b(T_HIGH);

                    while (to64)
                    {
                        const Square to = firstOne<T_HIGH>(to64);
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, pc, b);
                    }
                }
            }
            else if (pt <= ROOK)
            {
                if (canPromote(self, from))
                {
                    // 成りで動ける場所
                    Bitboard bb_to_promote = bb_to & ~st->check_sq[promotePieceType(pt)];

                    while (bb_to_promote)
                    {
                        const Square to = bb_to_promote.firstOne();
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, pc, b);
                    }
                }
                else
                {
                    // 成りで動ける場所
                    Bitboard bb_to_promote = bb_to & ~st->check_sq[promotePieceType(pt)] & enemyMask(self);

                    while (bb_to_promote)
                    {
                        const Square to = bb_to_promote.firstOne();
                        mlist++->move = makeMovePromote<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, pc, b);
                    }
                }
            }

            bb_to.andEqualNot(st->check_sq[pt]);

            // 成り以外
            while (bb_to)
            {
                const Square to = bb_to.firstOne();

                if (pt < GOLD)
                {
                    if (canPromote(self, from) || canPromote(self, to))
                    {
                        // 香の駒を取らない不成による空き王手は生成しない。
                        if (pt > LANCE && (pt != KNIGHT || isBehind(self, RANK_2, to)))
                            mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, pc, b);
                    }
                    else
                        mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, pc, b);
                }
                else
                    mlist++->move = makeMove<NO_CAPTURE_MINUS_PAWN_PROMOTE>(from, to, pc, b);
            }
        }

        if (h.exists(PAWN))
        {
            const Square to = ksq + tsouth;

            if (isOK(to)
                && b.piece(to) == EMPTY
                && !(b.bbPiece(PAWN, self) & fileMask(ksq)))  // ２歩じゃない.打ち歩のチェックはlegalで。
                mlist++->move = makeDrop(self == BLACK ? B_PAWN : W_PAWN, to);
        }

        if (h.exists(LANCE) && isBehind(enemy, RANK_1, ksq))
            for (Bitboard bb_to = target & st->check_sq[LANCE]; bb_to;)
                mlist++->move = makeDrop(self == BLACK ? B_LANCE : W_LANCE, bb_to.firstOne());

        if (h.exists(KNIGHT))
            for (uint64_t to64 = target64 & st->check_sq[KNIGHT].b(TK); to64;)
                mlist++->move = makeDrop(self == BLACK ? B_KNIGHT : W_KNIGHT, firstOne<TK>(to64));

        if (h.exists(SILVER))
            for (uint64_t to64 = target64 & st->check_sq[SILVER].b(TK); to64;)
                mlist++->move = makeDrop(self == BLACK ? B_SILVER : W_SILVER, firstOne<TK>(to64));

        if (h.exists(GOLD))
            for (uint64_t to64 = target64 & st->check_sq[GOLD].b(TK); to64;)
                mlist++->move = makeDrop(self == BLACK ? B_GOLD : W_GOLD, firstOne<TK>(to64));

        // 飛び駒はいろいろなところから王手できるのでBBを使わざるを得ない。
        if (h.exists(BISHOP))
            for (Bitboard bb_to = target & st->check_sq[BISHOP]; bb_to;)
                mlist++->move = makeDrop(self == BLACK ? B_BISHOP : W_BISHOP, bb_to.firstOne());

        if (h.exists(ROOK))
            for (Bitboard bb_to = target & st->check_sq[ROOK]; bb_to;)
                mlist++->move = makeDrop(self == BLACK ? B_ROOK : W_ROOK, bb_to.firstOne());

        return mlist;
    }

    // 玉が5段目より上にいるならHIGH盤面、5段目を含めて下にいるならLOW盤面を使用する。
    template <Turn T, MoveType MT>
    MoveStack* generateCheckRBB(MoveStack* mlist, const Board& b)
    {
        static_assert(MT == QUIET_CHECKS || MT == NEAR_CHECK, "");
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW = T == BLACK ? LOW : HIGH;
        const Square ksq = b.kingSquare(~T);
        const Rank k = rankOf(ksq);
        const bool is_behind = isBehind(BLACK, RANK_5, k);
        return is_behind ? generateNocapCheck<T, LOW>(mlist, b, ksq) : generateNocapCheck<T, HIGH>(mlist, b, ksq);
    };

#ifdef MATE3PLY
    // 近接王手生成。現在は使っていないが、いつか使う……かも？
    template <Turn T, Index TK>
    MoveStack* generateNearCheck(MoveStack* mlist, const Board& b, const Square ksq)
    {
        // 自玉に王手がかかっていたらここでは相手玉の1手詰めは調べない。そういうときにこの関数を読んではならない。
        // 厳密には先手玉に王手がかかっていても王手駒をとりかえせば1手詰めだったりするが、
        // それはここでは調べないことにする。
        assert(!b.inCheck());

        const Turn self = T;
        const Turn enemy = ~T;
        const Square tsouth = T == BLACK ? SQ_D : SQ_U;
        const Index T_HIGH = T == BLACK ? HIGH : LOW;
        const Index T_LOW = T == BLACK ? LOW : HIGH;

        Bitboard target = b.bbTurn(self).notThisAnd(kingAttack(ksq));
        uint64_t target64 = target.b(TK);
        const Hand h = b.hand(self);

        // 竜
        {
            Bitboard bb_from = b.bbPiece(DRAGON, self);

            while (bb_from)
            {
                const Square from = bb_from.firstOne();

                uint64_t to64 = target64 & dragonAttack(from, b.bbOccupied()).b(TK);

                while (to64)
                    mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_DRAGON : W_DRAGON, b);
            }
        }

        // 飛車
        {
            Bitboard bb_from = b.bbPiece(ROOK, self);
            uint64_t from123 = bb_from.b(T_HIGH) & ENEMY_MASK[T];

            // 敵陣からの移動
            while (from123)
            {
                const Square from = firstOne<T_HIGH>(from123);
                uint64_t to64 = target64 & rookAttack(from, b.bbOccupied()).b(TK);

                while (to64)
                    mlist++->move = makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_ROOK : W_ROOK, b);
            }

            uint64_t from4_9 = bb_from.b(T_LOW) & SELF_MASK[T];

            // 敵陣ではないところからの移動
            while (from4_9)
            {
                const Square from = firstOne<T_LOW>(from4_9);
                uint64_t to64 = target64 & rookAttack(from, b.bbOccupied()).b(TK) & (rookStepAttack(ksq).b(TK) | enemyMask(self).b(TK));

                if (to64)
                {
                    uint64_t to64_123 = to64 & enemyMask(self).b(TK);

                    if (to64_123)
                    {
                        to64 &= ~to64_123;

                        do {
                            mlist++->move = makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64_123), self == BLACK ? B_ROOK : W_ROOK, b);
                        } while (to64_123);
                    }

                    while (to64)
                        mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_ROOK : W_ROOK, b);
                }
            }
        }

        // 馬
        {
            Bitboard bb_from = b.bbPiece(HORSE, self);

            while (bb_from)
            {
                const Square from = bb_from.firstOne();

                uint64_t to64 = target64 & horseAttack(from, b.bbOccupied()).b(TK);

                while (to64)
                    mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_HORSE : W_HORSE, b);
            }
        }

        // 角
        {
            Bitboard bb_from = b.bbPiece(BISHOP, self);
            uint64_t from123 = bb_from.b(T_HIGH) & ENEMY_MASK[self];

            // 敵陣からの移動
            while (from123)
            {
                const Square from = firstOne<T_HIGH>(from123);
                uint64_t to64 = target64 & bishopAttack(from, b.bbOccupied()).b(TK);

                while (to64)
                    mlist++->move = makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_BISHOP : W_BISHOP, b);
            }

            uint64_t from4_9 = bb_from.b(T_LOW) & SELF_MASK[T];

            // 敵陣ではないところからの移動
            while (from4_9)
            {
                const Square from = firstOne<T_LOW>(from4_9);
                uint64_t to64 = target64 & bishopAttack(from, b.bbOccupied()).b(TK) & (bishopStepAttack(ksq).b(TK) | enemyMask(self).b(TK));

                while (to64)
                {
                    uint64_t to64_123 = to64 & enemyMask(self).b(TK);
                    to64 &= ~to64_123;

                    while (to64_123)
                        mlist++->move = makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64_123), self == BLACK ? B_BISHOP : W_BISHOP, b);

                    while (to64)
                        mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_BISHOP : W_BISHOP, b);
                }
            }
        }

        // 駒移動 (両王手も含む）

        // 金、成金
        {
            uint64_t from64 = b.bbGold().b(TK) & b.bbTurn(self).b(TK) & goldCheck(self, ksq).b(TK);

            if (from64)
            {
                uint64_t gold_target = target64 & goldAttack(enemy, ksq).b(TK);

                do {
                    const Square from = firstOne<TK>(from64);
                    uint64_t to64 = gold_target & goldAttack(self, from).b(TK);

                    while (to64)
                        mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), b.piece(from), b);
                } while (from64);
            }
        }

        // 銀
        {
            uint64_t from64 = b.bbPiece(SILVER, self).b(TK) & silverCheck(self, ksq).b(TK);

            if (from64)
            {
                // 成らなくても王手になる場所
                uint64_t chk = silverAttack(enemy, ksq).b(TK);

                // 成って王手になる場所
                uint64_t pro_chk = goldAttack(enemy, ksq).b(TK) & enemyMaskPlus1(self).b(TK);

                do
                {
                    const Square from = firstOne<TK>(from64);
                    uint64_t to64 = target64 & silverAttack(self, from).b(TK);
                    uint64_t to_pro = to64 & pro_chk;
                    to64 &= chk;

                    while (to_pro)
                    {
                        const Square to = firstOne<TK>(to_pro);

                        if (canPromote(self, from) || canPromote(self, to))
                            mlist++->move = makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_SILVER : W_SILVER, b);
                    }

                    while (to64)
                        mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_SILVER : W_SILVER, b);

                } while (from64);
            }
        }

        // 桂馬
        {
            // 桂馬はLOW盤面にしか存在できない
            uint64_t from64 = b.bbPiece(KNIGHT, self).b(T_LOW) & knightCheck(self, ksq).b(T_LOW);

            if (from64)
            {
                uint64_t chk_pro = target64 & goldAttack(enemy, ksq).b(TK) & enemyMask(self).b(TK);

                do {
                    const Square from = firstOne<T_LOW>(from64);
                    uint64_t to64 = ~b.bbTurn(self).b(TK) & knightAttack(self, from).b(TK);
                    uint64_t to_pro = to64 & chk_pro;
                    to64 &= knightAttack(enemy, ksq).b(TK);

                    while (to_pro)
                        mlist++->move = makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to_pro), self == BLACK ? B_KNIGHT : W_KNIGHT, b);

                    while (to64)
                        mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to64), self == BLACK ? B_KNIGHT : W_KNIGHT, b);

                } while (from64);
            }
        }

        // 香車
        {
            Bitboard bb_from = b.bbPiece(LANCE, self) & lanceCheck(self, ksq);
            const Rank TRank2 = self == BLACK ? RANK_2 : RANK_8;

            if (bb_from)
            {
                // 香車を動かす手で駒を取らない王手は、成りしかない。
                const uint64_t chk_pro = goldAttack(enemy, ksq).b(TK) & enemyMask(self).b(TK);

                do {
                    const Square from = bb_from.firstOne();
                    uint64_t to64 = target64 & lanceAttack(self, from, b.bbOccupied()).b(TK);
                    uint64_t to_pro = to64 & chk_pro;
                    to64 &= pawnAttack(enemy, ksq).b(TK);

                    if (to64)
                    {
                        const Square to = firstOne<TK>(to64);
                        assert(b.piece(to) != EMPTY);

                        if (isBehind(self, RANK_2, to))
                            mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_LANCE : W_LANCE, b);
                    }

                    while (to_pro)
                        mlist++->move = makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, firstOne<TK>(to_pro), self == BLACK ? B_LANCE : W_LANCE, b);

                } while (bb_from);
            }
        }

        // 歩
        {
            const Rank krank = rankOf(ksq);

            if (isBehind(enemy, RANK_2, krank))
            {
                uint64_t from64 = b.bbPiece(PAWN, self).b(TK);
                const Square tnorth = self == BLACK ? SQ_U : SQ_D;

                if (canPromote(self, krank))
                {
                    const uint64_t to_pro = target64 & goldAttack(enemy, ksq).b(TK) & enemyMask(self).b(TK);
                    uint64_t from_pro = from64 & (self == BLACK ? to_pro << 9 : to_pro >> 9);

                    while (from_pro)
                    {
                        const Square from = firstOne<TK>(from_pro);
                        const Square to = from + tnorth;
                        mlist++->move = makeMovePromote<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_PAWN : W_PAWN, b);
                    }
                }
                // 不成

                const Square to = ksq + tsouth;
                const Square from = to + tsouth;

                if (from64 & mask(from).b(TK) && !(b.bbTurn(self).b(TK) & mask(to).b(TK)))
                {
                    if (isBehind(self, RANK_2, krank))
                    {
                        mlist++->move = makeMove<CAPTURE_PLUS_PAWN_PROMOTE>(from, to, self == BLACK ? B_PAWN : W_PAWN, b);
                    }
                }
            }
        }

        target64 = b.bbEmpty().b(TK);

        if (h.exists(PAWN))
        {
            const Square to = ksq + tsouth;

            if (isOK(to)
                && b.piece(to) == EMPTY
                && !(b.bbPiece(PAWN, self) & fileMask(ksq)))  // ２歩じゃない.打ち歩のチェックはlegalで。
                mlist++->move = makeDrop(self == BLACK ? B_PAWN : W_PAWN, to);
        }

        if (h.exists(LANCE) && isBehind(enemy, RANK_1, ksq))
        {
            if (b.piece(ksq + tsouth) == EMPTY)
            mlist++->move = makeDrop(self == BLACK ? B_LANCE : W_LANCE, ksq + tsouth);
        }


        if (h.exists(KNIGHT))
        {
            uint64_t to64 = target64 & knightAttack(enemy, ksq).b(TK);

            while (to64)
                mlist++->move = makeDrop(self == BLACK ? B_KNIGHT : W_KNIGHT, firstOne<TK>(to64));
        }

        if (h.exists(SILVER))
        {
            uint64_t to64 = target64 & silverAttack(enemy, ksq).b(TK);

            while (to64)
                mlist++->move = makeDrop(self == BLACK ? B_SILVER : W_SILVER, firstOne<TK>(to64));
        }

        // ここからは近接王手なので最適化していく。
        // 金銀は先に打つ。
        if (h.exists(GOLD))
        {
            uint64_t to64 = target64 & goldAttack(enemy, ksq).b(TK);

            while (to64)
                mlist++->move = makeDrop(self == BLACK ? B_GOLD : W_GOLD, firstOne<TK>(to64));
        }

        if (h.exists(BISHOP))
        {
            uint64_t to64 = target64 & bishopStepAttack(ksq).b(TK);

            while (to64)
                mlist++->move = makeDrop(self == BLACK ? B_BISHOP : W_BISHOP, firstOne<TK>(to64));
        }

        // 飛び駒はいろいろなところから王手できるのでBBを使わざるを得ない。
        if (h.exists(ROOK))
        {
            uint64_t to64 = target64 & rookStepAttack(ksq).b(TK);

            while (to64)
                mlist++->move = makeDrop(self == BLACK ? B_ROOK : W_ROOK, firstOne<TK>(to64));
        }

        return mlist;
    }
#endif
}// namespace

template <MoveType MT, Turn T, bool ALL> MoveStack* generate(MoveStack* mlist, const Board& b)
{
    if (MT == CAPTURE_PLUS_PAWN_PROMOTE || MT == NO_CAPTURE_MINUS_PAWN_PROMOTE)
    {
        const Rank t_rank4 = (T == BLACK ? RANK_4 : RANK_6);
        const Rank t_rank3 = (T == BLACK ? RANK_3 : RANK_7);
        const Rank t_rank2 = (T == BLACK ? RANK_2 : RANK_8);
        const Bitboard t_bb_rank_123 = frontMask(T, t_rank4);
        const Bitboard t_bb_rank_4_9 = frontMask(~T, t_rank3);

        const Turn enemy_turn = ~T;

        const Bitboard target_pawn =
            MT == CAPTURE_PLUS_PAWN_PROMOTE ? (b.bbTurn(enemy_turn) | (b.bbEmpty() & t_bb_rank_123)) :
            MT == NO_CAPTURE_MINUS_PAWN_PROMOTE ? b.bbEmpty() & t_bb_rank_4_9 :
            allZeroMask();

        const Bitboard target_other =
            MT == CAPTURE_PLUS_PAWN_PROMOTE ? b.bbTurn(enemy_turn) :
            MT == NO_CAPTURE_MINUS_PAWN_PROMOTE ? b.bbEmpty() :
            allZeroMask();

        mlist = generatePawnMove <MT, T, ALL>(mlist, b, target_pawn);
        mlist = generateOtherMove<MT, T, ALL>(mlist, b, target_other);
        mlist = generateKingMove <MT, T, ALL>(mlist, b, target_other);
    }

    else if (MT == DROP)
        mlist = generateDrop<T>(mlist, b, b.bbEmpty());

    else if (MT == QUIETS)
    {
        mlist = generate<NO_CAPTURE_MINUS_PAWN_PROMOTE, T, false>(mlist, b);
        mlist = generate<DROP, T, false>(mlist, b);
    }

    else if (MT == EVASIONS)
        mlist = generateEvasion<T, ALL>(mlist, b);

    else if (MT == NO_EVASIONS)
        mlist = generateNoEvasion<T, ALL>(mlist, b);

    else if (MT == LEGAL)
        mlist = generateLegal<T, false>(mlist, b);

    else if (MT == LEGAL_ALL)
        mlist = generateLegal<T, true>(mlist, b);

    else if (MT == QUIET_CHECKS)
        mlist = generateCheckRBB<T, QUIET_CHECKS>(mlist, b);

    return mlist;
}

template <MoveType MT>
MoveStack* generate(MoveStack* mlist, const Board& b, const Square to)
{
    return (b.turn() == BLACK ?
        generateRecapture<BLACK>(mlist, b, to) : generateRecapture<WHITE>(mlist, b, to));
}

template <MoveType MT>
MoveStack* generate(MoveStack* mlist, const Board& b)
{
    return (b.turn() == BLACK ?
        generate<MT, BLACK, false>(mlist, b) : generate<MT, WHITE, false>(mlist, b));
}

// 明示的なインスタンス化
template MoveStack* generate<DROP                         >(MoveStack* mlist, const Board& b);
template MoveStack* generate<CAPTURE_PLUS_PAWN_PROMOTE    >(MoveStack* mlist, const Board& b);
template MoveStack* generate<QUIETS                       >(MoveStack* mlist, const Board& b);
template MoveStack* generate<NO_CAPTURE_MINUS_PAWN_PROMOTE>(MoveStack* mlist, const Board& b);
template MoveStack* generate<EVASIONS                     >(MoveStack* mlist, const Board& b);
template MoveStack* generate<NO_EVASIONS                  >(MoveStack* mlist, const Board& b);
template MoveStack* generate<LEGAL                        >(MoveStack* mlist, const Board& b);
template MoveStack* generate<LEGAL_ALL                    >(MoveStack* mlist, const Board& b);
template MoveStack* generate<QUIET_CHECKS                 >(MoveStack* mlist, const Board& b);
template MoveStack* generate<RECAPTURES  >(MoveStack* mlist, const Board& b, const Square to);
#endif
