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

#include "evaluate.h"

#ifdef EVAL_KPPT

#include <fstream>

#include "usi.h"
#include "board.h"
#include "learn.h"

#define  KK_BIN  "KK_synthesized.bin"
#define KKP_BIN "KKP_synthesized.bin"
#define KPP_BIN "KPP_synthesized.bin"

#ifdef HAVE_BMI2
#define USE_GATHER
#endif

#ifdef USE_GATHER
static const __m256i MASK[9] = 
{
    _mm256_setzero_si256(),
    _mm256_set_epi32( 0, 0, 0, 0, 0, 0, 0,-1),
    _mm256_set_epi32( 0, 0, 0, 0, 0, 0,-1,-1),
    _mm256_set_epi32( 0, 0, 0, 0, 0,-1,-1,-1),
    _mm256_set_epi32( 0, 0, 0, 0,-1,-1,-1,-1),
    _mm256_set_epi32( 0, 0, 0,-1,-1,-1,-1,-1),
    _mm256_set_epi32( 0, 0,-1,-1,-1,-1,-1,-1),
    _mm256_set_epi32( 0,-1,-1,-1,-1,-1,-1,-1),
    _mm256_set_epi32(-1,-1,-1,-1,-1,-1,-1,-1),
};
#endif

#if defined USE_FILE_SQUARE_EVAL
// 縦型Squareから横型Squareに変換する。
Square f2r(const Square sq) { return Square(sq % 9 * 9 + 8 - sq / 9); }

// 横型BonaPieceから縦型BonaPieceに変換する。
Eval::BonaPiece f2r(const Eval::BonaPiece p)
{
    if (p < Eval::fe_hand_end)
        return p;

    auto wp = p - Eval::fe_hand_end;
    Square sq = Square(wp % 81);
    return p - sq + f2r(sq);
};
#endif

namespace Eval
{
    // 評価関数ファイルを読み込む
    void Evaluater::load(std::string dir)
    {
        std::ifstream ifsKK (path(dir,  KK_BIN), std::ios::binary);
        std::ifstream ifsKKP(path(dir, KKP_BIN), std::ios::binary);
        std::ifstream ifsKPP(path(dir, KPP_BIN), std::ios::binary);

        if (!ifsKK || !ifsKKP || !ifsKPP)
        {
            std::cout << "\ninfo string open evaluation file failed.\n";
            return;
        }

#if defined CONSIDER_PROMOTION_IN_EVAL && 0
        {
            const size_t fe_end0 = 1548;
            const size_t size_kk = SQ_MAX * SQ_MAX;
            const size_t size_kkp = (size_t)SQ_MAX * (size_t)SQ_MAX * (size_t)fe_end0;
            const size_t size_kpp = (size_t)SQ_MAX * (size_t)fe_end0 * (size_t)fe_end0;
            auto kk_e = (ValueKk(*)[SQ_MAX][SQ_MAX])new ValueKk[size_kk];
            auto kkp_e = (ValueKkp(*)[SQ_MAX][SQ_MAX][fe_end0])new ValueKkp[size_kkp];
            auto kpp_e = (ValueKpp(*)[SQ_MAX][fe_end0][fe_end0])new ValueKpp[size_kpp];
            ifsKK.read(reinterpret_cast<char*>(kk_e), size_kk * sizeof(ValueKk));
            ifsKKP.read(reinterpret_cast<char*>(kkp_e), size_kkp * sizeof(ValueKkp));
            ifsKPP.read(reinterpret_cast<char*>(kpp_e), size_kpp * sizeof(ValueKpp));
            auto conv_gold = [](BonaPiece bp) {
                BonaPiece ret = bp;
                if (bp >= f_pro_pawn)
                {
                    if (bp < fe_gold_end)
                    {
                        int offset = (bp - f_pro_pawn) % 162;
                        ret = f_gold + offset;
                    }
                    else
                        ret = bp - 648;
                }
                return ret;
            };

            for (auto k1 : Squares)
                for (auto k2 : Squares)
                    kk_[k1][k2] = (*kk_e)[k1][k2];

            for (auto k1 : Squares)
                for (auto k2 : Squares)
                    for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                        kkp_[k1][k2][p] = (*kkp_e)[k1][k2][conv_gold(p)];

            for (auto k : Squares)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                        kpp_[k][p1][p2] = (*kpp_e)[k][conv_gold(p1)][conv_gold(p2)];

            delete[] kk_e;
            delete[] kkp_e;
            delete[] kpp_e;
        }

        return;
#endif
#ifdef USE_FILE_SQUARE_EVAL
        // x86環境ではKPPT二つ分のメモリを確保しようとするとbad_allocを起こすことがある。
        // 一時バッファ無しで縦型→横型変換するコードが理想だが今のところ思いつかないので
        // なるべくメモリを節約しながら縦横変換を行うことにする。また、省メモリ読み込みに
        // することでx64環境に悪影響を及ぼすことも別にないので、x86、x64両方ともこの読み込み方で行く。
        const size_t size_kk = SQ_MAX * SQ_MAX;
        const size_t size_kkp = SQ_MAX * (int)SQ_MAX * (int)fe_end;
        auto kk2  = (ValueKk(*) [SQ_MAX][SQ_MAX])        new ValueKk[size_kk];
        auto kkp2 = (ValueKkp(*)[SQ_MAX][SQ_MAX][fe_end])new ValueKkp[size_kkp];
        ifsKK.read(reinterpret_cast<char*>(kk2), size_kk * sizeof(ValueKk));
        ifsKKP.read(reinterpret_cast<char*>(kkp2), size_kkp * sizeof(ValueKkp));

        // 元の重みをコピー
        for (auto k1 : Squares)
            for (auto k2 : Squares)
                kk_[f2r(k1)][f2r(k2)] = (*kk2)[k1][k2];

        for (auto k1 : Squares)
            for (auto k2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                    kkp_[f2r(k1)][f2r(k2)][f2r(p)] = (*kkp2)[k1][k2][p];

        // せめてkk, kkpだけでも先に解放してやる。
        delete[] kk2;
        delete[] kkp2;

        // 何回かに分けて読み込んでやる。
        const int div = 3;
        static_assert(SQ_MAX % div == 0, "");
        const Square dsq = SQ_MAX / div;
        const size_t size_kpp = SQ_MAX * (int)fe_end * (int)fe_end / div;
        auto kpp2 = (ValueKpp(*)[dsq][fe_end][fe_end])new ValueKpp[size_kpp];

        for (int i = 0; i < div; i++)
        {
            ifsKPP.read(reinterpret_cast<char*>(kpp2), size_kpp * sizeof(ValueKpp));

            for (Square k = dsq * i; k < dsq * (i + 1); k++)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                        kpp_[f2r(k)][f2r(p1)][f2r(p2)] = (*kpp2)[k - dsq * i][p1][p2];
        }

        delete[] kpp2;
#else
        ifsKK.read(reinterpret_cast<char*>(kk_), sizeof(kk_));
        ifsKKP.read(reinterpret_cast<char*>(kkp_), sizeof(kkp_));
        ifsKPP.read(reinterpret_cast<char*>(kpp_), sizeof(kpp_));
#endif
    }

    // KPP,KPのスケール
    const int FV_SCALE = 32;

    // 全計算
    Score computeAll(const Board& b)
    {
        const auto kk  = (*b.thisThread()->evaluater)->kk_;
        const auto kkp = (*b.thisThread()->evaluater)->kkp_;
        const auto kpp = (*b.thisThread()->evaluater)->kpp_;
        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk0 = b.kingSquare(WHITE);
        auto sq_wk1 = inverse(sq_wk0);
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        EvalSum sum;

        // sum.p[0](BKPP)とsum.p[1](WKPP)をゼロクリア
#if defined HAVE_SSE2 || defined HAVE_SSE4
        sum.m[0] = _mm_setzero_si128();
#else
        sum.data[0] = sum.data[1] = 0;
#endif
        // KKの計算
        sum.p[2] = kk[sq_bk0][sq_wk0];

#ifdef USE_GATHER
        // †白美神†で行われているGATHERを使った高速化。
        // cf. http://denou.jp/tournament2016/img/PR/Hakubishin.pdf

        __m256i zero = _mm256_setzero_si256();
        __m256i sb = zero, sw = zero;

        // 並列足し算をする
        for (int i = 0; i < PIECE_NO_KING; i++)
        {
            auto k0 = list_fb[i];
            sum.p[2] += kkp[sq_bk0][sq_wk0][k0];

            for (int j = 0; j < i; j += 8)
            {
                auto pattern = _mm256_load_si256((const __m256i*)&list_fb[j]);
                auto mask = MASK[std::min(i - j, 8)];

                // gatherで該当する重みを一気に取ってくる。
                // 各要素は16bitだが足し合わせると16bitを超える可能性があるので、high128とlow128に分けて計算する。
                __m256i w = _mm256_mask_i32gather_epi32(zero, (const int*)kpp[sq_bk0][k0], pattern, mask, 4);
                __m256i wlo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 0));
                sb = _mm256_add_epi32(sb, wlo);
                __m256i whi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 1));
                sb = _mm256_add_epi32(sb, whi);

                // 後手も計算
                pattern = _mm256_load_si256((const __m256i*)&list_fw[j]);
                w = _mm256_mask_i32gather_epi32(zero, (const int*)kpp[sq_wk1][list_fw[i]], pattern, mask, 4);
                wlo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 0));
                sw = _mm256_add_epi32(sw, wlo);
                whi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 1));
                sw = _mm256_add_epi32(sw, whi);
            }
        }

        // 8バイトシフトして足す。なるほど……
        sb = _mm256_add_epi32(sb, _mm256_srli_si256(sb, 8));
        _mm_storel_epi64((__m128i*)&sum.p[0], _mm_add_epi32(_mm256_extracti128_si256(sb, 0), _mm256_extracti128_si256(sb, 1)));
        sw = _mm256_add_epi32(sw, _mm256_srli_si256(sw, 8));
        _mm_storel_epi64((__m128i*)&sum.p[1], _mm_add_epi32(_mm256_extracti128_si256(sw, 0), _mm256_extracti128_si256(sw, 1)));
#else
        for (int i = 0; i < PIECE_NO_KING; ++i)
        {
            auto k0 = list_fb[i];
            auto k1 = list_fw[i];

            for (int j = 0; j < i; ++j)
            {
                auto l0 = list_fb[j];
                auto l1 = list_fw[j];

#if defined HAVE_SSE2 || defined HAVE_SSE4
                // SSEによる実装
                // pkppw[l1][0],pkppw[l1][1],pkppb[l0][0],pkppb[l0][1]の16bit変数4つを整数拡張で32bit化して足し合わせる
                __m128i tmp;
                tmp = _mm_set_epi32(0, 0, 
                    *reinterpret_cast<const int32_t*>(&kpp[sq_wk1][k1][l1][0]), 
                    *reinterpret_cast<const int32_t*>(&kpp[sq_bk0][k0][l0][0]));
                tmp = _mm_cvtepi16_epi32(tmp);
                sum.m[0] = _mm_add_epi32(sum.m[0], tmp);
#else
                // やりたい処理はこれ。
                sum.p[0] += kpp[sq_bk0][k0][l0];
                sum.p[1] += kpp[sq_wk1][k1][l1];
#endif
            }

            sum.p[2] += kkp[sq_bk0][sq_wk0][k0];
        }
#endif
        b.state()->sum = sum;
        sum.p[2][0] += b.state()->material * FV_SCALE;

        return Score(sum.sum(b.turn()) / FV_SCALE);
    }

    Score computeDiff(const Board& b)
    {
        const auto kk  = (*b.thisThread()->evaluater)->kk_;
        const auto kkp = (*b.thisThread()->evaluater)->kkp_;
        const auto kpp = (*b.thisThread()->evaluater)->kpp_;
        auto st = b.state();
        EvalSum sum;

        if (!st->sum.isNotEvaluated())
        {
            sum = st->sum;
            goto CALC_DIFF_END;
        }

        auto now = st;
        auto prev = st->previous;

        if (prev->sum.isNotEvaluated())
            return computeAll(b);

        {
            int i;
            auto sq_bk0 = b.kingSquare(BLACK);
            auto sq_wk0 = b.kingSquare(WHITE);
            auto sq_wk1 = inverse(sq_wk0);
            auto list_fb = b.evalList()->pieceListFb();
            auto list_fw = b.evalList()->pieceListFw();
            auto& dp = now->dirty_piece;
            sum = prev->sum;
            int k = dp.dirty_num;
            auto dirty = dp.piece_no[0];

            if (dirty >= PIECE_NO_KING)
            {
                // 先手玉が移動したときの計算
                if (dirty == PIECE_NO_BKING)
                {
                    sum.p[0][0] = 0;
                    sum.p[0][1] = 0;
                    sum.p[2] = kk[sq_bk0][sq_wk0];
#ifdef USE_GATHER
                    __m256i zero = _mm256_setzero_si256();
                    __m256i sb = zero;

                    // 並列足し算をする
                    for (int i = 0; i < PIECE_NO_KING; i++)
                    {
                        auto k0 = list_fb[i];
                        sum.p[2] += kkp[sq_bk0][sq_wk0][k0];

                        for (int j = 0; j < i; j += 8)
                        {
                            auto pattern = _mm256_load_si256((const __m256i*)&list_fb[j]);
                            auto mask = MASK[std::min(i - j, 8)];
                            __m256i w = _mm256_mask_i32gather_epi32(zero, (const int*)kpp[sq_bk0][k0], pattern, mask, 4);
                            __m256i wlo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 0));
                            sb = _mm256_add_epi32(sb, wlo);
                            __m256i whi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 1));
                            sb = _mm256_add_epi32(sb, whi);
                        }
                    }

                    sb = _mm256_add_epi32(sb, _mm256_srli_si256(sb, 8));
                    _mm_storel_epi64((__m128i*)&sum.p[0], _mm_add_epi32(_mm256_extracti128_si256(sb, 0), _mm256_extracti128_si256(sb, 1)));
#else
                    // 片側まるごと計算
                    for (i = 0; i < PIECE_NO_KING; i++)
                    {
                        auto k0 = list_fb[i];
                        sum.p[2] += kkp[sq_bk0][sq_wk0][k0];

                        for (int j = 0; j < i; j++)
                            sum.p[0] += kpp[sq_bk0][k0][list_fb[j]];
                    }
#endif
                    // もうひとつの駒がある
                    if (k == 2)
                    {
                        auto k1 = dp.pre_piece[1].fw;
                        auto k3 = dp.now_piece[1].fw;
                        dirty = dp.piece_no[1];

                        for (i = 0; i < dirty; ++i)
                        {
                            sum.p[1] -= kpp[sq_wk1][k1][list_fw[i]];
                            sum.p[1] += kpp[sq_wk1][k3][list_fw[i]];
                        }
                        for (++i; i < PIECE_NO_KING; ++i)
                        {
                            sum.p[1] -= kpp[sq_wk1][k1][list_fw[i]];
                            sum.p[1] += kpp[sq_wk1][k3][list_fw[i]];
                        }
                    }
                }

                // 後手玉が移動したときの計算
                else
                {
                    assert(dirty == PIECE_NO_WKING);

                    sum.p[1][0] = 0;
                    sum.p[1][1] = 0;
                    sum.p[2] = kk[sq_bk0][sq_wk0];
#ifdef USE_GATHER
                    __m256i zero = _mm256_setzero_si256();
                    __m256i sb = zero, sw = zero;

                    // 並列足し算をする
                    for (int i = 0; i < PIECE_NO_KING; i++)
                    {
                        auto k0 = list_fb[i];
                        sum.p[2] += kkp[sq_bk0][sq_wk0][k0];

                        for (int j = 0; j < i; j += 8)
                        {
                            __m256i pattern = _mm256_load_si256((const __m256i*)&list_fw[j]);
                            auto mask = MASK[std::min(i - j, 8)];
                            __m256i w = _mm256_mask_i32gather_epi32(zero, (const int*)kpp[sq_wk1][list_fw[i]], pattern, mask, 4);
                            __m256i wlo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 0));
                            sw = _mm256_add_epi32(sw, wlo);
                            __m256i whi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 1));
                            sw = _mm256_add_epi32(sw, whi);
                        }
                    }

                    sw = _mm256_add_epi32(sw, _mm256_srli_si256(sw, 8));
                    _mm_storel_epi64((__m128i*)&sum.p[1], _mm_add_epi32(_mm256_extracti128_si256(sw, 0), _mm256_extracti128_si256(sw, 1)));
#else
                    for (i = 0; i < PIECE_NO_KING; i++)
                    {
                        auto k0 = list_fb[i];
                        auto k1 = list_fw[i];
                        sum.p[2] += kkp[sq_bk0][sq_wk0][k0];

                        for (int j = 0; j < i; j++)
                            sum.p[1] += kpp[sq_wk1][k1][list_fw[j]];
                    }
#endif
                    if (k == 2)
                    {
                        auto k0 = dp.pre_piece[1].fb;
                        auto k2 = dp.now_piece[1].fb;

                        dirty = dp.piece_no[1];

                        for (i = 0; i < dirty; ++i)
                        {
                            sum.p[0] -= kpp[sq_bk0][k0][list_fb[i]];
                            sum.p[0] += kpp[sq_bk0][k2][list_fb[i]];
                        }
                        for (++i; i < PIECE_NO_KING; ++i)
                        {
                            sum.p[0] -= kpp[sq_bk0][k0][list_fb[i]];
                            sum.p[0] += kpp[sq_bk0][k2][list_fb[i]];
                        }
                    }
                }
            }

            // 玉以外が移動したときの計算
            else 
            {

#define ADD_BWKPP(W0,W1,W2,W3) { \
          sum.p[0] -= kpp[sq_bk0][W0][list_fb[i]]; \
          sum.p[1] -= kpp[sq_wk1][W1][list_fw[i]]; \
          sum.p[0] += kpp[sq_bk0][W2][list_fb[i]]; \
          sum.p[1] += kpp[sq_wk1][W3][list_fw[i]]; \
}
                if (k == 1)
                {
                    auto k0 = dp.pre_piece[0].fb;
                    auto k1 = dp.pre_piece[0].fw;
                    auto k2 = dp.now_piece[0].fb;
                    auto k3 = dp.now_piece[0].fw;

                    sum.p[2] -= kkp[sq_bk0][sq_wk0][k0];
                    sum.p[2] += kkp[sq_bk0][sq_wk0][k2];

                    for (i = 0; i < dirty; ++i)
                        ADD_BWKPP(k0, k1, k2, k3);
                    for (++i; i < PIECE_NO_KING; ++i)
                        ADD_BWKPP(k0, k1, k2, k3);
                }
                else if (k == 2)
                {
                    // 移動する駒が王以外の2つ。
                    PieceNo dirty2 = dp.piece_no[1];

                    // PIECE_NO_ZERO <= dirty < dirty2 < PIECE_NO_KINGにしておく。
                    if (dirty > dirty2)
                        std::swap(dirty, dirty2);

                    auto k0 = dp.pre_piece[0].fb;
                    auto k1 = dp.pre_piece[0].fw;
                    auto k2 = dp.now_piece[0].fb;
                    auto k3 = dp.now_piece[0].fw;
                    auto m0 = dp.pre_piece[1].fb;
                    auto m1 = dp.pre_piece[1].fw;
                    auto m2 = dp.now_piece[1].fb;
                    auto m3 = dp.now_piece[1].fw;

                    // KKP差分
                    sum.p[2] -= kkp[sq_bk0][sq_wk0][k0];
                    sum.p[2] += kkp[sq_bk0][sq_wk0][k2];
                    sum.p[2] -= kkp[sq_bk0][sq_wk0][m0];
                    sum.p[2] += kkp[sq_bk0][sq_wk0][m2];

                    // KPP差分
                    for (i = 0; i < dirty; ++i)
                    {
                        ADD_BWKPP(k0, k1, k2, k3);
                        ADD_BWKPP(m0, m1, m2, m3);
                    }
                    for (++i; i < dirty2; ++i)
                    {
                        ADD_BWKPP(k0, k1, k2, k3);
                        ADD_BWKPP(m0, m1, m2, m3);
                    }
                    for (++i; i < PIECE_NO_KING; ++i)
                    {
                        ADD_BWKPP(k0, k1, k2, k3);
                        ADD_BWKPP(m0, m1, m2, m3);
                    }

                    sum.p[0] -= kpp[sq_bk0][k0][m0];
                    sum.p[1] -= kpp[sq_wk1][k1][m1];
                    sum.p[0] += kpp[sq_bk0][k2][m2];
                    sum.p[1] += kpp[sq_wk1][k3][m3];
                }
            }
        }

        now->sum = sum;

        // 差分計算終わり
    CALC_DIFF_END:
        sum.p[2][0] += b.state()->material * FV_SCALE;
        return (Score)(sum.sum(b.turn()) / FV_SCALE);
    }

    // 評価関数
    Score evaluate(const Board& b)
    {
        auto score = computeDiff(b);
        assert(score == computeAll(b));
        return score;
    }

#if defined LEARN

    // 絶対値を抑制するマクロ
#define SET_A_LIMIT_TO(X,MIN,MAX)  \
    X[0] = std::min(X[0],(MAX));   \
    X[0] = std::max(X[0],(MIN));   \
    X[1] = std::min(X[1],(MAX));   \
    X[1] = std::max(X[1],(MIN));

    // 学習時はグローバルのテーブルを書き換えればいいか
#define KK  (GlobalEvaluater->kk_)
#define KPP (GlobalEvaluater->kpp_)
#define KKP (GlobalEvaluater->kkp_)

    // KPP配列の同じ重みが入るべき場所に同じ値を書き込む。
    void writeKpp(Square k1, BonaPiece p1, BonaPiece p2, ValueKpp value)
    {
        KPP[k1][p1][p2] = KPP[k1][p2][p1] = value;
    }

    // 書き込む場所を少なくする目的で、一番若いアドレスを返す。
    uint64_t getKppIndex(Square k1, BonaPiece p1, BonaPiece p2)
    {
        const auto q0 = &KPP[0][0][0];
        auto q1 = &KPP[k1][p1][p2] - q0;
        auto q2 = &KPP[k1][p2][p1] - q0;
        return std::min(q1, q2);
    }

    void writeKkp(Square k1, Square k2, BonaPiece p1, ValueKkp value)
    {
        KKP[k1][k2][p1] = value;
    }

    // 勾配をdelta分増やす。
    void Weight::addGrad(WeightValue &delta)
    {
        g[0] += delta[0];
        g[1] += delta[1];
    }

    // 勾配を重みに反映させる。
    bool Weight::update(bool skip_update)
    { 
        if (g[0] == 0 && g[1] == 0)
            return false;

        g2 += WeightValue{ g[0] * g[0] , g[1] * g[1] };

        // 値が小さいうちはskipする
        if (!skip_update)
        {
            if (g2[0] >= 0.1f)
                w[0] = w[0] - eta * g[0] / sqrt(g2[0]);

            if (g2[1] >= 0.1f)
                w[1] = w[1] - eta * g[1] / sqrt(g2[1]);
        }

        g = { 0, 0 };
        return !skip_update;
    }

    Weight (*kk_w_)[SQ_MAX][SQ_MAX];
    Weight(*kpp_w_)[SQ_MAX][fe_end][fe_end];
    Weight(*kkp_w_)[SQ_MAX][SQ_MAX][fe_end];

#define KKW (*kk_w_)
#define KPPW (*kpp_w_)
#define KKPW (*kkp_w_)

    void initGrad()
    {
        if (kk_w_ == nullptr)
        {
            const auto sizekk  = uint64_t(SQ_MAX) * uint64_t(SQ_MAX);
            const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
            const auto sizekkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end);
            kk_w_  = (Weight(*)[SQ_MAX][SQ_MAX])        new Weight[sizekk];
            kpp_w_ = (Weight(*)[SQ_MAX][fe_end][fe_end])new Weight[sizekpp];
            kkp_w_ = (Weight(*)[SQ_MAX][SQ_MAX][fe_end])new Weight[sizekkp];
            memset(kk_w_,  0, sizeof(Weight) * sizekk);
            memset(kpp_w_, 0, sizeof(Weight) * sizekpp);
            memset(kkp_w_, 0, sizeof(Weight) * sizekkp);

            // 元の重みをコピー
            for (auto k1 : Squares)
                for (auto k2 : Squares)
                {
                    KKW[k1][k2].w[0] = LearnFloatType(KK[k1][k2][0]);
                    KKW[k1][k2].w[1] = LearnFloatType(KK[k1][k2][1]);

                    for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                    {
                        KKPW[k1][k2][p].w[0] = LearnFloatType(KKP[k1][k2][p][0]);
                        KKPW[k1][k2][p].w[1] = LearnFloatType(KKP[k1][k2][p][1]);
                    }
                };

            for (auto k : Squares)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    {
                        KPPW[k][p1][p2].w[0] = LearnFloatType(KPP[k][p1][p2][0]);
                        KPPW[k][p1][p2].w[1] = LearnFloatType(KPP[k][p1][p2][1]);
                    }
        }
    }

    // 現在の局面で出現している特徴すべてに対して、勾配値を勾配配列に加算する。
    void addGrad(Board& b, Turn root_turn, double delta_grad)
    {
        auto sq_bk = b.kingSquare(BLACK);
        auto sq_wk = b.kingSquare(WHITE);
        auto sq_ik = inverse(sq_wk);
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();
        auto f = (root_turn == BLACK   ) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);
        auto g = (root_turn == b.turn()) ? LearnFloatType(delta_grad) : -LearnFloatType(delta_grad);

        for (int i = 0; i < PIECE_NO_KING; ++i)
        {
            auto k0 = list_fb[i];
            auto k1 = list_fw[i];
            KKPW[sq_bk][sq_wk][k0].addGrad(WeightValue{ f, g });

            for (int j = 0; j < i; ++j)
            {
                auto l0 = list_fb[j];
                auto l1 = list_fw[j];
                ((Weight*)kpp_w_)[getKppIndex(sq_bk, k0, l0)].addGrad(WeightValue{ +f, g });
                ((Weight*)kpp_w_)[getKppIndex(sq_ik, k1, l1)].addGrad(WeightValue{ -f, g });
            }
        }

        KKW[sq_bk][sq_wk].addGrad(WeightValue{ f, g });
    }

    void updateWeights(uint64_t epoch)
    {
        const bool skip_update = epoch <= Eval::Weight::skip_count;

        // KKPの一番大きな値を表示させることで学習が進んでいるかのチェックに用いる。
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        WeightValue max_kkp{ 0.0f, 0.0f };
#endif
        // こういう処理ってopenmp使えば楽なのかな……
        auto func = [&](size_t id)
        {
            for (auto k1 = 9 * id; k1 < 9 * (id + 1); k1++)
                for (auto k2 : Squares)
                {
                    auto& w = KKW[k1][k2];
#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
                    max_kkp[0] = std::max(max_kkp[0], abs(w.w[0]));
                    max_kkp[1] = std::max(max_kkp[1], abs(w.w[1]));
#endif
                    // wの値にupdateがあったなら、値を制限して、かつ、KKに反映させる。
                    if (w.update(skip_update))
                    {
                        SET_A_LIMIT_TO(w.w, LearnFloatType((int32_t)INT16_MIN * 4), LearnFloatType((int32_t)INT16_MAX * 4));
                        KK[k1][k2] = { (int32_t)std::round(w.w[0]), (int32_t)std::round(w.w[1]) };
                    }
                }

            for (Square k = Square(9 * id); k < 9 * (id + 1); k++)
                for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                    for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    {
                        auto& w = KPPW[k][p1][p2];

                        if (w.update(skip_update))
                        {
                            SET_A_LIMIT_TO(w.w, (LearnFloatType)(INT16_MIN / 2), (LearnFloatType)(INT16_MAX / 2));
                            writeKpp(k, p1, p2, ValueKpp{ (int16_t)std::round(w.w[0]), (int16_t)std::round(w.w[1]) });
                        }
                    }

            for (auto k1 = 9 * id; k1 < 9 * (id + 1); k1++)
                for (auto k2 : Squares)
                    for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                    {
                        auto& w = KKPW[k1][k2][p];

                        if (w.update(skip_update))
                        {
                            SET_A_LIMIT_TO(w.w, (LearnFloatType)(INT16_MIN / 2), (LearnFloatType)(INT16_MAX / 2));
                            KKP[k1][k2][p] = ValueKkp{ (int32_t)std::round(w.w[0]), (int32_t)std::round(w.w[1]) };
                        }
                    }
        };

        std::thread th[9];

        for (int i = 0; i < 9; i++)
            th[i] = std::thread(func, i);

        for (int i = 0; i < 9; i++)
            th[i].join();

#ifdef DISPLAY_STATS_IN_UPDATE_WEIGHTS
        SYNC_COUT << " , max_kkp = " << max_kkp[0] << ", " << max_kkp[1] << SYNC_ENDL;
#endif
    }

    void Evaluater::save(std::string eval_dir, bool save_in_eval_dir)
    {
        auto dir = path(save_in_eval_dir ? evalDir() : evalSaveDir(), eval_dir);
        mkdir(dir);
        std::ofstream ofsKK(path(dir, KK_BIN), std::ios::binary);
        std::ofstream ofsKKP(path(dir, KKP_BIN), std::ios::binary);
        std::ofstream ofsKPP(path(dir, KPP_BIN), std::ios::binary);
        const auto size_kk = uint64_t(SQ_MAX) * uint64_t(SQ_MAX);
        const auto size_kpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
        const auto size_kkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end);
#if defined USE_FILE_SQUARE_EVAL
        auto kk2 = (ValueKk(*)[SQ_MAX][SQ_MAX])        new ValueKk[size_kk];
        auto kkp2 = (ValueKkp(*)[SQ_MAX][SQ_MAX][fe_end])new ValueKkp[size_kkp];
        auto kpp2 = (ValueKpp(*)[SQ_MAX][fe_end][fe_end])new ValueKpp[size_kpp];

        // 元の重みを縦横変換しながらコピー
        for (auto k1 : Squares)
            for (auto k2 : Squares)
                (*kk2)[k1][k2] = kk_[f2r(k1)][f2r(k2)];

        for (auto k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    (*kpp2)[k][p1][p2] = kpp_[f2r(k)][f2r(p1)][f2r(p2)];

        for (auto k1 : Squares)
            for (auto k2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                    (*kkp2)[k1][k2][p] = kkp_[f2r(k1)][f2r(k2)][f2r(p)];
#else
        auto& kk2 = kk_;
        auto& kkp2 = kkp_;
        auto& kpp2 = kpp_;
#endif
        if (!ofsKK.write(reinterpret_cast<char*>(kk2), sizeof(ValueKk)  * size_kk)
            || !ofsKKP.write(reinterpret_cast<char*>(kkp2), sizeof(ValueKkp) * size_kkp)
            || !ofsKPP.write(reinterpret_cast<char*>(kpp2), sizeof(ValueKpp) * size_kpp))
            std::cout << "Error : save_eval() failed" << std::endl;
        else
            std::cout << "finished. folder = " << dir << std::endl;

#if defined USE_FILE_SQUARE_EVAL
        delete[] kk2;
        delete[] kpp2;
        delete[] kkp2;
#endif
        if (save_in_eval_dir)
        {
            std::ofstream fs(path(evalDir(), "config.txt"), std::ios::trunc);
            fs << eval_dir << std::endl;
        }
    }

#ifdef USE_FILE_SQUARE_EVAL
    void Evaluater::saveNoFileConvert(std::string eval_dir)
    {
        eval_dir = path(USI::Options["EvalSaveDir"], eval_dir);
        mkdir(eval_dir);
        std::ofstream ofsKK (path(eval_dir,  KK_BIN), std::ios::binary);
        std::ofstream ofsKKP(path(eval_dir, KKP_BIN), std::ios::binary);
        std::ofstream ofsKPP(path(eval_dir, KPP_BIN), std::ios::binary);
        const auto size_kk  = uint64_t(SQ_MAX) * uint64_t(SQ_MAX);
        const auto size_kpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
        const auto size_kkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end);
        auto& kk2 = kk_;
        auto& kkp2 = kkp_;
        auto& kpp2 = kpp_;
        if (   ! ofsKK.write(reinterpret_cast<char*>(kk2),  sizeof(ValueKk)  * size_kk)
            || !ofsKKP.write(reinterpret_cast<char*>(kkp2), sizeof(ValueKkp) * size_kkp)
            || !ofsKPP.write(reinterpret_cast<char*>(kpp2), sizeof(ValueKpp) * size_kpp))
            std::cout << "Error : save_eval() failed" << std::endl;
        else
            std::cout << "finished. folder = " << eval_dir << std::endl;
        return;
    }

#endif
    void Evaluater::blend(float ratio1, Evaluater & e, float ratio2)
    {
        for (auto k1 : Squares)
            for (auto k2 : Squares)
                for (int i = 0; i < 2; i++)
                    kk_[k1][k2][i] = kk_[k1][k2][i] * ratio1 + e.kk_[k1][k2][i] * ratio2;

        for (auto k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    for (int i = 0; i < 2; i++)
                        kpp_[k][p1][p2][i] = kpp_[k][p1][p2][i] * ratio1 + e.kpp_[k][p1][p2][i] * ratio2;

        for (auto k1 : Squares)
            for (auto k2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                    for (int i = 0; i < 2; i++)
                        kkp_[k1][k2][p][i] = kkp_[k1][k2][p][i] * ratio1 + e.kkp_[k1][k2][p][i] * ratio2;
    }
#endif // LEARN
} // namespace Eval
#endif // EVAL_KPPT
