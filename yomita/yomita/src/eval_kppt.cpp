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

#include "eval_kppt.h"

#ifdef EVAL_KPPT

#include <fstream>
#include "usi.h"
#include "board.h"

#define kk (*et.kk_)
#define kpp (*et.kpp_)
#define kkp (*et.kkp_)

#ifdef HAVE_BMI2
#define USE_GATHER
#endif

#ifdef USE_GATHER
static const __m256i MASK[9] = {
    _mm256_setzero_si256(),
    _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, -1),
    _mm256_set_epi32(0, 0, 0, 0, 0, 0, -1, -1),
    _mm256_set_epi32(0, 0, 0, 0, 0, -1, -1, -1),
    _mm256_set_epi32(0, 0, 0, 0, -1, -1, -1, -1),
    _mm256_set_epi32(0, 0, 0, -1, -1, -1, -1, -1),
    _mm256_set_epi32(0, 0, -1, -1, -1, -1, -1, -1),
    _mm256_set_epi32(0, -1, -1, -1, -1, -1, -1, -1),
    _mm256_set_epi32(-1, -1, -1, -1, -1, -1, -1, -1),
};
#endif

namespace Eval
{
    EvalTable et;

    // 評価関数ファイルを読み込む
    void loadSub()
    {
        std::ifstream ifsKK(path((std::string)USI::Options["EvalDir"], KK_BIN), std::ios::binary);
        std::ifstream ifsKKP(path((std::string)USI::Options["EvalDir"], KKP_BIN), std::ios::binary);
        std::ifstream ifsKPP(path((std::string)USI::Options["EvalDir"], KPP_BIN), std::ios::binary);

        if (!ifsKK || !ifsKKP || !ifsKPP)
            goto Error;

#ifdef USE_FILE_SQUARE_EVAL
        // 縦型Square用の評価関数バイナリを使うときは横型に変換して読み込む。
        EvalTable et2;
        auto tempeval = new SharedEval();
        et2.set(tempeval);

        ifsKK.read(reinterpret_cast<char*>(*et2.kk_), sizeof(*et2.kk_));
        ifsKKP.read(reinterpret_cast<char*>(*et2.kkp_), sizeof(*et2.kkp_));
        ifsKPP.read(reinterpret_cast<char*>(*et2.kpp_), sizeof(*et2.kpp_));
    
        int y2b[fe_end];

        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; p++)
        {
            if (p < fe_hand_end)
            {
                y2b[p] = p;
            }
            else
            {
                auto wp = p - fe_hand_end;
                Square sq = Square(wp % 81);
                EvalSquare rsq = toEvalSq(sq);
                auto np = p - sq + rsq;
                y2b[p] = np;
            }
        }

        // 元の重みをコピー
        for (auto k1 : Squares)
            for (auto k2 : Squares)
                kk[k1][k2] = (*et2.kk_)[toEvalSq(k1)][toEvalSq(k2)];

        for (auto k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                    kpp[k][p1][p2] = (*et2.kpp_)[toEvalSq(k)][y2b[p1]][y2b[p2]];

        for (auto k1 : Squares)
            for (auto k2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                    kkp[k1][k2][p] = (*et2.kkp_)[toEvalSq(k1)][toEvalSq(k2)][y2b[p]];

        delete tempeval;
#else
        ifsKK.read(reinterpret_cast<char*>(kk), sizeof(kk));
        ifsKKP.read(reinterpret_cast<char*>(kkp), sizeof(kkp));
        ifsKPP.read(reinterpret_cast<char*>(kpp), sizeof(kpp));
#endif
#ifdef LEARN
        evalLearnInit();
#endif
        return;

    Error:;
        std::cout << "\ninfo string open evaluation file failed.\n";
    }

    // KPP,KPのスケール
    const int FV_SCALE = 32;

    // 駒割り以外の全計算
    // b.st->BKPP,WKPP,KPPを初期化する。Board::set()で一度だけ呼び出される。(以降は差分計算)
    Score computeEval(const Board& b)
    {
        assert(kpp != nullptr);

        auto sq_bk0 = b.kingSquare(BLACK);
        auto sq_wk0 = b.kingSquare(WHITE);
        auto sq_wk1 = inverse(sq_wk0);
        auto list_fb = b.evalList()->pieceListFb();
        auto list_fw = b.evalList()->pieceListFw();

        EvalSum sum;

        // sum.p[0](BKPP)とsum.p[1](WKPP)をゼロクリア
        sum.m[0] = _mm_setzero_si128();

        // KKの計算
        sum.p[2] = kk[sq_bk0][sq_wk0];

#ifdef USE_GATHER
        // †白美神†で行われているGATHERを使った高速化。
        // cf. http://denou.jp/tournament2016/img/PR/Hakubishin.pdf
        // ただ、_mm256_load_si256が使えなかったので代わりにloaduを使った。
        __m256i zero = _mm256_setzero_si256();
        __m256i sb = zero, sw = zero;

        // 並列足し算をする
        for (int i = 0; i < PIECE_NO_KING; i++)
        {
            auto k0 = list_fb[i];
            sum.p[2] += kkp[sq_bk0][sq_wk0][k0];

            for (int j = 0; j < i; j += 8)
            {
                auto pattern = _mm256_loadu_si256((const __m256i*)&list_fb[j]);
                auto mask = MASK[std::min(i - j, 8)];

                // gatherで該当する重みを一気に取ってくる。
                // 各要素は16bitだが足し合わせると16bitを超える可能性があるので、high128とlow128に分けて計算する。
                __m256i w = _mm256_mask_i32gather_epi32(zero, (const int*)kpp[sq_bk0][k0], pattern, mask, 4);
                __m256i wlo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 0));
                sb = _mm256_add_epi32(sb, wlo);
                __m256i whi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 1));
                sb = _mm256_add_epi32(sb, whi);

                // 後手も計算
                pattern = _mm256_loadu_si256((const __m256i*)&list_fw[j]);
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
#if 0
                // やりたい処理はこれ。
                sum.p[0] += pkppb[l0];
                sum.p[1] += pkppw[l1];
#else
                // SSEによる実装
                // pkppw[l1][0],pkppw[l1][1],pkppb[l0][0],pkppb[l0][1]の16bit変数4つを整数拡張で32bit化して足し合わせる
                __m128i tmp;
                tmp = _mm_set_epi32(0, 0, 
                    *reinterpret_cast<const int32_t*>(&kpp[sq_wk1][k1][l1][0]), 
                    *reinterpret_cast<const int32_t*>(&kpp[sq_bk0][k0][l0][0]));
                tmp = _mm_cvtepi16_epi32(tmp);
                sum.m[0] = _mm_add_epi32(sum.m[0], tmp);
#endif
            }

            sum.p[2] += kkp[sq_bk0][sq_wk0][k0];
        }
#endif
        b.state()->sum = sum;
        sum.p[2][0] += b.state()->material * FV_SCALE;

        return Score(sum.sum(b.turn()) / FV_SCALE);
    }

    Score calcEvalDiff(const Board& b)
    {
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
            return computeEval(b);

        // この差分を求める
        {
            auto sq_bk0 = b.kingSquare(BLACK);
            auto sq_wk0 = b.kingSquare(WHITE);
            auto sq_wk1 = inverse(sq_wk0);

            auto list_fb = b.evalList()->pieceListFb();
            auto list_fw = b.evalList()->pieceListFw();

            int k0, k1, k2, k3;
            int i, j;
            auto& dp = now->dirty_piece;
            sum = prev->sum;
            
            // 移動させた駒は最大2つある。その数
            int k = dp.dirty_num;

            auto dirty = dp.piece_no[0];

            if (dirty >= PIECE_NO_KING) // 王と王でないかで場合分け
            {
                if (dirty == PIECE_NO_BKING)
                {
                    // ----------------------------
                    // 先手玉が移動したときの計算
                    // ----------------------------

                    // 現在の玉の位置に移動させて計算する。
                    // 先手玉に関するKKP,KPPは全計算なので一つ前の値は関係ない。

                    // BKPP
                    sum.p[0][0] = 0;
                    sum.p[0][1] = 0;

                    // このときKKPは差分で済まない。
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
                            auto pattern = _mm256_loadu_si256((const __m256i*)&list_fb[j]);
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
                        k0 = list_fb[i];
                        sum.p[2] += kkp[sq_bk0][sq_wk0][k0];

                        for (j = 0; j < i; j++)
                            sum.p[0] += kpp[sq_bk0][k0][list_fb[j]];
                    }
#endif
                    // もうひとつの駒がないならこれで計算終わりなのだが。
                    if (k == 2)
                    {
                        // この駒についての差分計算をしないといけない。
                        k1 = dp.pre_piece[1].fw;
                        k3 = dp.now_piece[1].fw;

                        dirty = dp.piece_no[1];

                        // BKPPはすでに計算済みなのでWKPPのみ。
                        // WKは移動していないのでこれは前のままでいい。
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
                else {
                    // ----------------------------
                    // 後手玉が移動したときの計算
                    // ----------------------------
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
                            __m256i pattern = _mm256_loadu_si256((const __m256i*)&list_fw[j]);
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
                        k0 = list_fb[i]; // これ、KKPテーブルにk1側も入れておいて欲しい気はするが..
                        k1 = list_fw[i];
                        sum.p[2] += kkp[sq_bk0][sq_wk0][k0];

                        for (j = 0; j < i; j++)
                            sum.p[1] += kpp[sq_wk1][k1][list_fw[j]];
                    }
#endif
                    if (k == 2)
                    {
                        k0 = dp.pre_piece[1].fb;
                        k2 = dp.now_piece[1].fb;

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
            else {
                // ----------------------------
                // 玉以外が移動したときの計算
                // ----------------------------

#define ADD_BWKPP(W0,W1,W2,W3) { \
          sum.p[0] -= kpp[sq_bk0][W0][list_fb[i]]; \
          sum.p[1] -= kpp[sq_wk1][W1][list_fw[i]]; \
          sum.p[0] += kpp[sq_bk0][W2][list_fb[i]]; \
          sum.p[1] += kpp[sq_wk1][W3][list_fw[i]]; \
}
                if (k == 1)
                {
                    // 移動した駒が一つ。

                    k0 = dp.pre_piece[0].fb;
                    k1 = dp.pre_piece[0].fw;
                    k2 = dp.now_piece[0].fb;
                    k3 = dp.now_piece[0].fw;

                    // KKP差分
                    sum.p[2] -= kkp[sq_bk0][sq_wk0][k0];
                    sum.p[2] += kkp[sq_bk0][sq_wk0][k2];

                    // KP値、要らんのでi==dirtyを除く
                    for (i = 0; i < dirty; ++i)
                        ADD_BWKPP(k0, k1, k2, k3);
                    for (++i; i < PIECE_NO_KING; ++i)
                        ADD_BWKPP(k0, k1, k2, k3);
                }
                else if (k == 2)
                {
                    // 移動する駒が王以外の2つ。
                    PieceNo dirty2 = dp.piece_no[1];
                    if (dirty > dirty2) std::swap(dirty, dirty2);
                    // PIECE_NO_ZERO <= dirty < dirty2 < PIECE_NO_KING
                    // にしておく。

                    k0 = dp.pre_piece[0].fb;
                    k1 = dp.pre_piece[0].fw;
                    k2 = dp.now_piece[0].fb;
                    k3 = dp.now_piece[0].fw;

                    int m0, m1, m2, m3;
                    m0 = dp.pre_piece[1].fb;
                    m1 = dp.pre_piece[1].fw;
                    m2 = dp.now_piece[1].fb;
                    m3 = dp.now_piece[1].fw;

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
        // 差分計算
        auto score = calcEvalDiff(b);

#if 0
        // 非差分計算
        auto sscore = computeEval(b);
        auto sum1 = b.state()->sum;
        auto ssscore = computeEvalFast(b);
        auto sum2 = b.state()->sum;

        // 差分計算と非差分計算との計算結果が合致するかのテスト。(さすがに重いのでコメントアウトしておく)
        if (ssscore != sscore)
        {
            std::cout << b << score << sscore << std::endl
                << "sum1[0][0] = " << sum1.p[0][0] << " sum1[0][1] = " << sum1.p[0][1] << "\n"
                << "sum1[1][0] = " << sum1.p[1][0] << " sum1[1][1] = " << sum1.p[1][1] << "\n"
                << "sum1[2][0] = " << sum1.p[2][0] << " sum1[2][1] = " << sum1.p[2][1] << "\n"
                << "sum2[0][0] = " << sum2.p[0][0] << " sum2[0][1] = " << sum2.p[0][1] << "\n"
                << "sum2[1][0] = " << sum2.p[1][0] << " sum2[1][1] = " << sum2.p[1][1] << "\n"
                << "sum2[2][0] = " << sum2.p[2][0] << " sum2[2][1] = " << sum2.p[2][1] << "\n";

        }
        assert(score == computeEval(b));
#endif
        return score;
    }

#ifdef CONVERT_EVAL
    // 縦型Square型のevalファイルを横型Square型のevalファイルに変換する。
    void convertEvalFileToRank()
    {
        USI::isReady();

        int y2b[fe_end];

        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; p++)
        {
            if (p < fe_hand_end)
            {
                y2b[p] = p;
            }
            else
            {
                auto wp = p - fe_hand_end;
                Square sq = Square(wp % 81);
                EvalSquare rsq = toEvalSq(sq);
                auto np = p - sq + rsq;
                y2b[p] = np;
            }
        }

        const auto sizekk = uint64_t(SQ_MAX) * uint64_t(SQ_MAX);
        const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
        const auto sizekkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end);
        auto wkk = new ValueKk[sizekk];
        auto wkpp = new ValueKpp[sizekpp];
        auto wkkp = new ValueKkp[sizekkp];

        // 元の重みをコピー
        for (auto k1 : Squares)
            for (auto k2 : Squares)
            {
                wkk[k1 * SQ_MAX + k2] = kk[toEvalSq(k1)][toEvalSq(k2)];
            };

#define KKP2(k1, k2, p) wkkp[(int)k1 * (int)SQ_MAX * (int)fe_end + (int)k2 * (int)fe_end + (int)p ]
#define KPP2(k, p1, p2) wkpp[(int)k  * (int)fe_end * (int)fe_end + (int)p1 * (int)fe_end + (int)p2]

        for (auto k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                {
                    KPP2(k, p1, p2) = kpp[toEvalSq(k)][y2b[p1]][y2b[p2]];
                }


        for (auto k1 : Squares)
            for (auto k2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                {
                    KKP2(k1, k2, p) = kkp[toEvalSq(k1)][toEvalSq(k2)][y2b[p]];
                }

        const std::string eval_dir = "new_eval";
        mkdir(eval_dir);

        // KK
        std::ofstream ofsKK(path(eval_dir, KK_BIN), std::ios::binary);
        // KKP
        std::ofstream ofsKKP(path(eval_dir, KKP_BIN), std::ios::binary);
        // KPP
        std::ofstream ofsKPP(path(eval_dir, KPP_BIN), std::ios::binary);

        if (!ofsKK.write(reinterpret_cast<char*>(wkk), sizeof(ValueKk) * sizekk))
            goto Error;
        
        if (!ofsKKP.write(reinterpret_cast<char*>(wkkp), sizeof(ValueKkp) * sizekkp))
            goto Error;

        
        if (!ofsKPP.write(reinterpret_cast<char*>(wkpp), sizeof(ValueKpp) * sizekpp))
            goto Error;

        std::cout << "finished. folder = " << eval_dir << std::endl;

        delete[] wkk;
        delete[] wkpp;
        delete[] wkkp;

        return;
    Error:;
        std::cout << "Error : save_eval() failed" << std::endl;
    }

    // 横型Square型のevalファイルを縦型Square型のevalファイルに変換する。
    void convertEvalRankToFile()
    {
        USI::isReady();

        int y2b[fe_end];

        for (BonaPiece p = BONA_PIECE_ZERO; p < fe_end; p++)
        {
            if (p < fe_hand_end)
            {
                y2b[p] = p;
            }
            else
            {
                auto wp = p - fe_hand_end;
                Square sq = Square(wp % 81);
                EvalSquare rsq = toEvalSq(sq);
                auto np = p - sq + rsq;
                y2b[p] = np;
            }
        }

        const auto sizekk = uint64_t(SQ_MAX) * uint64_t(SQ_MAX);
        const auto sizekpp = uint64_t(SQ_MAX) * uint64_t(fe_end) * uint64_t(fe_end);
        const auto sizekkp = uint64_t(SQ_MAX) * uint64_t(SQ_MAX) * uint64_t(fe_end);
        auto wkk = new ValueKk[sizekk];
        auto wkpp = new ValueKpp[sizekpp];
        auto wkkp = new ValueKkp[sizekkp];

        // 元の重みをコピー
        for (auto k1 : Squares)
            for (auto k2 : Squares)
            {
                wkk[toEvalSq(k1) * (int)SQ_MAX + toEvalSq(k2)] = kk[k1][k2];
            };

//#define KKP2(k1, k2, p) wkkp[k1 * (int)SQ_MAX * (int)fe_end + k2 * (int)fe_end + p ]
//#define KPP2(k, p1, p2) wkpp[k  * (int)fe_end * (int)fe_end + p1 * (int)fe_end + p2]

        for (auto k : Squares)
            for (auto p1 = BONA_PIECE_ZERO; p1 < fe_end; ++p1)
                for (auto p2 = BONA_PIECE_ZERO; p2 < fe_end; ++p2)
                {
                    KPP2(toEvalSq(k), y2b[p1], y2b[p2]) = kpp[k][p1][p2];
                }


        for (auto k1 : Squares)
            for (auto k2 : Squares)
                for (auto p = BONA_PIECE_ZERO; p < fe_end; ++p)
                {
                    KKP2(toEvalSq(k1), toEvalSq(k2), y2b[p]) = kkp[k1][k2][p];
                }

        const std::string eval_dir = "new_eval";
        mkdir(eval_dir);

        // KK
        std::ofstream ofsKK(path(eval_dir, KK_BIN), std::ios::binary);
        // KKP
        std::ofstream ofsKKP(path(eval_dir, KKP_BIN), std::ios::binary);
        // KPP
        std::ofstream ofsKPP(path(eval_dir, KPP_BIN), std::ios::binary);

        if (!ofsKK.write(reinterpret_cast<char*>(wkk), sizeof(ValueKk) * sizekk))
            goto Error;

        if (!ofsKKP.write(reinterpret_cast<char*>(wkkp), sizeof(ValueKkp) * sizekkp))
            goto Error;

        if (!ofsKPP.write(reinterpret_cast<char*>(wkpp), sizeof(ValueKpp) * sizekpp))
            goto Error;

        std::cout << "finished. folder = " << eval_dir << std::endl;

        delete[] wkk;
        delete[] wkpp;
        delete[] wkkp;

        return;
    Error:;
        std::cout << "Error : save_eval() failed" << std::endl;
    }
#endif
}
#endif // EVAL_KPPT