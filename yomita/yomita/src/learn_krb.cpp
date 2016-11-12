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

#include "learn.h"

#if defined LEARN && defined EVAL_KRB

// KPPを更新する
//#define UPDATE_KPP

#include <codecvt>
#include <fstream>
#include <unordered_set>
#include "eval_krb.h"
#include "usi.h"

#define kpp (*et.kpp_)
#define rpp (*et.rpp_)
#define bpp (*et.bpp_)

namespace Eval
{
    // 絶対値を抑制するマクロ
#define SET_A_LIMIT_TO(X,MIN,MAX)  \
    X = std::min(X,(MAX));   \
    X = std::max(X,(MIN));   \

    BonaPiece INV_PIECE[fe_end], MIR_PIECE[fe_end];

    // kpp配列の同じ重みが入るべき場所に同じ値を書き込む。
    void writeKpp(Square k1, BonaPiece p1, BonaPiece p2, Value value)
    {
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirror(k1);

        //std::cout << k1 << p1 << p2 << std::endl;
        //std::cout << mk1 << mp1 << mp2 << std::endl;

        assert(kpp[k1][p1][p2] == kpp[k1][p2][p1]);
        assert(kpp[k1][p1][p2] == kpp[mk1][mp1][mp2] || fileOf(k1) == FILE_5);
        assert(kpp[k1][p1][p2] == kpp[mk1][mp2][mp1] || fileOf(k1) == FILE_5);

        kpp[k1][p1][p2] = kpp[k1][p2][p1] = kpp[mk1][mp1][mp2] = kpp[mk1][mp2][mp1] = value;
    }

    // 一番若いアドレスを返す。
    uint64_t getKppIndex(Square k1, BonaPiece p1, BonaPiece p2)
    {
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirror(k1);

        const auto q0 = &kpp[0][0][0];

        auto q1 = &kpp[k1][p1][p2] - q0;
        auto q2 = &kpp[k1][p2][p1] - q0;
        auto q3 = &kpp[mk1][mp1][mp2] - q0;
        auto q4 = &kpp[mk1][mp2][mp1] - q0;

        return std::min({ q1, q2, q3, q4 });
    }

    void writeRpp(Square k1, BonaPiece p1, BonaPiece p2, Value value)
    {
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirrorProHand(k1);
#if 0
        std::cout << k1 << p1 << p2 << std::endl;
        std::cout << mk1 << mp1 << mp2 << std::endl;
#endif
        assert(rpp[k1][p1][p2] == rpp[k1][p2][p1]);
        assert(rpp[k1][p1][p2] == rpp[mk1][mp1][mp2] || fileOf(k1) == FILE_5);
        assert(rpp[k1][p1][p2] == rpp[mk1][mp2][mp1] || fileOf(k1) == FILE_5);

        rpp[k1][p1][p2] = rpp[k1][p2][p1] = rpp[mk1][mp1][mp2] = rpp[mk1][mp2][mp1] = value;
    }

    // 一番若いアドレスを返す。
    uint64_t getRppIndex(Square k1, BonaPiece p1, BonaPiece p2)
    {
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirrorProHand(k1);
#if 0
        std::cout << k1 << p1 << p2 << std::endl;
        std::cout << mk1 << mp1 << mp2 << std::endl;
#endif
        const auto q0 = &rpp[0][0][0];

        auto q1 = &rpp[k1][p1][p2] - q0;
        auto q2 = &rpp[k1][p2][p1] - q0;
        auto q3 = &rpp[mk1][mp1][mp2] - q0;
        auto q4 = &rpp[mk1][mp2][mp1] - q0;

        return std::min({ q1, q2, q3, q4 });
    }

    void writeBpp(Square k1, BonaPiece p1, BonaPiece p2, Value value)
    {
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirrorProHand(k1);
#if 0
        std::cout << k1 << p1 << p2 << std::endl;
        std::cout << mk1 << mp1 << mp2 << std::endl;
#endif
        assert(bpp[k1][p1][p2] == bpp[k1][p2][p1]);
        assert(bpp[k1][p1][p2] == bpp[mk1][mp1][mp2] || fileOf(k1) == FILE_5);
        assert(bpp[k1][p1][p2] == bpp[mk1][mp2][mp1] || fileOf(k1) == FILE_5);

        bpp[k1][p1][p2] = bpp[k1][p2][p1] = bpp[mk1][mp1][mp2] = bpp[mk1][mp2][mp1] = value;
    }

    // 一番若いアドレスを返す。
    uint64_t getBppIndex(Square k1, BonaPiece p1, BonaPiece p2)
    {
        BonaPiece mp1 = MIR_PIECE[p1];
        BonaPiece mp2 = MIR_PIECE[p2];
        Square mk1 = mirrorProHand(k1);
#if 0
        std::cout << k1 << p1 << p2 << std::endl;
        std::cout << mk1 << mp1 << mp2 << std::endl;
#endif
        const auto q0 = &bpp[0][0][0];

        auto q1 = &bpp[k1][p1][p2] - q0;
        auto q2 = &bpp[k1][p2][p1] - q0;
        auto q3 = &bpp[mk1][mp1][mp2] - q0;
        auto q4 = &bpp[mk1][mp2][mp1] - q0;

        return std::min({ q1, q2, q3, q4 });
    }

    // 勾配をdelta分増やす。
    void Weight::addGrad(WeightValue delta)
    {
        g += delta;
#ifdef USE_SGD_UPDATE
        count++;
        assert(abs(g / count) <= 1);
#endif
    }

    // 勾配を重みに反映させる。
    bool Weight::update(bool skip_update)
    {

#ifdef USE_SGD_UPDATE
        if (g == 0)
            return false;

        if (count)
        {
            g /= count;
            count = 0;
            w = w - eta * g;
        }

#elif defined USE_ADA_GRAD_UPDATE
        if (g == 0)
            return false;

        g2 += WeightValue(g * g);

        // 値が小さいうちはskipする
        if (g2 >= 0.1f && !skip_update)
            w = w - eta * g / sqrt(g2);

#elif defined USE_YANE_GRAD_UPDATE
        if (g == 0)
            return false;

        g2 = alpha * g2 + g * g;

        // skip_updateのときはg2の更新に留める。
        if (!skip_update)
        {
            //std::cout << "w = " << w;
            w = w - eta * g / sqrt(g2 + epsilon);
            //std::cout << "→" << w << std::endl;
        }

#elif defined USE_ADAM_UPDATE
        v = beta * v + (1 - beta) * g;
        r = gamma * r + (1 - gamma) * g * g;

        if (!skip_update)
            w = w - eta / (sqrt(r / rt) + epsilon) * v / bt;	
#endif
        g = 0;

        return !skip_update;
    }

#ifdef USE_ADAM_UPDATE
    double Weight::bt;
    double Weight::rt;
    LearnFloatType Weight::eta;
#else
    LearnFloatType Weight::eta;
#endif

#ifdef UPDATE_KPP
    Weight(*kpp_w_)[SQ_MAX][fe_end][fe_end];
#endif
    Weight(*rpp_w_)[SQ_MAX_PRO_HAND][fe_gold_end][fe_gold_end];
    Weight(*bpp_w_)[SQ_MAX_PRO_HAND][fe_gold_end][fe_gold_end];

#define kpp_w (*kpp_w_)
#define rpp_w (*rpp_w_)
#define bpp_w (*bpp_w_)

    void initGrad()
    {
        if (rpp_w_ == nullptr)
        {
#ifdef UPDATE_KPP
            const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
            kpp_w_ = (Weight(*)[SQ_MAX][fe_end][fe_end])new Weight[sizekpp];
            memset(kpp_w_, 0, sizeof(Weight) * sizekpp);
#endif
            const auto sizerpp = uint64_t(SQ_MAX_PRO_HAND) * uint64_t(fe_gold_end) * uint64_t(fe_gold_end);
            const auto sizebpp = uint64_t(SQ_MAX_PRO_HAND) * uint64_t(fe_gold_end) * uint64_t(fe_gold_end);
            rpp_w_ = (Weight(*)[SQ_MAX_PRO_HAND][fe_gold_end][fe_gold_end])new Weight[sizerpp];
            bpp_w_ = (Weight(*)[SQ_MAX_PRO_HAND][fe_gold_end][fe_gold_end])new Weight[sizebpp];
            memset(rpp_w_, 0, sizeof(Weight) * sizerpp);
            memset(bpp_w_, 0, sizeof(Weight) * sizebpp);

#ifdef RESET_TO_ZERO_VECTOR
            std::cout << "\n[RESET_TO_ZERO_VECTOR]";
            memset(&kpp, 0, sizeof(Value) * sizekpp);
            memset(&rpp, 0, sizeof(Value) * sizerpp);
            memset(&bpp, 0, sizeof(Value) * sizebpp);
#elif defined RESET_TO_ZERO_EXCEPT_KPP
            std::cout << "\n[RESET_TO_ZERO_RPP_AND_BPP]";
            for (auto k = SQ_ZERO; k < SQ_MAX_PRO_HAND; ++k)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_gold_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_gold_end; ++p2)
                    {
                        rpp[k][p1][p2] = 0;
                        bpp[k][p1][p2] = 0;
                    }
            //memset(&rpp, 0, sizeof(Value) * sizerpp);
            //memset(&bpp, 0, sizeof(Value) * sizebpp);
#else

#ifdef UPDATE_KPP
            for (auto k : Squares)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                        kpp_w[k][p1][p2].w = LearnFloatType(kpp[k][p1][p2]);
#endif

            for (auto k = SQ_ZERO; k < SQ_MAX_PRO_HAND; ++k)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_gold_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_gold_end; ++p2)
                    {	
                        rpp_w[k][p1][p2].w = LearnFloatType(rpp[k][p1][p2]);
                        bpp_w[k][p1][p2].w = LearnFloatType(bpp[k][p1][p2]);
                    }
#endif
        }
    }

    // 現在の局面で出現している特徴すべてに対して、勾配値を勾配配列に加算する。
    void addGrad(Board& b, Turn root_turn, double delta_grad)
    {
    
        auto sq_bk = b.kingSquare(BLACK);
        auto sq_wk = b.kingSquare(WHITE);
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        // 飛車角の位置
        auto sq_r0 = b.rookSquare(0);
        auto sq_r1 = b.rookSquare(1);
        auto sq_b0 = b.bishopSquare(0);
        auto sq_b1 = b.bishopSquare(1);

        const bool r0_is_white = sq_r0 >= SQ_MAX_PRO_HAND,
            r1_is_white = sq_r1 >= SQ_MAX_PRO_HAND,
            b0_is_white = sq_b0 >= SQ_MAX_PRO_HAND,
            b1_is_white = sq_b1 >= SQ_MAX_PRO_HAND;

        BonaPiece *r0list, *r1list, *b0list, *b1list;

        // squareは既にinverseされているのでinverseは必要ない。
        if (r0_is_white)
        {
            sq_r0 -= SQ_MAX_PRO_HAND;
            r0list = list_fw;
        }
        else
            r0list = list_fb;

        if (r1_is_white)
        {
            sq_r1 -= SQ_MAX_PRO_HAND;
            r1list = list_fw;
        }
        else
            r1list = list_fb;

        if (b0_is_white)
        {
            sq_b0 -= SQ_MAX_PRO_HAND;
            b0list = list_fw;
        }
        else
            b0list = list_fb;

        if (b1_is_white)
        {
            sq_b1 -= SQ_MAX_PRO_HAND;
            b1list = list_fw;
        }
        else
            b1list = list_fb;

#if 0
        bool r0_is_pro = sq_r0 >= SQ_MAX && sq_r0 < SQ_MAX * 2,
            r1_is_pro = sq_r1 >= SQ_MAX && sq_r1 < SQ_MAX * 2,
            b0_is_pro = sq_b0 >= SQ_MAX && sq_b0 < SQ_MAX * 2,
            b1_is_pro = sq_b1 >= SQ_MAX && sq_b1 < SQ_MAX * 2;

        auto r0 = r0_is_pro ? sq_r0 - SQ_MAX : sq_r0,
            r1 = r1_is_pro ? sq_r1 - SQ_MAX : sq_r1,
            b0 = b0_is_pro ? sq_b0 - SQ_MAX : sq_b0,
            b1 = b1_is_pro ? sq_b1 - SQ_MAX : sq_b1;

        r0 = r0_is_white && r0 != SQ_MAX * 2 ? inverse(r0) : r0,
            r1 = r1_is_white && r1 != SQ_MAX * 2 ? inverse(r1) : r1,
            b0 = b0_is_white && b0 != SQ_MAX * 2 ? inverse(b0) : b0,
            b1 = b1_is_white && b1 != SQ_MAX * 2 ? inverse(b1) : b1;

        std::cout << b
            << "\nsq_r0 = " << (r0_is_white ? "w" : "b") << (r0_is_pro ? "*" : "")
            << (r0 == SQ_MAX * 2 ? "hand" : pretty(r0))
            << "\nsq_r1 = " << (r1_is_white ? "w" : "b") << (r1_is_pro ? "*" : "")
            << (r1 == SQ_MAX * 2 ? "hand" : pretty(r1))
            << "\nsq_b0 = " << (b0_is_white ? "w" : "b") << (b0_is_pro ? "*" : "")
            << (b0 == SQ_MAX * 2 ? "hand" : pretty(b0))
            << "\nsq_b1 = " << (b1_is_white ? "w" : "b") << (b1_is_pro ? "*" : "")
            << (b1 == SQ_MAX * 2 ? "hand" : pretty(b1))
            << std::endl;
#endif
        // 手番を考慮しない値
        auto f = (root_turn == BLACK) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);

        for (int i = 0; i < PIECE_NO_BISHOP; i++)
            for (int j = 0; j < i; j++)
            {
                //std::cout << list_fb[i] << list_fb[j] << std::endl;
                ((Weight*)rpp_w_)[getRppIndex(sq_r0, r0list[i], r0list[j])].addGrad(r0_is_white ? -f : f);
                ((Weight*)rpp_w_)[getRppIndex(sq_r1, r1list[i], r1list[j])].addGrad(r1_is_white ? -f : f);
                ((Weight*)bpp_w_)[getBppIndex(sq_b0, b0list[i], b0list[j])].addGrad(b0_is_white ? -f : f);
                ((Weight*)bpp_w_)[getBppIndex(sq_b1, b1list[i], b1list[j])].addGrad(b1_is_white ? -f : f);
            }

#ifdef UPDATE_KPP
        for (int i = 0; i < PIECE_NO_KING; ++i)
            for (int j = 0; j < i; ++j)
            {
                ((Weight*)kpp_w_)[getKppIndex(sq_bk, list_fb[i], list_fb[j])].addGrad(f);
                ((Weight*)kpp_w_)[getKppIndex(inverse(sq_wk), list_fw[i], list_fw[j])].addGrad(-f);
            }
#endif
    }


    void updateWeights(uint64_t mini_batch_size, uint64_t epoch)
    {
        // 3回目まではwのupdateを保留する。
        // ただし、SGDは履歴がないのでこれを行なう必要がない。
        bool skip_update =
#ifdef USE_SGD_UPDATE
            false;
#else
            epoch <= 3;
#endif

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        WeightValue max_kpp = 0.0f, max_rpp = 0.0f;
#endif

        // 学習メソッドに応じた学習率設定
#ifdef USE_SGD_UPDATE
#if defined (LOSS_FUNCTION_IS_CROSS_ENTOROPY)
        Weight::eta = 3.2f;
#elif defined (LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
        Weight::eta = 32.0f;
#endif
#elif defined USE_ADA_GRAD_UPDATE
        Weight::eta = 5.0f / float(1000000.0 / mini_batch_size);
#elif defined USE_YANE_GRAD_UPDATE
#if defined (LOSS_FUNCTION_IS_CROSS_ENTOROPY)
        Weight::eta = 5.0f / float(1000000.0 / mini_batch_size);
#elif defined (LOSS_FUNCTION_IS_WINNING_PERCENTAGE)
        Weight::eta = 20.0f / float( 1000000.0/mini_batch_size );
#endif
#elif defined USE_ADAM_UPDATE
        Weight::eta = LearnFloatType(32.0 / 64.0);
        Weight::bt = 1.0 - pow((double)Weight::beta, (double)epoch);
        Weight::rt = 1.0 - pow((double)Weight::gamma, (double)epoch);
#endif
        std::cout << "eta = " << Weight::eta << std::endl;

#ifdef UPDATE_KPP
        for (auto k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                {
                    auto& w = kpp_w[k][p1][p2];

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                    if (max_kpp < w.w)
                        max_kpp = w.w;
#endif
                    if (w.update(skip_update))
                    {
                        //std::cout << kpp[k][p1][p2];
                        // 絶対値を抑制する。
                        SET_A_LIMIT_TO(w.w, (LearnFloatType)(INT16_MIN / 2), (LearnFloatType)(INT16_MAX / 2));
                        writeKpp(k, p1, p2, Value((int16_t)w.w));
                        //std::cout << "→" << kpp[k][p1][p2] << std::endl;
                    }
                }

        Weight::eta /= 4.0;
#endif

        for (auto k = SQ_ZERO; k < SQ_MAX_PRO_HAND; ++k)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_gold_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_gold_end; ++p2)
                {
                    auto& r = rpp_w[k][p1][p2];
                    auto& b = bpp_w[k][p1][p2];

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                    if (max_rpp < r.w)
                        max_rpp = r.w;
#endif
                    if (r.update(skip_update))
                    {
                        SET_A_LIMIT_TO(r.w, (LearnFloatType)(INT16_MIN / 2), (LearnFloatType)(INT16_MAX / 2));
                        writeRpp(k, p1, p2, Value((int16_t)r.w));
                    }
                    if (b.update(skip_update))
                    {
                        SET_A_LIMIT_TO(b.w, (LearnFloatType)(INT16_MIN / 2), (LearnFloatType)(INT16_MAX / 2));
                        writeBpp(k, p1, p2, Value((int16_t)b.w));
                    }
                }
        
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        SYNC_COUT << "max_kpp = " << max_kpp << ", max_rpp = " << max_rpp << SYNC_ENDL;
#endif
    }

    // 学習のためのテーブルの初期化
    void evalLearnInit()
    {
        // fとeとの交換
        int t[] =
        {
            f_hand_pawn    , e_hand_pawn   ,
            f_hand_lance    , e_hand_lance  ,
            f_hand_knight   , e_hand_knight  ,
            f_hand_silver   , e_hand_silver ,
            f_hand_gold   , e_hand_gold   ,
            f_hand_bishop   , e_hand_bishop  ,
            f_hand_rook    , e_hand_rook    ,
            f_pawn             , e_pawn            ,
            f_lance            , e_lance           ,
            f_knight           , e_knight          ,
            f_silver           , e_silver          ,
            f_gold             , e_gold            ,
            f_bishop           , e_bishop          ,
            f_horse            , e_horse           ,
            f_rook             , e_rook            ,
            f_dragon           , e_dragon          ,
        };

        INV_PIECE[0] = BonaPiece(0);
        MIR_PIECE[0] = BonaPiece(0);

        // 未初期化の値を突っ込んでおく。
        for (BonaPiece p = BONA_PIECE_ZERO + 1; p < fe_end; ++p)
        {
            INV_PIECE[p] = (BonaPiece)-1;

            // mirrorは手駒に対しては機能しない。元の値を返すだけ。
            MIR_PIECE[p] = (p < f_pawn) ? p : (BonaPiece)-1;
        }

        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
        {
            for (int i = 0; i < 32 /* t.size() */; i += 2)
            {
                if (t[i] <= p && p < t[i + 1])
                {
                    Square sq = (Square)(p - t[i]);

                    // 持ち駒ならそのままe_にして、盤面上の駒ならinverseする。
                    BonaPiece q = (p < fe_hand_end) ? BonaPiece(sq + t[i + 1]) : (BonaPiece)(inverse(sq) + t[i + 1]);
                    INV_PIECE[p] = q;
                    INV_PIECE[q] = p;

                    // 手駒に関してはmirrorなど存在しない。
                    if (p < fe_hand_end)
                        break;

                    BonaPiece r1 = (BonaPiece)(mirror(sq) + t[i]);
                    MIR_PIECE[p] = r1;
                    MIR_PIECE[r1] = p;

                    BonaPiece p2 = (BonaPiece)(sq + t[i + 1]);
                    BonaPiece r2 = (BonaPiece)(mirror(sq) + t[i + 1]);
                    MIR_PIECE[p2] = r2;
                    MIR_PIECE[r2] = p2;

                    break;
                }
            }
        }

        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; ++p)
            if (INV_PIECE[p] == (BonaPiece)-1 || MIR_PIECE[p] == (BonaPiece)-1)
            {
                // 未初期化のままになっている。上のテーブルの初期化コードがおかしい。
                assert(false);
            }

#if 0
        // 評価関数のミラーをしても大丈夫であるかの事前検証
        // 値を書き込んだときにassertionがあるので、ミラーしてダメである場合、
        // そのassertに引っかかるはず。

        // 調べるべきではないBonaPiece。
        std::unordered_set<BonaPiece> s;

        s.insert(BONA_PIECE_ZERO);

        // さらに出現しない升の盤上の歩、香、桂も除外(Aperyはここにもゴミが入っている)
        for (Rank r = RANK_1; r <= RANK_2; ++r)
            for (File f = FILE_9; f <= FILE_1; ++f)
            {
                if (r == RANK_1)
                {
                    // 1段目の歩
                    BonaPiece b1 = BonaPiece(f_pawn + sqOf(f, r));
                    s.insert(b1);
                    s.insert(INV_PIECE[b1]);

                    // 1段目の香
                    BonaPiece b2 = BonaPiece(f_lance + sqOf(f, r));
                    s.insert(b2);
                    s.insert(INV_PIECE[b2]);
                }

                // 1,2段目の桂
                BonaPiece b = BonaPiece(f_knight + sqOf(f, r));
                s.insert(b);
                s.insert(INV_PIECE[b]);
            }

#if 1
        std::cout << "\nchecking writeKpp()..";

        for (auto sq : Squares)
        {
            std::cout << sq << ' ';

            for (BonaPiece p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (BonaPiece p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    if (!s.count(p1) && !s.count(p2))
                        writeKpp(sq, p1, p2, kpp[sq][p1][p2]);
        }

        std::cout << "\nchecking writeRpp()..";

        for (auto sq = SQ_ZERO; sq < SQ_MAX_PRO_HAND; ++sq)
        {
            std::cout << (int)sq << ' ';

            for (BonaPiece p1 = BONA_PIECE_ZERO; p1 < fe_gold_end; ++p1)
                for (BonaPiece p2 = BONA_PIECE_ZERO; p2 < fe_gold_end; ++p2)
                    if (!s.count(p1) && !s.count(p2))
                        writeRpp(sq, p1, p2, rpp[sq][p1][p2]);
        }

#endif
        std::cout << "\nchecking writeBpp()..";

        for (auto sq = SQ_MAX_PRO_HAND - 1; sq < SQ_MAX_PRO_HAND; ++sq)
        {
            std::cout << (int)sq << ' ';

            for (BonaPiece p1 = BONA_PIECE_ZERO; p1 < fe_gold_end; ++p1)
                for (BonaPiece p2 = BONA_PIECE_ZERO; p2 < fe_gold_end; ++p2)
                    if (!s.count(p1) && !s.count(p2))
                        writeBpp(sq, p1, p2, bpp[sq][p1][p2]);
        }
        std::cout << "..done!" << std::endl;
#endif
    }


    void saveEval(std::string dir_name)
    {
        {
            auto eval_dir = path((std::string)USI::Options["EvalSaveDir"], dir_name);

            mkdir(eval_dir);

            // KPP
            std::ofstream ofsKK(path(eval_dir, KPP_BIN), std::ios::binary);
            if (!ofsKK.write(reinterpret_cast<char*>(kpp), sizeof(kpp)))
                goto Error;

            // RPP
            std::ofstream ofsKKP(path(eval_dir, RPP_BIN), std::ios::binary);
            if (!ofsKKP.write(reinterpret_cast<char*>(rpp), sizeof(rpp)))
                goto Error;

            // BPP
            std::ofstream ofsKPP(path(eval_dir, BPP_BIN), std::ios::binary);
            if (!ofsKPP.write(reinterpret_cast<char*>(bpp), sizeof(bpp)))
                goto Error;

            std::cout << "save_eval() finished. folder = " << eval_dir << std::endl;

            return;
        }

    Error:;
        std::cout << "Error : save_eval() failed" << std::endl;
    }

} // namespace Eval

#endif
