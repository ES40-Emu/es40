/* ES40 emulator.
 * Copyright (C) 2026 by gdwnldsKSC
 *
 * WWW    : https://github.com/ES40-Emu/es40
 *
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "REALimage2100.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <utility>

// Finite repeat coordinates only need the fractional IEEE-754 bits.
static double realimage_repeat_fraction(double value)
{
	if constexpr (sizeof(double) != sizeof(uint64_t) ||
		!std::numeric_limits<double>::is_iec559 ||
		std::numeric_limits<double>::digits != 53 ||
		std::numeric_limits<double>::max_exponent != 1024)
		return std::fmod(value, 1.0);
	uint64_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	const unsigned exponent = unsigned((bits >> 52) & 0x7ff);
	if (exponent < 1023)
		return value;
	if (exponent >= 1075)
		return 0;
	bits &= ~((uint64_t(1) << (1075 - exponent)) - 1);
	double integral;
	std::memcpy(&integral, &bits, sizeof(integral));
	return value - integral;
}

#if (defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) && !defined(REALIMAGE_SCALAR_ONLY)
#define REALIMAGE_SSE2 1
#include <emmintrin.h>
#if defined(__AVX2__)
#include <immintrin.h>
#endif
#if defined(_MSC_VER)
#define REALIMAGE_INLINE __forceinline
#else
#define REALIMAGE_INLINE inline
#endif
#if !defined(__AVX2__)
static REALIMAGE_INLINE __m128i realimage_texture2(__m128d s, __m128d t, const uint8_t* texture,
	uint32_t base, uint32_t width, uint32_t height, unsigned row_shift)
{
	auto repeat = [](__m128d q) {
		const __m128d magnitude = _mm_andnot_pd(_mm_set1_pd(-0.0), q);
		if (_mm_movemask_pd(_mm_cmplt_pd(magnitude, _mm_set1_pd(1.0))) == 3)
			return q;
		if (_mm_movemask_pd(_mm_cmplt_pd(magnitude, _mm_set1_pd(2147483648.0))) == 3)
			return _mm_sub_pd(q, _mm_cvtepi32_pd(_mm_cvttpd_epi32(q)));
		return _mm_set_pd(realimage_repeat_fraction(_mm_cvtsd_f64(_mm_unpackhi_pd(q, q))),
			realimage_repeat_fraction(_mm_cvtsd_f64(q)));
	};
	const __m128d u = _mm_sub_pd(_mm_mul_pd(repeat(s), _mm_set1_pd(width)), _mm_set1_pd(0.5)),
		v = _mm_sub_pd(_mm_mul_pd(repeat(t), _mm_set1_pd(height)), _mm_set1_pd(0.5));
	auto floor_int = [](__m128d q) {
		const __m128i truncated = _mm_cvttpd_epi32(q);
		const __m128i below = _mm_castpd_si128(_mm_cmplt_pd(q, _mm_cvtepi32_pd(truncated)));
		return _mm_sub_epi32(truncated, _mm_and_si128(
			_mm_shuffle_epi32(below, _MM_SHUFFLE(3, 1, 2, 0)), _mm_set1_epi32(1)));
	};
	const __m128i x = floor_int(u), y = floor_int(v);
	const __m128d fx = _mm_sub_pd(u, _mm_cvtepi32_pd(x)), fy = _mm_sub_pd(v, _mm_cvtepi32_pd(y));
	const __m128i xmask = _mm_set1_epi32(width - 1), ymask = _mm_set1_epi32(height - 1);
	const __m128i x0 = _mm_slli_epi32(_mm_and_si128(x, xmask), 1),
		y0 = _mm_sll_epi32(_mm_and_si128(y, ymask), _mm_cvtsi32_si128(row_shift)),
		y1 = _mm_sll_epi32(_mm_and_si128(_mm_add_epi32(y, _mm_set1_epi32(1)), ymask), _mm_cvtsi32_si128(row_shift));
	__m128i p00, p10, p01, p11;
	if (!(_mm_movemask_epi8(_mm_cmpeq_epi32(x0, _mm_set1_epi32((width - 1) * 2))) & 255))
	{
		auto pair = [&](__m128i yy) {
			const __m128i at = _mm_add_epi32(_mm_set1_epi32(base), _mm_add_epi32(x0, yy));
			const unsigned a = unsigned(_mm_cvtsi128_si32(at)), b = unsigned(_mm_cvtsi128_si32(_mm_srli_si128(at, 4)));
			uint32_t first, second;
			std::memcpy(&first, texture + a, 4);
			std::memcpy(&second, texture + b, 4);
			return _mm_set_epi32(0, 0, second, first);
		};
		const __m128i top = pair(y0), bottom = pair(y1), mask = _mm_set1_epi32(65535);
		p00 = _mm_and_si128(top, mask);
		p10 = _mm_srli_epi32(top, 16);
		p01 = _mm_and_si128(bottom, mask);
		p11 = _mm_srli_epi32(bottom, 16);
	}
	else
	{
		const __m128i x1 = _mm_slli_epi32(_mm_and_si128(_mm_add_epi32(x, _mm_set1_epi32(1)), xmask), 1);
		auto texel = [&](__m128i xx, __m128i yy) {
			const __m128i at = _mm_add_epi32(_mm_set1_epi32(base), _mm_add_epi32(xx, yy));
			const unsigned a = unsigned(_mm_cvtsi128_si32(at)), b = unsigned(_mm_cvtsi128_si32(_mm_srli_si128(at, 4)));
			uint16_t first, second;
			std::memcpy(&first, texture + a, 2);
			std::memcpy(&second, texture + b, 2);
			return _mm_set_epi32(0, 0, second, first);
		};
		p00 = texel(x0, y0);
		p10 = texel(x1, y0);
		p01 = texel(x0, y1);
		p11 = texel(x1, y1);
	}
	auto channel = [&](unsigned shift, uint32_t mask) {
		const __m128i shifts = _mm_cvtsi32_si128(shift), masks = _mm_set1_epi32(mask);
		auto expand = [&](const __m128i p) {
			return _mm_cvtepi32_pd(_mm_and_si128(_mm_srl_epi32(p, shifts), masks));
		};
		const __m128d a = expand(p00), b = expand(p10), c = expand(p01), d = expand(p11);
		const __m128d top = _mm_add_pd(a, _mm_mul_pd(fx, _mm_sub_pd(b, a))),
			bottom = _mm_add_pd(c, _mm_mul_pd(fx, _mm_sub_pd(d, c)));
		const __m128d color = _mm_mul_pd(_mm_add_pd(top,
			_mm_mul_pd(fy, _mm_sub_pd(bottom, top))), _mm_set1_pd(255));
		return _mm_cvttpd_epi32(color);
	};
	const __m128i red = channel(11, 31), green = channel(5, 63), blue = channel(0, 31);
	const __m128i numerators = _mm_packs_epi32(_mm_unpacklo_epi64(red, green), blue);
	// Unsigned factors 33826 (R/B) and 16645 (G) preserve integral RGB thresholds.
	const __m128i colors = _mm_srli_epi16(_mm_mulhi_epu16(numerators,
		_mm_set_epi16(0, 0, -31710, -31710, 16645, 16645, -31710, -31710)), 4);
	const __m128i rg = _mm_unpacklo_epi16(colors, _mm_setzero_si128()),
		b = _mm_unpackhi_epi16(colors, _mm_setzero_si128());
	return _mm_or_si128(_mm_slli_epi32(rg, 16),
		_mm_or_si128(_mm_slli_epi32(_mm_srli_si128(rg, 8), 8), b));
}
static __m128i realimage_texture2_exact(__m128d s, __m128d t, const uint8_t* texture,
	uint32_t base, uint32_t width, uint32_t height, unsigned row_shift)
{
	return realimage_texture2(s, t, texture, base, width, height, row_shift);
}

struct RealImageTextureCoordinates2
{
	__m128i x, y;
	__m128 fx, fy;
};

static REALIMAGE_INLINE RealImageTextureCoordinates2 realimage_texture_coordinates2(
	__m128d s, __m128d t, uint32_t width, uint32_t height)
{
	auto repeat = [](__m128d q) {
		const __m128d magnitude = _mm_andnot_pd(_mm_set1_pd(-0.0), q);
		if (_mm_movemask_pd(_mm_cmplt_pd(magnitude, _mm_set1_pd(1.0))) == 3)
			return q;
		if (_mm_movemask_pd(_mm_cmplt_pd(magnitude, _mm_set1_pd(2147483648.0))) == 3)
			return _mm_sub_pd(q, _mm_cvtepi32_pd(_mm_cvttpd_epi32(q)));
		return _mm_set_pd(realimage_repeat_fraction(_mm_cvtsd_f64(_mm_unpackhi_pd(q, q))),
			realimage_repeat_fraction(_mm_cvtsd_f64(q)));
	};
	const __m128d u = _mm_sub_pd(_mm_mul_pd(repeat(s), _mm_set1_pd(width)), _mm_set1_pd(0.5)),
		v = _mm_sub_pd(_mm_mul_pd(repeat(t), _mm_set1_pd(height)), _mm_set1_pd(0.5));
	auto floor_int = [](__m128d q) {
		const __m128i truncated = _mm_cvttpd_epi32(q);
		const __m128i below = _mm_castpd_si128(_mm_cmplt_pd(q, _mm_cvtepi32_pd(truncated)));
		return _mm_sub_epi32(truncated, _mm_and_si128(
			_mm_shuffle_epi32(below, _MM_SHUFFLE(3, 1, 2, 0)), _mm_set1_epi32(1)));
	};
	const __m128i x = floor_int(u), y = floor_int(v);
	return RealImageTextureCoordinates2{x, y, _mm_cvtpd_ps(_mm_sub_pd(u, _mm_cvtepi32_pd(x))),
		_mm_cvtpd_ps(_mm_sub_pd(v, _mm_cvtepi32_pd(y)))};
}

static REALIMAGE_INLINE __m128i realimage_texture4(__m128d s01, __m128d t01,
	__m128d s23, __m128d t23, const uint8_t* texture,
	uint32_t base, uint32_t width, uint32_t height, unsigned row_shift)
{
	const auto lo = realimage_texture_coordinates2(s01, t01, width, height), hi = realimage_texture_coordinates2(s23, t23, width, height);
	const __m128i x = _mm_unpacklo_epi64(lo.x, hi.x), y = _mm_unpacklo_epi64(lo.y, hi.y),
		xmask = _mm_set1_epi32(width - 1), ymask = _mm_set1_epi32(height - 1),
		x0 = _mm_slli_epi32(_mm_and_si128(x, xmask), 1),
		y0 = _mm_sll_epi32(_mm_and_si128(y, ymask), _mm_cvtsi32_si128(row_shift)),
		y1 = _mm_sll_epi32(_mm_and_si128(_mm_add_epi32(y, _mm_set1_epi32(1)), ymask), _mm_cvtsi32_si128(row_shift));
	const __m128 fx = _mm_movelh_ps(lo.fx, hi.fx), fy = _mm_movelh_ps(lo.fy, hi.fy);
	__m128i p00, p10, p01, p11;
	if (!_mm_movemask_epi8(_mm_cmpeq_epi32(x0, _mm_set1_epi32((width - 1) * 2))))
	{
		auto pair = [&](__m128i yy) {
			const __m128i at = _mm_add_epi32(_mm_set1_epi32(base), _mm_add_epi32(x0, yy));
			uint32_t a, b, c, d;
			std::memcpy(&a, texture + unsigned(_mm_cvtsi128_si32(at)), 4);
			std::memcpy(&b, texture + unsigned(_mm_cvtsi128_si32(_mm_srli_si128(at, 4))), 4);
			std::memcpy(&c, texture + unsigned(_mm_cvtsi128_si32(_mm_srli_si128(at, 8))), 4);
			std::memcpy(&d, texture + unsigned(_mm_cvtsi128_si32(_mm_srli_si128(at, 12))), 4);
			return _mm_set_epi32(d, c, b, a);
		};
		const __m128i top = pair(y0), bottom = pair(y1), mask = _mm_set1_epi32(65535);
		p00 = _mm_and_si128(top, mask);
		p10 = _mm_srli_epi32(top, 16);
		p01 = _mm_and_si128(bottom, mask);
		p11 = _mm_srli_epi32(bottom, 16);
	}
	else
	{
		const __m128i x1 = _mm_slli_epi32(_mm_and_si128(_mm_add_epi32(x, _mm_set1_epi32(1)), xmask), 1);
		auto texel = [&](__m128i xx, __m128i yy) {
			const __m128i at = _mm_add_epi32(_mm_set1_epi32(base), _mm_add_epi32(xx, yy));
			uint16_t a, b, c, d;
			std::memcpy(&a, texture + unsigned(_mm_cvtsi128_si32(at)), 2);
			std::memcpy(&b, texture + unsigned(_mm_cvtsi128_si32(_mm_srli_si128(at, 4))), 2);
			std::memcpy(&c, texture + unsigned(_mm_cvtsi128_si32(_mm_srli_si128(at, 8))), 2);
			std::memcpy(&d, texture + unsigned(_mm_cvtsi128_si32(_mm_srli_si128(at, 12))), 2);
			return _mm_set_epi32(d, c, b, a);
		};
		p00 = texel(x0, y0);
		p10 = texel(x1, y0);
		p01 = texel(x0, y1);
		p11 = texel(x1, y1);
	}
	const __m128i full = _mm_and_si128(_mm_and_si128(p00, p10), _mm_and_si128(p01, p11));
	// The upward green scale keeps maximum texels at 255 within the guarded error bound.
	__m128 ambiguous = _mm_setzero_ps();
	auto channel = [&](unsigned shift, unsigned mask) {
		const __m128i shifts = _mm_cvtsi32_si128(shift), masks = _mm_set1_epi32(mask);
		auto expand = [&](__m128i value) {
			return _mm_cvtepi32_ps(_mm_and_si128(_mm_srl_epi32(value, shifts), masks));
		};
		const __m128 a = expand(p00), b = expand(p10), c = expand(p01), d = expand(p11),
			top = _mm_add_ps(a, _mm_mul_ps(fx, _mm_sub_ps(b, a))),
			bottom = _mm_add_ps(c, _mm_mul_ps(fx, _mm_sub_ps(d, c))),
			value = _mm_mul_ps(_mm_add_ps(top, _mm_mul_ps(fy, _mm_sub_ps(bottom, top))), _mm_set1_ps(mask == 63 ? 0x1.030c32p+2f : 255.0f / mask));
		const __m128i maximum = _mm_cmpeq_epi32(_mm_and_si128(_mm_srl_epi32(full, shifts), masks), masks),
			integer = _mm_cvttps_epi32(value);
		const __m128 fraction = _mm_sub_ps(value, _mm_cvtepi32_ps(integer)),
			near_boundary = _mm_or_ps(_mm_and_ps(_mm_cmpge_ps(value, _mm_set1_ps(1)),
				_mm_cmplt_ps(fraction, _mm_set1_ps(1.0f / 1024))),
				_mm_cmpgt_ps(fraction, _mm_set1_ps(1 - 1.0f / 1024)));
		ambiguous = _mm_or_ps(ambiguous, _mm_andnot_ps(_mm_castsi128_ps(maximum), near_boundary));
		return integer;
	};
	__m128i rgb = _mm_or_si128(_mm_slli_epi32(channel(11, 31), 16),
		_mm_or_si128(_mm_slli_epi32(channel(5, 63), 8), channel(0, 31)));
	const unsigned fallback = unsigned(_mm_movemask_ps(ambiguous));
	if (fallback & 3)
		rgb = _mm_unpacklo_epi64(realimage_texture2_exact(s01, t01, texture, base, width, height, row_shift),
			_mm_srli_si128(rgb, 8));
	if (fallback & 12)
		rgb = _mm_unpacklo_epi64(rgb, realimage_texture2_exact(s23, t23, texture, base, width, height, row_shift));
	return rgb;
}

#else
static REALIMAGE_INLINE __m128i realimage_texture4(__m256d s, __m256d t, const uint8_t* texture,
	uint32_t base, uint32_t width, uint32_t height, unsigned row_shift)
{
	auto repeat = [](__m256d q) {
		const __m256d magnitude = _mm256_andnot_pd(_mm256_set1_pd(-0.0), q);
		if (_mm256_movemask_pd(_mm256_cmp_pd(magnitude, _mm256_set1_pd(1.0), _CMP_LT_OQ)) == 15)
			return q;
		if (_mm256_movemask_pd(_mm256_cmp_pd(magnitude, _mm256_set1_pd(2147483648.0), _CMP_LT_OQ)) == 15)
			return _mm256_sub_pd(q, _mm256_cvtepi32_pd(_mm256_cvttpd_epi32(q)));
		double values[4];
		_mm256_storeu_pd(values, q);
		return _mm256_set_pd(realimage_repeat_fraction(values[3]), realimage_repeat_fraction(values[2]),
			realimage_repeat_fraction(values[1]), realimage_repeat_fraction(values[0]));
	};
	const __m256d u = _mm256_sub_pd(_mm256_mul_pd(repeat(s), _mm256_set1_pd(width)), _mm256_set1_pd(0.5)),
		v = _mm256_sub_pd(_mm256_mul_pd(repeat(t), _mm256_set1_pd(height)), _mm256_set1_pd(0.5));
	const __m256d floor_u = _mm256_floor_pd(u), floor_v = _mm256_floor_pd(v);
	const __m128i x = _mm256_cvttpd_epi32(floor_u), y = _mm256_cvttpd_epi32(floor_v);
	const __m256d fx = _mm256_sub_pd(u, floor_u), fy = _mm256_sub_pd(v, floor_v);
	const __m128i xmask = _mm_set1_epi32(width - 1), ymask = _mm_set1_epi32(height - 1);
	const __m128i x0 = _mm_slli_epi32(_mm_and_si128(x, xmask), 1),
		y0 = _mm_sll_epi32(_mm_and_si128(y, ymask), _mm_cvtsi32_si128(row_shift)),
		y1 = _mm_sll_epi32(_mm_and_si128(_mm_add_epi32(y, _mm_set1_epi32(1)), ymask), _mm_cvtsi32_si128(row_shift));
	__m128i p00, p10, p01, p11;
	if (!_mm_movemask_epi8(_mm_cmpeq_epi32(x0, _mm_set1_epi32((width - 1) * 2))))
	{
		auto pair = [&](__m128i yy) {
			const __m128i at = _mm_add_epi32(_mm_set1_epi32(base), _mm_add_epi32(x0, yy));
			return _mm_i32gather_epi32(reinterpret_cast<const int*>(texture), at, 1);
		};
		const __m128i top = pair(y0), bottom = pair(y1), mask = _mm_set1_epi32(65535);
		p00 = _mm_and_si128(top, mask);
		p10 = _mm_srli_epi32(top, 16);
		p01 = _mm_and_si128(bottom, mask);
		p11 = _mm_srli_epi32(bottom, 16);
	}
	else
	{
		const __m128i x1 = _mm_slli_epi32(_mm_and_si128(_mm_add_epi32(x, _mm_set1_epi32(1)), xmask), 1);
		auto texel = [&](__m128i xx, __m128i yy) {
			const __m128i at = _mm_add_epi32(_mm_set1_epi32(base), _mm_add_epi32(xx, yy));
			const __m128i packed = _mm_i32gather_epi32(reinterpret_cast<const int*>(texture), _mm_and_si128(at, _mm_set1_epi32(-4)), 1);
			return _mm_and_si128(_mm_srlv_epi32(packed, _mm_slli_epi32(_mm_and_si128(at, _mm_set1_epi32(2)), 3)), _mm_set1_epi32(65535));
		};
		p00 = texel(x0, y0);
		p10 = texel(x1, y0);
		p01 = texel(x0, y1);
		p11 = texel(x1, y1);
	}
	auto channel = [&](unsigned shift, uint32_t mask) {
		const __m128i shifts = _mm_cvtsi32_si128(shift), masks = _mm_set1_epi32(mask);
		auto expand = [&](const __m128i p) {
			return _mm256_cvtepi32_pd(_mm_and_si128(_mm_srl_epi32(p, shifts), masks));
		};
		const __m256d a = expand(p00), b = expand(p10), c = expand(p01), d = expand(p11);
		const __m256d top = _mm256_add_pd(a, _mm256_mul_pd(fx, _mm256_sub_pd(b, a))),
			bottom = _mm256_add_pd(c, _mm256_mul_pd(fx, _mm256_sub_pd(d, c)));
		const __m256d color = _mm256_mul_pd(_mm256_add_pd(top,
			_mm256_mul_pd(fy, _mm256_sub_pd(bottom, top))), _mm256_set1_pd(255));
		return _mm256_cvttpd_epi32(color);
	};
	const __m128i red = channel(11, 31), green = channel(5, 63), blue = channel(0, 31);
	const __m256i numerators = _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_packs_epi32(red, green)),
		_mm_packs_epi32(blue, _mm_setzero_si128()), 1);
	const __m256i colors = _mm256_srli_epi16(_mm256_mulhi_epu16(numerators,
		_mm256_set_epi16(0,0,0,0,-31710,-31710,-31710,-31710,16645,16645,16645,16645,-31710,-31710,-31710,-31710)), 4);
	const __m128i rg = _mm256_castsi256_si128(colors), b = _mm256_extracti128_si256(colors, 1);
	return _mm_or_si128(_mm_slli_epi32(_mm_unpacklo_epi16(rg, _mm_setzero_si128()), 16),
		_mm_or_si128(_mm_slli_epi32(_mm_unpackhi_epi16(rg, _mm_setzero_si128()), 8), _mm_unpacklo_epi16(b, _mm_setzero_si128())));
}

static __m128i realimage_texture4_exact(__m256d s, __m256d t, const uint8_t* texture,
	uint32_t base, uint32_t width, uint32_t height, unsigned row_shift)
{
	return realimage_texture4(s, t, texture, base, width, height, row_shift);
}

struct RealImageTextureCoordinates4
{
	__m128i x, y;
	__m128 fx, fy;
};

static REALIMAGE_INLINE RealImageTextureCoordinates4 realimage_texture_coordinates4(
	__m256d s, __m256d t, uint32_t width, uint32_t height)
{
	auto repeat = [](__m256d q) {
		const __m256d magnitude = _mm256_andnot_pd(_mm256_set1_pd(-0.0), q);
		if (_mm256_movemask_pd(_mm256_cmp_pd(magnitude, _mm256_set1_pd(1.0), _CMP_LT_OQ)) == 15)
			return q;
		if (_mm256_movemask_pd(_mm256_cmp_pd(magnitude, _mm256_set1_pd(2147483648.0), _CMP_LT_OQ)) == 15)
			return _mm256_sub_pd(q, _mm256_cvtepi32_pd(_mm256_cvttpd_epi32(q)));
		double values[4];
		_mm256_storeu_pd(values, q);
		return _mm256_set_pd(realimage_repeat_fraction(values[3]), realimage_repeat_fraction(values[2]),
			realimage_repeat_fraction(values[1]), realimage_repeat_fraction(values[0]));
	};
	const __m256d u = _mm256_sub_pd(_mm256_mul_pd(repeat(s), _mm256_set1_pd(width)), _mm256_set1_pd(0.5)),
		v = _mm256_sub_pd(_mm256_mul_pd(repeat(t), _mm256_set1_pd(height)), _mm256_set1_pd(0.5)),
		floor_u = _mm256_floor_pd(u), floor_v = _mm256_floor_pd(v);
	return RealImageTextureCoordinates4{_mm256_cvttpd_epi32(floor_u), _mm256_cvttpd_epi32(floor_v),
		_mm256_cvtpd_ps(_mm256_sub_pd(u, floor_u)), _mm256_cvtpd_ps(_mm256_sub_pd(v, floor_v))};
}

static REALIMAGE_INLINE __m256i realimage_texture8(__m256d s0, __m256d t0,
	__m256d s1, __m256d t1, const uint8_t* texture,
	uint32_t base, uint32_t width, uint32_t height, unsigned row_shift)
{
	const auto lo = realimage_texture_coordinates4(s0, t0, width, height), hi = realimage_texture_coordinates4(s1, t1, width, height);
	const __m256i x = _mm256_inserti128_si256(_mm256_castsi128_si256(lo.x), hi.x, 1),
		y = _mm256_inserti128_si256(_mm256_castsi128_si256(lo.y), hi.y, 1),
		xmask = _mm256_set1_epi32(width - 1), ymask = _mm256_set1_epi32(height - 1),
		x0 = _mm256_slli_epi32(_mm256_and_si256(x, xmask), 1),
		y0 = _mm256_sll_epi32(_mm256_and_si256(y, ymask), _mm_cvtsi32_si128(row_shift)),
		y1 = _mm256_sll_epi32(_mm256_and_si256(_mm256_add_epi32(y, _mm256_set1_epi32(1)), ymask), _mm_cvtsi32_si128(row_shift));
	const __m256 fx = _mm256_insertf128_ps(_mm256_castps128_ps256(lo.fx), hi.fx, 1),
		fy = _mm256_insertf128_ps(_mm256_castps128_ps256(lo.fy), hi.fy, 1);
	__m256i p00, p10, p01, p11;
	if (!_mm256_movemask_epi8(_mm256_cmpeq_epi32(x0, _mm256_set1_epi32((width - 1) * 2))))
	{
		auto pair = [&](__m256i yy) {
			const __m256i at = _mm256_add_epi32(_mm256_set1_epi32(base), _mm256_add_epi32(x0, yy));
			return _mm256_i32gather_epi32(reinterpret_cast<const int*>(texture), at, 1);
		};
		const __m256i top = pair(y0), bottom = pair(y1), mask = _mm256_set1_epi32(65535);
		p00 = _mm256_and_si256(top, mask);
		p10 = _mm256_srli_epi32(top, 16);
		p01 = _mm256_and_si256(bottom, mask);
		p11 = _mm256_srli_epi32(bottom, 16);
	}
	else
	{
		const __m256i x1 = _mm256_slli_epi32(_mm256_and_si256(_mm256_add_epi32(x, _mm256_set1_epi32(1)), xmask), 1);
		auto texel = [&](__m256i xx, __m256i yy) {
			const __m256i at = _mm256_add_epi32(_mm256_set1_epi32(base), _mm256_add_epi32(xx, yy));
			const __m256i packed = _mm256_i32gather_epi32(reinterpret_cast<const int*>(texture), _mm256_and_si256(at, _mm256_set1_epi32(-4)), 1);
			return _mm256_and_si256(_mm256_srlv_epi32(packed,
				_mm256_slli_epi32(_mm256_and_si256(at, _mm256_set1_epi32(2)), 3)), _mm256_set1_epi32(65535));
		};
		p00 = texel(x0, y0);
		p10 = texel(x1, y0);
		p01 = texel(x0, y1);
		p11 = texel(x1, y1);
	}
	const __m256i full = _mm256_and_si256(_mm256_and_si256(p00, p10), _mm256_and_si256(p01, p11));
	// Recompute near RGB thresholds; float error stays below 1/4096.
	__m256 ambiguous = _mm256_setzero_ps();
	auto channel = [&](unsigned shift, unsigned mask) {
		const __m128i shifts = _mm_cvtsi32_si128(shift);
		const __m256i masks = _mm256_set1_epi32(mask);
		auto expand = [&](__m256i value) {
			return _mm256_cvtepi32_ps(_mm256_and_si256(_mm256_srl_epi32(value, shifts), masks));
		};
		const __m256 a = expand(p00), b = expand(p10), c = expand(p01), d = expand(p11),
			top = _mm256_add_ps(a, _mm256_mul_ps(fx, _mm256_sub_ps(b, a))),
			bottom = _mm256_add_ps(c, _mm256_mul_ps(fx, _mm256_sub_ps(d, c))),
			value = _mm256_mul_ps(_mm256_add_ps(top, _mm256_mul_ps(fy, _mm256_sub_ps(bottom, top))), _mm256_set1_ps(255.0f / mask));
		const __m256i maximum = _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_srl_epi32(full, shifts), masks), masks),
			integer = _mm256_or_si256(_mm256_and_si256(maximum, _mm256_set1_epi32(255)),
				_mm256_andnot_si256(maximum, _mm256_cvttps_epi32(value)));
		const __m256 fraction = _mm256_sub_ps(value, _mm256_cvtepi32_ps(integer)),
			near_boundary = _mm256_or_ps(_mm256_and_ps(_mm256_cmp_ps(value, _mm256_set1_ps(1), _CMP_GE_OQ),
				_mm256_cmp_ps(fraction, _mm256_set1_ps(1.0f / 1024), _CMP_LT_OQ)),
				_mm256_cmp_ps(fraction, _mm256_set1_ps(1 - 1.0f / 1024), _CMP_GT_OQ));
		ambiguous = _mm256_or_ps(ambiguous, _mm256_andnot_ps(_mm256_castsi256_ps(maximum), near_boundary));
		return integer;
	};
	const __m256i red = channel(11, 31), green = channel(5, 63), blue = channel(0, 31);
	__m256i rgb = _mm256_or_si256(_mm256_slli_epi32(red, 16),
		_mm256_or_si256(_mm256_slli_epi32(green, 8), blue));
	const unsigned fallback = unsigned(_mm256_movemask_ps(ambiguous));
	if (fallback & 15)
		rgb = _mm256_inserti128_si256(rgb, realimage_texture4_exact(s0, t0, texture, base, width, height, row_shift), 0);
	if (fallback & 240)
		rgb = _mm256_inserti128_si256(rgb, realimage_texture4_exact(s1, t1, texture, base, width, height, row_shift), 1);
	return rgb;
}

#endif
#undef REALIMAGE_INLINE
#endif

static bool valid_width(int bits)
{
	return bits == 8 || bits == 16 || bits == 32;
}

static uint32_t width_mask(int bits)
{
	return bits == 32 ? 0xffffffffu : (1u << bits) - 1u;
}

// Bulk context templates write zero to these otherwise undecoded slots.
static bool zero_context_register(uint32_t a)
{
	return a == 0x008005cc || a == 0x008005d4 || a == 0x008005dc;
}

static bool integer_vertex_color_register(uint32_t a)
{
	return !(a & 3) && a >= CRealImage2100::IntegerVertexColorBase &&
		a < CRealImage2100::IntegerVertexColorBase + 16;
}

static bool integer_vertex_register(uint32_t a)
{
	const uint32_t offset = a - CRealImage2100::IntegerVertexColorBase;
	return integer_vertex_color_register(a) || (!(a & 3) && offset < 3 * CRealImage2100::VertexStride &&
		(offset % CRealImage2100::VertexStride) >= 0x10 &&
		(offset % CRealImage2100::VertexStride) < 0x20);
}

static bool flat_vertex_register(uint32_t a)
{
	return a >= CRealImage2100::FlatVertexBase &&
		a < CRealImage2100::FlatVertexBase + 3 * CRealImage2100::VertexStride &&
		(a - CRealImage2100::FlatVertexBase) % CRealImage2100::VertexStride < 0x34;
}

static bool vertex_register(uint32_t a)
{
	return flat_vertex_register(a) ||
		(a >= CRealImage2100::VertexTextureBase &&
		 a < CRealImage2100::VertexTextureBase + 3 * CRealImage2100::VertexStride &&
		 (a - CRealImage2100::VertexTextureBase) % CRealImage2100::VertexStride < 0x34);
}

// Register dispatch

void CRealImage2100::dac_port_map(address_map& map)
{
	map.unmap_value_high();
	map(0x10, 0x10)
		.lrw8(
			NAME([this](offs_t) {
				return u8(m_dac_index);
			}),
			NAME([this](offs_t, u8 v) {
				m_dac_index = uint16_t((m_dac_index & 0xff00) | v);
				m_dac_component = 0;
			}));
	map(0x14, 0x14)
		.lrw8(
			NAME([this](offs_t) {
				return u8(m_dac_index >> 8);
			}),
			NAME([this](offs_t, u8 v) {
				m_dac_index =
					uint16_t((m_dac_index & 255) | (uint16_t(v) << 8));
				m_dac_component = 0;
			}));
	// RGB640 byte registers and the streamed tables used by the NT miniport.
	map(0x18, 0x18)
		.lrw8(
			NAME([this](offs_t) {
				return dac_data_read();
			}),
			NAME([this](offs_t, u8 v) {
				dac_data_write(v);
			}));
}

// RGB indices select triples; WAT and cursor RAM are byte addressed.
uint32_t CRealImage2100::dac_data_offset() const
{
	if (m_dac_index >= 0x4300 && m_dac_index < 0x4400)
		return 0x4300 + 3u * (m_dac_index - 0x4300) + m_dac_component;
	if (m_dac_index >= 0x4800 && m_dac_index < 0x4808)
		return 0x4800 + 3u * (m_dac_index - 0x4800) + m_dac_component;
	return m_dac_index;
}

void CRealImage2100::advance_dac_data()
{
	if ((m_dac_index >= 0x4300 && m_dac_index < 0x4400) ||
		(m_dac_index >= 0x4800 && m_dac_index < 0x4808))
	{
		if (++m_dac_component == 3)
		{
			m_dac_component = 0;
			++m_dac_index;
		}
	}
	else if ((m_dac_index >= 0x0100 && m_dac_index < 0x0140) ||
		(m_dac_index >= 0x0200 && m_dac_index < 0x0240) ||
		(m_dac_index >= 0x1000 && m_dac_index < 0x1400))
		++m_dac_index;
}

uint8_t CRealImage2100::dac_data_read()
{
	const uint8_t value = m_dac_regs[dac_data_offset()];
	advance_dac_data();
	return value;
}

void CRealImage2100::dac_data_write(uint8_t value)
{
	const uint32_t offset = dac_data_offset();
	if (offset == 0xfb)
		m_dac_regs[offset] = (m_dac_regs[offset] & 6) | (value & 1);
	else if (offset < 0xfc || offset > 0xff)
		m_dac_regs[offset] = value;
	advance_dac_data();
}

uint32_t CRealImage2100::misr_step(uint32_t signature, uint32_t pixel)
{
	return ((signature << 1) ^ pixel ^
		((signature & 0x20000000u) ? 0x00800007u : 0)) & 0x3fffffffu;
}

uint32_t CRealImage2100::dac_rgb30(uint32_t pixel)
{
	const uint32_t r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
	return (((r << 2) | (r >> 6)) << 20) |
		(((g << 2) | (g >> 6)) << 10) | (b << 2) | (b >> 6);
}

bool CRealImage2100::capture_misr(uint32_t& signature)
{
	// The supported RGB640 input serializes four RGB888 pixels per VRAM load.
	const uint8_t serializer[] = { 0x30, 0x31, 0x32, 0x33, 0x10, 0x11, 1, 0 };
	if (!std::equal(std::begin(serializer), std::end(serializer), m_dac_regs.begin() + 2))
		return false;
	const unsigned wid_control = m_dac_regs[0x0a] & 7, cursor_control = m_dac_regs[0x4b] & ~0x10u;
	if ((m_dac_regs[0x0a] & 0x18) ||
		(wid_control != 0 && wid_control != 4 && wid_control != 5 && wid_control != 6) ||
		(cursor_control != 0 && cursor_control != 8 && cursor_control != 0x0a &&
			cursor_control != 0x0b) || m_dac_regs[0x57])
		return false;
	Frame frame = dac_frame(Frame{}, nullptr, true);
	if (frame.argb.empty())
		return false;
	const unsigned byte_mask = m_dac_regs[0xf0] | (unsigned(m_dac_regs[0xf1]) << 8);
	for (uint32_t y = 0; y < frame.height; ++y)
		for (uint32_t x = 0; x < frame.width; ++x)
		{
			const unsigned phase = x & 3;
			const unsigned wid = wid_control && (m_dac_regs[0xf2] & (1u << phase)) ?
				(m_auxiliary[size_t(y) * m_color_width + x] >> 12) & 15 : 0;
			const unsigned fb = 0x100 + 4 * wid, overlay = 0x200 + 4 * wid;
			if (m_dac_regs[fb] != 9 || m_dac_regs[fb + 2] || m_dac_regs[fb + 3] ||
				m_dac_regs[overlay] != 4 || m_dac_regs[overlay + 1] ||
				m_dac_regs[overlay + 2] || m_dac_regs[overlay + 3] != 0x44)
				return false;
			uint32_t& pixel = frame.argb[size_t(y) * frame.width + x];
			const unsigned enables = (byte_mask >> (4 * phase)) & 7;
			pixel &= ((enables & 1) ? 0x0000ffu : 0) |
				((enables & 2) ? 0x00ff00u : 0) | ((enables & 4) ? 0xff0000u : 0);
			pixel = dac_rgb30(pixel);
		}
	composite_cursor(frame, true);
	// Accumulate active pixels after cursor composition and before analog blanking.
	uint32_t accumulator = 0x3fffffffu;
	for (uint32_t pixel : frame.argb)
		accumulator = misr_step(accumulator, pixel);
	signature = (~accumulator) & 0x3fffffffu;
	return true;
}

void CRealImage2100::advance_frame()
{
	++m_frame_counter;
	uint8_t& control = m_dac_regs[0xfb];
	// RGB640 requires a disabled frame before another signature capture.
	if (!(control & 1))
	{
		control = 0;
		return;
	}
	if (!(control & 6))
	{
		control = 5;
		std::fill(m_dac_regs.begin() + 0xfc, m_dac_regs.begin() + 0x100, uint8_t(0));
	}
	else if ((control & 6) == 4)
	{
		uint32_t signature;
		if (!capture_misr(signature))
		{
			unimplemented_once("RGB640 MISR input profile", 0x838018, control, true);
			return;
		}
		for (unsigned i = 0; i < 4; ++i)
			m_dac_regs[0xfc + i] = uint8_t(signature >> (8 * i));
		control = 3;
	}
}

// Readback latches, display selection and unit reset.
uint32_t* CRealImage2100::native_register(uint32_t a)
{
	if (a >= DMABase && a < DMABase + DMARegisterCount * 4)
		return &m_dma_regs[(a - DMABase) / 4];
	switch (a)
	{
	case UnitReset:
		return &m_unit_reset;
	case InterruptEnable:
		return &m_interrupt_enable;
	case VGAControl:
		return &m_vga_control;
	case DisplayControl:
		return &m_display_control;
	default:
		return nullptr;
	}
}

// Drawing is synchronous. Synthetic retrace sits inside vertical blank.
uint32_t CRealImage2100::status_read()
{
	const uint32_t phase = m_status_phase;
	m_status_phase = (phase + 1) % StatusFrameReads;
	if (m_status_phase == 0)
		advance_frame();
	uint32_t live = 0;
	if (phase >= StatusFrameReads - 8)
		live |= StatusVBlank;
	if (phase >= StatusFrameReads - 6 && phase < StatusFrameReads - 2)
		live |= StatusVRetrace;
	return live & m_status_mask;
}

uint32_t CRealImage2100::ReadMem(uint32_t a, int bits)
{
	if (!valid_width(bits) || (a & (unsigned(bits) / 8 - 1)))
	{
		report(
			"ACCESS_WIDTH", a, uint32_t(bits),
			"Invalid native width or alignment");
		return 0xffffffffu;
	}
	if (flat_vertex_register(a & ~3u))
		a -= 0x100;
	uint32_t pixel = 0;
	if (framebuffer_access(a, bits, pixel, false))
		return pixel;
	if (a >= 0x838000 && a < 0x838020 &&
		m_dac_ports.has_handler(a - 0x838000))
		return m_dac_ports.read_byte(a - 0x838000);
	if (const uint32_t* reg = native_register(a & ~3u))
		return (*reg >> ((a & 3) * 8)) & width_mask(bits);
	if ((a & ~3u) == Status)
		return (status_read() >> ((a & 3) * 8)) & width_mask(bits);
	if ((a & ~3u) == BoardStatus)
	{
		const uint32_t v = m_frame_counter | (uint32_t(m_board_straps) << 16) |
			(uint32_t(m_board_control) << 24);
		return (v >> ((a & 3) * 8)) & width_mask(bits);
	}
	if ((a & ~3u) == BoardIO)
	{
		const uint32_t v = m_board_io | (uint32_t(BoardIDPCGA3) << 24);
		return (v >> ((a & 3) * 8)) & width_mask(bits);
	}
	if (m_readback.width && a >= HostData && a < HostData + HostReadSize)
	{
		if (bits == 32)
			return host_read();
		const uint32_t value = readback_pixel(m_readback.word);
		return (value >> ((a & 3) * 8)) & width_mask(bits);
	}
	const auto it = m_shadow.find(a & ~3u);
	const uint32_t value = it == m_shadow.end() ? 0 : it->second;
	// Reads return the stored value; readback is modeled, not measured.
	if (!native_storage_register(a & ~3u))
		unimplemented_once("REALimage register", a, value, false);
	return (value >> ((a & 3) * 8)) & width_mask(bits);
}

void CRealImage2100::WriteMem(uint32_t a, int bits, uint32_t v)
{
	if (!valid_width(bits) || (a & (unsigned(bits) / 8 - 1)))
	{
		report("ACCESS_WIDTH", a, v, "Invalid native width or alignment");
		return;
	}
	const uint32_t original_address = a;
	if ((flat_vertex_register(a & ~3u) || integer_vertex_register(a & ~3u)) && bits != 32)
	{
		unimplemented_once("REALimage vertex width (command rejected)", a, v, true);
		return;
	}
	// Flat and smooth floating-point ports address the same vertex slots.
	if (flat_vertex_register(a & ~3u))
		a -= 0x100;
	if (framebuffer_access(a, bits, v, true))
		return;
	if (a >= 0x838000 && a < 0x838020 &&
		m_dac_ports.has_handler(a - 0x838000))
	{
		m_dac_ports.write_byte(a - 0x838000, u8(v));
		return;
	}
	const uint32_t key = a & ~3u, shift = (a & 3) * 8,
				   lanes = width_mask(bits) << shift;
	if (uint32_t* reg = native_register(key))
	{
		*reg = (*reg & ~lanes) | ((v << shift) & lanes);
		// Unit resets cancel an in-flight host transfer, not color storage.
		if (key == UnitReset && !(*reg & (1u << 26)))
		{
			m_pending = {};
			m_readback = {};
		}
		if (key == DMAControl)
		{
			if ((v << shift) & lanes & 0x200u)
				m_dma_irq_pending = false;
			update_irq();
			if (*reg & ~0x200u)
				unimplemented_once("REALimage DMA control profile", a, v, true);
		}
		if (key == DMAInterruptEnable)
		{
			update_irq();
			if (*reg != 0 && *reg != 1 && *reg != 7)
				unimplemented_once("REALimage DMA interrupt profile", a, v, true);
		}
		if (key == DMAReset && (*reg & 1))
		{
			m_dma_regs[(DMACommand - DMABase) / 4] = 0;
			m_dma_irq_pending = false;
			update_irq();
		}
		if (key == DMACommand && (lanes & 0xc0000000) &&
			(*reg & 0xc0000000))
		{
			if (m_dma_regs[(DMAReset - DMABase) / 4] & 1)
				*reg = 0;
			else if (bits == 32)
				dma_command(*reg);
			else
				unimplemented(
					"REALimage DMA transfer (rejected; completion not written)",
					key, *reg, true);
		}
		return;
	}
	if (key == Status)
	{
		// Upper field (bits 23..31) is a software visibility mask. Timing bits
		// continue to track the phase counter.
		// Writes outside that field are not a decoded profile.
		const uint32_t written = (v << shift) & lanes;
		if (written & ~StatusUpperWritable)
			unimplemented_once("REALimage status register", a, v, true);
		else if (written)
			m_status_mask = (m_status_mask & ~StatusUpperWritable) | written;
		return;
	}
	// Whole-longword Alpha miniport writes leave counter and strap bytes unchanged.
	if (key == BoardStatus)
	{
		if (lanes & 0xff000000u)
			m_board_control = uint8_t(((v << shift) & lanes) >> 24);
		return;
	}
	if (key == BoardIO)
	{
		m_board_io = (m_board_io & ~lanes & 0x00ffffffu) |
			((v << shift) & lanes & 0x00ffffffu);
		if (lanes & 0xff000000u)
			m_board_timing = uint8_t(((v << shift) & lanes) >> 24);
		return;
	}
	// Configuration accesses must not consume a pending pixel upload.
	if (a >= HostData &&
		!(key == HostData + 0x400 && (peek(DrawControl) & 0x04000000u)) &&
		(a < HostData + HostDataSize ||
		(m_pending.width && !(peek(DrawControl) & 0x04000000u) &&
			a < HostData + HostUploadSize)))
	{
		if (bits == 32)
			host_data(v);
		else
			unimplemented_once("REALimage host-data width", a, v, true);
		return;
	}
	auto it = m_shadow.find(key);
	if (it == m_shadow.end())
	{
		if (m_shadow.size() >= MaxShadowRegisters)
		{
			unimplemented_once("REALimage register", a, v, true);
			if (!m_shadow_full_warned)
			{
				m_shadow_full_warned = true;
				report(
					"SHADOW_LIMIT", a, v,
					"Native register storage full; further writes ignored");
			}
			return;
		}
		it = m_shadow.emplace(key, 0).first;
	}
	const uint32_t old_value = it->second;
	it->second = (old_value & ~lanes) | ((v << shift) & lanes);
	if ((key == DrawControl && ((old_value ^ it->second) & 0x00000f01u)) ||
		(key == MemoryControl && old_value != it->second))
		m_clear_cache = {};
	if (plane_write(key, lanes, (v << shift) & lanes))
		return;
	if (integer_vertex_register(key))
	{
		if ((key - IntegerVertexColorBase) % VertexStride == 0x1c)
			raster_vertex_command(key, it->second);
	}
	else if (vertex_register(key))
	{
		if (bits != 32)
			unimplemented_once("REALimage vertex width (command rejected)", a, v, true);
		else if ((key - VertexBase) % VertexStride == 0x1c)
		{
			if (flat_vertex_register(original_address) && (it->second == 0x22 || it->second == 0x62))
				raster_vertex_command(original_address, it->second);
			else
				triangle_command(original_address, it->second);
		}
	}
	else if (key == HostCommand || key == FillCommand || key == BitmapCommand || key == BlockCommand)
	{
		// Only longword command launches have been established by the driver.
		if (bits == 32)
		{
			if (key == BlockCommand)
				block_command(it->second);
			else
				start_command(key, it->second);
		}
		else
			unimplemented_once("REALimage command width (command rejected)", a, v, true);
	}
	else if (key == SyncCommand)
	{
		// Idle startup synchronization does not cancel an unfinished upload.
		if (bits != 32 || (it->second && it->second != 0x80000000u) ||
			m_pending.width || m_readback.width)
			unimplemented_once("REALimage synchronization command (command rejected)", a, v, true);
	}
	else if (key == DisplaySelect)
	{
		// Format 3 uses RGB color with independent clock/capability flags.
		if (it->second && (it->second & ~0x4100u) != 0x0600)
			unimplemented_once("REALimage display selector", a, v, true);
	}
	else if (key == BoardSetup)
	{
		if (it->second & ~0x00820000u)
			unimplemented_once("REALimage board setup/profile", a, v, true);
	}
	else if (key == InitializationPort)
	{
		if (it->second)
			unimplemented_once("REALimage initialization port/profile", a, v, true);
	}
	else if (zero_context_register(key))
	{
		if (it->second)
			unimplemented_once("REALimage context register/profile", a, v, true);
	}
	else if (!native_storage_register(key))
	{
		// Unmodeled mask aliases must not leave a stale block mask usable.
		if ((key & ~0x1fe000u) == HostData + 0x400 &&
			(peek(DrawControl) & (1u << 26)))
			for (unsigned bank = 0; bank < PlaneCount; ++bank)
				if (peek(DrawControl) & (0x1000u << bank))
					m_planes[bank].unknown_masks |= (key >> 13) &
						(m_color_width == MaxColorWidth ? 255u : 15u);
		unimplemented_once("REALimage register", a, v, true);
	}
}

// Native drawing profiles decoded from the NT drivers and traces.

uint32_t CRealImage2100::peek(uint32_t a) const
{
	const auto it = m_shadow.find(a & ~3u);
	return it == m_shadow.end() ? 0 : it->second;
}

int CRealImage2100::plane_register(uint32_t a)
{
	if ((a & ~0x001fe01cu) == 0x00e00100u && (a & 0x001fe000u))
		return 16 + int((a & 0x1c) / 4);
	if (a == PlanePixelMask || (a & ~0x1fe000u) == HostData + 0x400)
		return 24;
	// DFE7xx and FFE7xx are the two observed full-selector state forms.
	const uint32_t state_address = a | 0x00200000u;
	switch (state_address)
	{
	case PlaneStateBase + 0x00: case PlaneStateBase + 0x04:
	case PlaneStateBase + 0x08: case PlaneStateBase + 0x0c:
	case PlaneStateBase + 0x10: case PlaneStateBase + 0x14:
	case PlaneStateBase + 0x18: case PlaneStateBase + 0x1c:
	case PlaneStateBase + 0x20:
	case PlaneStateBase + 0x24: case PlaneStateBase + 0x28:
	case PlaneStateBase + 0x2c: case PlaneStateBase + 0x30:
	case PlaneStateBase + 0x34: case PlaneStateBase + 0x38:
	case PlaneStateBase + 0x3c:
		return int((state_address - PlaneStateBase) / 4);
	default:
		return -1;
	}
}

bool CRealImage2100::plane_write(uint32_t a, uint32_t lanes, uint32_t value)
{
	const int index = plane_register(a);
	if (index < 0)
		return false;
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15;
	if (!(control & (1u << 26)))
	{
		unimplemented_once("REALimage plane access", a, value, true);
		return true;
	}
	const bool clear = index >= 16 && index < 24,
		broadcast = clear && (a & ~0x1fu) == PlaneClearColor;
	const uint32_t available = m_color_width == MaxColorWidth ? 255u : 15u,
		groups = a == PlanePixelMask || broadcast ? available : (a >> 13) & 255;
	if ((index == 24 || clear) && (groups & ~available))
	{
		for (unsigned bank = 0; bank < PlaneCount; ++bank)
			if (banks & (1u << bank))
				m_planes[bank].unknown_masks |= groups & available;
		unimplemented_once(clear ? "REALimage clear-source selector" : "REALimage plane mask",
			a, value, true);
		return true;
	}
	// Configuration writes broadcast to the selected 3D-RAM planes.
	for (unsigned bank = 0; bank < PlaneCount; ++bank)
		if (banks & (1u << bank))
		{
			auto& plane = m_planes[bank];
			if (clear)
			{
				const unsigned word = unsigned(index - 16);
				for (unsigned group = 0; group < 8; ++group)
					if (groups & (1u << group))
					{
						if (broadcast && lanes == 0xffffffffu)
						{
							plane.clear_written[group] &= uint8_t(~(1u << word));
							plane.clear_colors[group][word] = 0;
						}
						else if (!broadcast || (plane.clear_written[group] & (1u << word)))
						{
							plane.clear_colors[group][word] =
								(plane_clear_value(bank, group, word) & ~lanes) | value;
							plane.clear_written[group] |= uint8_t(1u << word);
						}
					}
				if (!broadcast)
					continue;
			}
			if (index == 24 && lanes == 0xffffffffu)
				plane.unknown_masks &= ~groups;
			const unsigned end = index == 24 ? PlaneRegisterCount : index + 1;
			for (unsigned i = unsigned(index); i < end; ++i)
			{
				if (index == 24 && !(groups & (1u << (i - 24))))
					continue;
				plane.regs[i] = (plane.regs[i] & ~lanes) | value;
				plane.written |= 1u << i;
			}
		}
	return true;
}

uint32_t CRealImage2100::plane_value(
	unsigned bank, unsigned index, uint32_t fallback) const
{
	const auto& plane = m_planes[bank];
	return plane.written & (1u << index) ? plane.regs[index] : fallback;
}

uint32_t CRealImage2100::plane_clear_value(unsigned bank, unsigned group, unsigned word) const
{
	const auto& plane = m_planes[bank];
	return plane.clear_written[group] & (1u << word) ?
		plane.clear_colors[group][word] : plane_value(bank, 16 + word, 0);
}

bool CRealImage2100::plane_clear_written(unsigned bank, unsigned group, unsigned word) const
{
	return (m_planes[bank].clear_written[group] & (1u << word)) ||
		(m_planes[bank].written & (1u << (16 + word)));
}

bool CRealImage2100::plane_profile(unsigned bank, uint32_t format, uint32_t rop_high,
	uint32_t multiply_control) const
{
	// Callers select the supported operation; comparisons remain guarded.
	const uint32_t compare_mask = plane_value(bank, 10, 0);
	return plane_value(bank, 1, 0) == 0 && plane_value(bank, 2, 0) == 0 &&
		plane_value(bank, 3, 0) == 0 &&
		(plane_value(bank, 4, 0x03030303) & 0xf0f0f0f0) == rop_high &&
		plane_value(bank, 5, 0x0a000000) == 0x0a000000 &&
		plane_value(bank, 6, 0) == 0 && plane_value(bank, 7, 0) == 0 &&
		plane_value(bank, 8, 0) == multiply_control &&
		plane_value(bank, 9, 0) == 0 &&
		(compare_mask == 0 || compare_mask == 0x00ff0000) &&
		plane_value(bank, 11, 0x33300000) == 0x33300000 &&
		plane_value(bank, 12, 0x100) == 0x100 && plane_value(bank, 13, 0) == 0 &&
		plane_value(bank, 14, 0x100) == format &&
		plane_value(bank, 15, 0) == 0;
}

bool CRealImage2100::native_storage_register(uint32_t a) const
{
	return plane_register(a) >= 0 || vertex_register(a) || integer_vertex_register(a) ||
		a == TextureBase || a == ContextLink ||
		a == DrawControl || a == MemoryControl || a == PixelControl ||
		a == Foreground || a == Background || a == HostOrigin ||
		a == MonoPattern0 || a == MonoPattern1 ||
		a == MonoPattern2 || a == MonoPattern3 ||
		a == HostExtent || a == FillOrigin || a == FillExtent ||
		a == HostCommand || a == FillCommand ||
		a == BitmapForeground || a == BitmapBackground ||
		a == BitmapPattern0 || a == BitmapPattern1 ||
		a == BitmapOrigin || a == BitmapExtent || a == BitmapCommand ||
		a == BlockSource || a == BlockDestination || a == BlockExtent ||
		a == BlockCommand ||
		a == ContextControl || a == DisplaySelect ||
		a == BoardSetup || a == InitializationPort ||
		a == BoardTiming || a == WindowMask ||
		a == ClipXMax || a == ClipYMax || a == ClipXMin || a == ClipYMin ||
		a == GlobalControl0 || a == GlobalControl1 || a == GlobalControl2 ||
		a == PipelineControl0 || a == PipelineControl1 ||
		a == PipelineControl2 || a == PipelineControl3 ||
		a == PipelineControl4 || a == PipelineControl5 ||
		zero_context_register(a) ||
		(a >= TimingBase && a <= TimingBase + 0x1c);
}

bool CRealImage2100::native_pixel_profile() const
{
	const uint32_t memory = peek(MemoryControl);
	return (memory & 0x00ffffff) == 0x8000 &&
		(peek(PixelControl) & ~0x08000000u) ==
			(m_color_width == MaxColorWidth ? 0x62722060u : 0x42722060u);
}

uint32_t CRealImage2100::block_width() const
{
	const uint32_t format = peek(DrawControl) & 255;
	return format == 2 ? 8 : format == 3 && m_color_width == MaxColorWidth ? 16 : 0;
}

bool CRealImage2100::native_copy_control_profile(bool clear, bool integer) const
{
	const uint32_t width = block_width(), columns = (peek(DrawControl) >> 8) & 15,
		copy_columns = ((peek(MemoryControl) >> 24) & 63) + 1;
	// Each page column spans two copy blocks; the top two bits control timing.
	if (!native_pixel_profile() || !width || columns != (copy_columns + 1) / 2 ||
		copy_columns > (m_color_width + 10 * width - 1) / (10 * width))
		return false;
	const uint32_t global = peek(GlobalControl0), control = peek(DrawControl);
	const bool integer_profile = integer || (clear && ((control & ~0x0400ff01u) == 0xa1000002u ||
		(control & ~0x04ff0f01u) == 0xa1004002u));
	// Integer raster commands can retain the preceding line mode.
	const bool retained_line = clear && integer_profile && (global == 1 || global == 3);
	const bool context_clear = clear && (global == 0x180 || global == 0x190) &&
		peek(GlobalControl1) == 0x20800;
	// Other programmed pipeline modes have not been decoded.
	const std::pair<uint32_t, uint32_t> profile[] = {
		{GlobalControl0, integer_profile ? (retained_line ? global : 0u) : context_clear ? global : 1u},
		{GlobalControl1, integer_profile ? 0x20810u : context_clear ? 0x20800u : 0x20811u},
		{GlobalControl2, 0x33},
		{PipelineControl0, 0}, {PipelineControl1, 0},
		{PipelineControl2, 0x10000000}, {PipelineControl3, 0},
		{PipelineControl4, 0}, {PipelineControl5, 0},
		{0x008005cc, 0}, {0x008005d4, 0}, {0x008005dc, 0}};
	for (const auto& reg : profile)
	{
		const auto it = m_shadow.find(reg.first);
		if (it != m_shadow.end() && it->second != reg.second)
			return false;
	}
	return true;
}

bool CRealImage2100::copy_profile() const
{
	// Only the observed RGB copy profile; bits 12..15 select banks.
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15;
	return (control & ~0xff01u) == 0x81000002 && banks && !(banks & ~3u) &&
		native_copy_control_profile();
}

bool CRealImage2100::block_copy_profile() const
{
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15;
	if ((control & ~0xff01u) != 0xa1000002u ||
		(banks != 1 && banks != 2 && banks != 5 && banks != 6) ||
		!native_copy_control_profile(true, true))
		return false;
	const unsigned bank = banks & 2 ? 1 : 0;
	if (!plane_profile(bank) || m_planes[bank].unknown_masks ||
		((plane_value(bank, 0, 0xffffffff) & 0xff000000u) &&
			(plane_value(bank, 4, 0x03030303) >> 24) != 5))
		return false;
	if (!(banks & 4))
		return true;
	if (m_planes[2].unknown_masks)
		return false;
	const uint32_t expected[] = {0, 0, 0x0fff0fff, 0x03030303, 0x0a000000,
		0, 0, 0, 0, 0x00ff0000, 0x33300000, 0x100, 0, 0x100, 0};
	for (unsigned i = 0; i < std::size(expected); ++i)
		if (plane_value(2, i + 1, expected[i]) != expected[i])
			return false;
	return true;
}

bool CRealImage2100::block_transfer_profile(bool upload, bool native_command) const
{
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15;
	if (upload && !native_command && (banks == 5 || banks == 6))
		return integer_rgb_profile();
	const bool native_transfer = native_command || (upload && (banks == 1 || banks == 2) &&
		(control & ~0x00ffff01u) == 0xa1000002u);
	// Native RGB transfers retain inactive window references.
	const uint32_t fields = native_transfer ? 0x00ffff01u : 0xff01u,
		mode = native_transfer ? 0xa1000002u : 0x21000002u;
	if ((control & ~fields) != mode || !banks || (banks & ~3u) ||
		!native_copy_control_profile(true, true))
		return false;
	for (unsigned bank = 0; bank < 2; ++bank)
		if (banks & (1u << bank))
		{
			if (!plane_profile(bank) || m_planes[bank].unknown_masks ||
				((plane_value(bank, 0, 0xffffffff) & 0xff000000u) &&
					(plane_value(bank, 4, 0x03030303) >> 24) != 5))
				return false;
		}
	return true;
}

bool CRealImage2100::bitmap_profile() const
{
	const uint32_t control = peek(DrawControl);
	if ((control & ~0x00ff0f01u) != 0xa1004002u ||
		!native_copy_control_profile(true, true) ||
		plane_value(2, 0, 0) != 0x10000000u || m_planes[2].unknown_masks ||
		((peek(ClipXMin) | peek(ClipYMin) | peek(ClipXMax) | peek(ClipYMax)) & 0xffff000fu))
		return false;
	const uint32_t expected[] = {0, 0, 0x0fff0fff, 0x03030303, 0x0a000000,
		0, 0, 0, 0, 0x00ff0000, 0x33300000, 0x100, 0, 0x100, 0};
	for (unsigned i = 0; i < std::size(expected); ++i)
		if (plane_value(2, i + 1, expected[i]) != expected[i])
			return false;
	for (unsigned i = 24; i < 24 + block_width() / 2; ++i)
		if (plane_value(2, i, 0xffffffff) != 0xffffffff)
			return false;
	return true;
}

bool CRealImage2100::auxiliary_mono_profile() const
{
	const uint32_t control = peek(DrawControl);
	if ((control & ~0x00ff0f01u) != 0xa5004002u ||
		!native_copy_control_profile(true, true) || m_planes[2].unknown_masks)
		return false;
	const uint32_t expected[] = {0, 0, 0x0fff0fff, 0x03030303, 0x0a000000,
		0, 0, 0, 0, 0x00ff0000, 0x33300000, 0x100, 0, 0x100, 0};
	for (unsigned i = 0; i < std::size(expected); ++i)
		if (plane_value(2, i + 1, expected[i]) != expected[i])
			return false;
	for (unsigned i = 24; i < 24 + block_width() / 2; ++i)
		if (plane_value(2, i, 0xffffffff) != 0xffffffff)
			return false;
	for (unsigned i = 0; i < 4; ++i)
		if (peek(MonoPattern0 + i * 4) != 0xffffffff)
			return false;
	return true;
}

bool CRealImage2100::integer_rgb_profile(bool configuration) const
{
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15;
	const bool auxiliary_compare = (control & ~0x00ffff01u) == 0xa1000002u &&
		(banks == 5 || banks == 6),
		direct = (control & ~0xff01u) == 0x21000002u && (banks == 1 || banks == 2),
		rectangle = configuration && (control & ~0x00ffff01u) == 0xa5000002u &&
			(banks == 1 || banks == 2);
	if ((!auxiliary_compare && !direct && !rectangle) || !native_copy_control_profile(true, true))
		return false;
	const unsigned bank = banks & 2 ? 1 : 0;
	if (!plane_profile(bank) || m_planes[bank].unknown_masks ||
		((plane_value(bank, 0, 0xffffffff) & 0xff000000u) &&
			(plane_value(bank, 4, 0x03030303) >> 24) != 5))
		return false;
	if (!auxiliary_compare)
		return true;
	if (plane_value(2, 0, 0xffffffff) || m_planes[2].unknown_masks)
		return false;
	const uint32_t expected[] = {0, 0x8000, 0x0fff0fff, 0x03030303, 0x0a000200,
		0, 0, 0, 0, 0x00ff0000, 0x33300000, 0x100, 0, 0x100, 0};
	for (unsigned i = 0; i < std::size(expected); ++i)
		if (plane_value(2, i + 1, expected[i]) != expected[i])
			return false;
	return true;
}

bool CRealImage2100::fast_copy_profile(bool block) const
{
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15,
		fields = block ? 0x00ffff01u : 0xff01u;
	if ((control & ~fields) != 0x21000002 || (banks != 1 && banks != 2) ||
		!native_copy_control_profile(block, block))
		return false;
	const unsigned bank = banks == 2 ? 1 : 0;
	if ((plane_value(bank, 0, 0xffffffff) & 0xffffff) != 0xffffff ||
		plane_value(bank, 1, 0) != 0 || plane_value(bank, 2, 0) != 0 ||
		plane_value(bank, 3, 0) != 0 ||
		plane_value(bank, 4, 0x03030303) != 0x05050505 ||
		plane_value(bank, 5, 0x0a000000) != 0 || plane_value(bank, 6, 0) != 1 ||
		plane_value(bank, 7, 0) != 0 ||
		plane_value(bank, 8, 0) != 0 || plane_value(bank, 9, 0) != 0 ||
		(plane_value(bank, 10, 0) != 0 && (!block || plane_value(bank, 10, 0) != 0x00ff0000)) ||
		plane_value(bank, 11, 0x33300000) != 0x33300000 ||
		plane_value(bank, 12, 0x100) != 0x100 || plane_value(bank, 13, 0) != 0 ||
		plane_value(bank, 14, 0x100) != 0x100 || plane_value(bank, 15, 0) != 0 ||
		m_planes[bank].unknown_masks)
		return false;
	// Native block copies bypass retained configuration masks.
	if (!block)
		for (unsigned i = 24; i < 24 + block_width() / 2; ++i)
			if (plane_value(bank, i, 0xffffffff) != 0xffffffff)
				return false;
	return true;
}

CRealImage2100::ColorWriteContext CRealImage2100::prepare_color_write() const
{
	ColorWriteContext context;
	for (unsigned bank = 0; bank < 2; ++bank)
	{
		context.masks[bank] = plane_value(bank, 0, 0xffffffff);
		context.rops[bank] = plane_value(bank, 4, 0x03030303);
	}
	return context;
}

CRealImage2100::PixelOperation CRealImage2100::prepare_pixel(uint32_t x,
	uint32_t y, uint32_t value, uint32_t banks, uint32_t lanes, bool invalidate_cache) const
{
	return {prepare_color_write(), x, y, value, banks, lanes, invalidate_cache};
}

void CRealImage2100::execute_pixel(const PixelOperation& operation)
{
	if (operation.invalidate_cache)
		for (unsigned bank = 0; bank < 2; ++bank)
			if (operation.banks & (1u << bank))
				m_clear_cache[bank] = {};
	color_write(operation.context, operation.x, operation.y, operation.value,
		operation.banks, operation.lanes);
}

void CRealImage2100::color_write(const ColorWriteContext& context,
	uint32_t x, uint32_t y, uint32_t color, uint32_t banks, uint32_t lanes)
{
	if (x >= m_color_width || y >= m_color_height)
		return;
	const uint32_t offset = y * m_color_width + x;
	for (uint32_t bank = 0; bank < 2; ++bank)
		if (banks & (1u << bank))
		{
			uint32_t& destination = m_color[bank * m_color_pixels + offset];
			const uint32_t mask = context.masks[bank] & lanes & 0xffffff,
				rops = context.rops[bank];
			uint32_t result = 0;
			for (unsigned shift = 0; shift < 24; shift += 8)
			{
				const unsigned op = (rops >> shift) & 15;
				uint32_t channel = 0;
				if (op & 1) channel |= color & destination;
				if (op & 2) channel |= color & ~destination;
				if (op & 4) channel |= ~color & destination;
				if (op & 8) channel |= ~color & ~destination;
				result |= channel & (0xffu << shift);
			}
			destination = (destination & ~mask) | (result & mask);
		}
}

bool CRealImage2100::auxiliary_framebuffer_profile() const
{
	if ((peek(DrawControl) & ~0x00ff0f01u) != 0xa1004002u ||
		!native_copy_control_profile(true, true) || m_planes[2].unknown_masks)
		return false;
	const uint32_t expected[] = {0, 0, 0x0fff0fff, 0x03030303, 0x0a000000,
		0, 0, 0, 0, 0x00ff0000, 0x33300000, 0x100, 0, 0x100, 0};
	for (unsigned i = 0; i < std::size(expected); ++i)
		if (plane_value(2, i + 1, expected[i]) != expected[i] &&
			!(i == 9 && plane_value(2, i + 1, expected[i]) == 0))
			return false;
	return true;
}

bool CRealImage2100::framebuffer_access(
	uint32_t a, int bits, uint32_t& value, bool write)
{
	if (a < 0x01000000 || a >= 0x04000000)
		return false;
	// Fixed front/back/auxiliary windows use an 8 KiB row pitch.
	const uint32_t bank = (a >> 24) - 1, x = (a & 0x1fff) >> 2,
		y = (a & 0x00ffffff) >> 13, shift = (a & 3) * 8,
		control = peek(DrawControl);
	const bool integer_rgb = bank < 2 && (control & ~0xff01u) == 0x21000002u &&
		((control >> 12) & 15) == (1u << bank) && integer_rgb_profile();
	if ((bank == 2 ? write && !auxiliary_framebuffer_profile() :
		(!integer_rgb && !copy_profile()) || !plane_profile(bank)) ||
		x >= m_color_width || y >= m_color_height)
	{
		unimplemented_once("REALimage framebuffer aperture/profile", a, value, write);
		if (!write)
			value = width_mask(bits);
		return true;
	}
	if (bank == 2)
	{
		uint32_t& pixel = m_auxiliary[size_t(y) * m_color_width + x];
		if (write)
		{
			const uint32_t mask = plane_value(2, 0, 0xffffffff) & (width_mask(bits) << shift);
			pixel = (pixel & ~mask) | ((value << shift) & mask);
			m_clear_cache[2] = {};
		}
		else
			value = (pixel >> shift) & width_mask(bits);
		return true;
	}
	if (write)
	{
		execute_pixel(prepare_pixel(x, y, value << shift, 1u << bank,
			width_mask(bits) << shift, true));
	}
	else
		value = (m_color[size_t(bank) * m_color_pixels + y * m_color_width + x] >> shift) &
			width_mask(bits);
	return true;
}

bool CRealImage2100::fill_profile(bool initialization) const
{
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15;
	// Initialization can broadcast the rectangle to RGB and auxiliary planes.
	const bool bootstrap = initialization &&
		(control & ~0xff01u) == 0x05000002 && banks && !(banks & ~7u);
	const bool normal = (control & ~0x040fff01u) == 0x81000002 && banks && !(banks & ~3u);
	return (normal || bootstrap) && native_copy_control_profile(true);
}

static bool block_pattern_layout(const std::array<uint32_t, 64>& values,
	uint32_t width, uint32_t mask)
{
	if (width != 16)
		return true;
	// Only the driver's replicated four-column pattern is decoded for 16-pixel blocks.
	for (unsigned i = 0; i < width * 4; ++i)
		if ((values[i] ^ values[(i & ~15u) | (i & 3u)]) & mask)
			return false;
	return true;
}

CRealImage2100::BlockOperation CRealImage2100::prepare_block(uint32_t v) const
{
	BlockOperation op;
	op.value = v;
	op.color = prepare_color_write();
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15,
		source = peek(BlockSource), destination = peek(BlockDestination),
		extent = peek(BlockExtent), auxiliary_mask = plane_value(2, 0, 0xffffffff),
		auxiliary_value = plane_clear_value(2, 0, 0);
	const bool copy = (v & 0x20000) != 0, configuration = (control & 0x04000000) != 0;
	const bool auxiliary_clear = (banks & 4) && auxiliary_mask;
	const bool seed = !configuration && !copy;
	const uint32_t clear_width = block_width(), groups = clear_width / 2,
		dx = destination & 0x7ff, dy = destination >> 16,
		width = (extent & 0x7ff) + 1, height = (extent >> 16) + 1,
		scale_x = copy ? 10 * clear_width : clear_width, scale_y = copy ? 16 : 4;
	op.banks = banks;
	op.source = source;
	op.clear_width = clear_width;
	op.groups = groups;
	op.x = dx * scale_x;
	op.y = dy * scale_y;
	op.right = op.x + width * scale_x;
	op.bottom = op.y + height * scale_y;
	op.seed_key = ((dy / 4) << 16) | (dx / 10);
	op.auxiliary_mask = auxiliary_mask;
	op.auxiliary_value = auxiliary_value;
	op.copy = copy;
	op.seed = seed;
	op.auxiliary_clear = auxiliary_clear;
	const uint32_t control_fields = 0x040fff01u;
	// Integer profiles retain the fourth byte used by the DAC overlay plane.
	const bool integer_clear = (control & ~0x0400ff01u) == 0xa1000002u;
	const bool integer_seed = integer_clear && seed;
	const bool integer_auxiliary = !copy && (control & ~0x04ff0f01u) == 0xa1004002u;
	op.allow_pattern = integer_clear && !(banks & ~3u);
	for (auto& mask : op.color.masks)
		mask &= integer_clear ? 0xffffffffu : 0x00ffffffu;
	if ((!integer_clear && !integer_auxiliary && (control & ~control_fields) != 0x81000002) ||
		(integer_clear && configuration && (banks & 4) && !integer_auxiliary) ||
		!banks || (banks & ~7u) ||
		v != (((banks ^ 7u) << 18) | (copy ? 0x30000u : 0x10000u)) ||
		!native_copy_control_profile(true) || m_pending.width || m_readback.width ||
		((source | destination | extent) & ~0x07ff07ffu) ||
		(!copy && source) || (copy && !configuration))
		return op;
	op.geometry_known = true;
	if (seed && (extent != 0x00080014 || dx % 10 || dy % 4 ||
		(dx * clear_width < m_color_width && dy * 4 < m_color_height)))
		return op;
	if (auxiliary_clear)
	{
		const bool integer_packed = integer_seed || integer_auxiliary;
		const uint32_t depth_control = plane_value(2, 5, 0),
			format = integer_packed ? 0x100u : 0u;
		// The driver packs window references into the auxiliary clear source.
		const uint32_t references = ((control & 0x000f0000u) >> 4) |
			((control & 0x00f00000u) << 8);
		if (integer_auxiliary && !(integer_seed && auxiliary_mask == 0xffffffffu) &&
			((auxiliary_value ^ references) & auxiliary_mask & 0xf000f000u))
			return op;
		const bool neutral_clear = (!integer_auxiliary || integer_seed) &&
			plane_profile(2, 0) && plane_value(2, 10, 0) == 0 &&
			(auxiliary_mask == 0xffffffffu ||
				(configuration && !(auxiliary_mask & ~0x0000f000u)));
		const bool packed_clear = (auxiliary_mask == 0xffffffffu || integer_auxiliary) &&
			plane_value(2, 1, 0) == 0 && plane_value(2, 2, 0) == (integer_packed ? 0u : 0xf000u) &&
			plane_value(2, 3, 0) == 0x0fff0fff &&
			(integer_packed ? depth_control == 0x0a000000 :
				(depth_control == 0x0a000200 || depth_control == 0x0a000205 ||
					depth_control == 0x0a000207)) &&
			plane_value(2, 6, 0) == 0 && plane_value(2, 7, 0) == 0 &&
			plane_value(2, 8, 0) == 0 && plane_value(2, 9, 0) == 0 &&
			plane_value(2, 10, 0) == 0x00ff0000 &&
			plane_value(2, 11, 0x33300000) == 0x33300000 &&
			plane_value(2, 12, 0x100) == 0x100 && plane_value(2, 13, 0) == 0 &&
			plane_value(2, 14, format) == format && plane_value(2, 15, 0) == 0;
		if ((!neutral_clear && !packed_clear) ||
			plane_value(2, 4, 0x03030303) != 0x03030303 ||
			m_planes[2].unknown_masks)
			return op;
		for (unsigned i = 0; i < groups; ++i)
		{
			const uint32_t bits = plane_value(2, 24 + i, 0xffffffff);
			// Uniform offscreen seeds bypass retained configuration pixel masks.
			if (bits != (bits & 255) * 0x01010101u)
				return op;
		}
	}
	for (unsigned bank = 0; bank < 2; ++bank)
	{
		const uint32_t mask = op.color.masks[bank],
			rops = op.color.rops[bank];
		if (!(banks & (1u << bank)) || !mask)
			continue;
		const bool blend = rops == 0xd0d0d0d0;
		op.replace_color[bank] = blend || (!copy && (rops & 0xffffff) == 0x060606);
		if ((blend && (mask & 0xff000000)) ||
			!plane_profile(bank, 0x100, blend ? rops : 0) ||
			m_planes[bank].unknown_masks)
			return op;
		if (!copy)
			op.colors[bank] = plane_clear_value(bank, 0, 0);
		for (unsigned i = 0; i < groups; ++i)
		{
			const uint32_t bits = plane_value(bank, 24 + i, 0xffffffff);
			if (bits != (bits & 255) * 0x01010101u)
				return op;
		}
	}
	for (unsigned bank = 0; bank < 3; ++bank)
	{
		for (unsigned i = 0; i < groups; ++i)
			op.pixel_masks[bank][i] = plane_value(bank, 24 + i, 0xffffffff);
		const uint32_t mask = bank == 2 ? auxiliary_mask : op.color.masks[bank],
			first = bank == 2 ? auxiliary_value : op.colors[bank];
		if (copy || !(banks & (1u << bank)) || !mask)
			continue;
		for (unsigned y = 0; y < 4; ++y)
			for (unsigned x = 0; x < clear_width; ++x)
			{
				const unsigned group = x % groups, word = 2 * y + x / groups;
				if (!plane_clear_written(bank, group, word))
					return op;
				const uint32_t value = plane_clear_value(bank, group, word) & mask;
				op.clear_values[bank][y * clear_width + x] = value;
				op.patterned[bank] = op.patterned[bank] || value != (first & mask);
			}
		if (op.patterned[bank] && (!op.allow_pattern ||
			!block_pattern_layout(op.clear_values[bank], clear_width, mask)))
			return op;
		if (seed && (op.patterned[bank] || m_clear_cache[bank].patterned))
			for (unsigned group = 0; group < groups; ++group)
				if (op.pixel_masks[bank][group] != 0xffffffffu)
					return op;
	}
	op.valid = true;
	return op;
}

void CRealImage2100::execute_block(const BlockOperation& op)
{
	if (!op.value)
		return;
	auto invalidate_cache = [&]() {
		for (unsigned bank = 0; bank < 3; ++bank)
			if (op.banks & (1u << bank))
			{
				auto& cache = m_clear_cache[bank];
				const uint32_t cx = (cache.source & 0xffff) * 10 * op.clear_width,
					cy = (cache.source >> 16) * 16;
				if (!op.geometry_known ||
					(op.x < cx + 21 * op.clear_width && op.right > cx &&
						op.y < cy + 36 && op.bottom > cy))
					cache = {};
			}
	};
	auto reject = [&]() {
		invalidate_cache();
		unimplemented_once("REALimage block command/profile (command rejected)",
			BlockCommand, op.value, true);
	};
	if (!op.valid)
	{
		reject();
		return;
	}
	std::array<uint32_t, 3> colors = {op.colors[0], op.colors[1], op.auxiliary_value};
	auto clear_values = op.clear_values;
	auto patterned = op.patterned;
	for (unsigned bank = 0; bank < 3; ++bank)
	{
		const uint32_t mask = bank == 2 ? op.auxiliary_mask : op.color.masks[bank];
		if (!(op.banks & (1u << bank)) || !mask)
			continue;
		if (op.copy)
		{
			const auto& cache = m_clear_cache[bank];
			if (cache.source != op.source || (cache.known & mask) != mask)
			{
				reject();
				return;
			}
			colors[bank] = cache.color;
			patterned[bank] = cache.patterned;
			clear_values[bank] = cache.pattern;
			if (patterned[bank])
			{
				patterned[bank] = false;
				for (unsigned i = 0; i < op.clear_width * 4; ++i)
					patterned[bank] = patterned[bank] || ((cache.pattern[i] ^ cache.color) & mask);
			}
		}
		if (patterned[bank] && (!op.allow_pattern ||
			!block_pattern_layout(clear_values[bank], op.clear_width, mask)))
		{
			reject();
			return;
		}
		if (bank == 2 || op.seed)
			continue;
		const uint32_t rops = op.color.rops[bank];
		for (unsigned pixel = 0; pixel < (patterned[bank] ? op.clear_width * 4 : 1); ++pixel)
		{
			const uint32_t value = patterned[bank] ? clear_values[bank][pixel] : colors[bank];
			for (unsigned shift = 0; shift < 32; shift += 8)
				if (!(op.replace_color[bank] && shift < 24 && !patterned[bank]) &&
					(mask & (0xffu << shift)) && ((rops >> shift) & 15) != 3 &&
					(((rops >> shift) & 15) != 0 || (value & mask & (0xffu << shift))))
				{
					reject();
					return;
				}
		}
	}
	if (op.seed)
	{
		for (unsigned bank = 0; bank < 3; ++bank)
			if (op.banks & (1u << bank))
			{
				const uint32_t mask = bank == 2 ? op.auxiliary_mask : op.color.masks[bank];
				if (!mask)
					continue;
				auto& cache = m_clear_cache[bank];
				if (cache.source != op.seed_key)
					cache = {};
				cache.source = op.seed_key;
				if (cache.patterned || patterned[bank])
				{
					const bool old_patterned = cache.patterned;
					cache.patterned = false;
					for (unsigned i = 0; i < op.clear_width * 4; ++i)
					{
						const uint32_t old = old_patterned ? cache.pattern[i] : cache.color,
							value = patterned[bank] ? clear_values[bank][i] : colors[bank];
						cache.pattern[i] = (old & ~mask) | (value & mask);
						cache.patterned = cache.patterned || cache.pattern[i] != cache.pattern[0];
					}
					cache.color = cache.pattern[0];
					if (!cache.patterned)
						cache.pattern = {};
				}
				else
					cache.color = (cache.color & ~mask) | (colors[bank] & mask);
				cache.known |= mask;
			}
		return;
	}
	invalidate_cache();
	const uint32_t right = std::min(op.right, m_color_width),
		bottom = std::min(op.bottom, m_color_height);
	if (op.x >= right || op.y >= bottom)
		return;
	// Validated block ROPs reduce to masked replacement, including zero-source clears.
	for (unsigned bank = 0; bank < 3; ++bank)
	{
		if (bank == 2 ? !op.auxiliary_clear : !(op.banks & (1u << bank)))
			continue;
		const uint32_t mask = bank == 2 ? op.auxiliary_mask : op.color.masks[bank],
			value = colors[bank];
		if (!mask)
			continue;
		uint32_t* const pixels = bank == 2 ? m_auxiliary.data() :
			m_color.data() + size_t(bank) * m_color_pixels;
		std::array<std::array<uint32_t, 16>, 4> masks{};
		std::array<bool, 4> full{}, empty{};
		for (unsigned y = 0; y < 4; ++y)
		{
			full[y] = empty[y] = true;
			for (unsigned x = 0; x < op.clear_width; ++x)
			{
				const uint32_t bits = (op.pixel_masks[bank][x % op.groups] &
					(1u << (2 * y + x / op.groups))) ? mask : 0;
				masks[y][x] = bits;
				full[y] = full[y] && bits == 0xffffffffu;
				empty[y] = empty[y] && !bits;
			}
		}
		for (uint32_t row = op.y; row < bottom; ++row)
		{
			if (empty[row & 3])
				continue;
			uint32_t* const line = pixels + size_t(row) * m_color_width;
			if (patterned[bank])
			{
				for (uint32_t col = op.x; col < right; ++col)
				{
					const unsigned x = col & (op.clear_width - 1);
					const uint32_t lanes = masks[row & 3][x],
						source = clear_values[bank][(row & 3) * op.clear_width + x];
					line[col] = (line[col] & ~lanes) | (source & lanes);
				}
				continue;
			}
			if (full[row & 3])
			{
				std::fill(line + op.x, line + right, value);
				continue;
			}
			const auto& row_masks = masks[row & 3];
			uint32_t col = op.x;
#if defined(REALIMAGE_SSE2)
			const __m128i values = _mm_set1_epi32(value);
			for (; col + 4 <= right; col += 4)
			{
				const __m128i lanes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(
					row_masks.data() + (col & (op.clear_width - 1))));
				const __m128i old = _mm_loadu_si128(reinterpret_cast<const __m128i*>(line + col));
				_mm_storeu_si128(reinterpret_cast<__m128i*>(line + col),
					_mm_or_si128(_mm_andnot_si128(lanes, old), _mm_and_si128(lanes, values)));
			}
#endif
			for (; col < right; ++col)
			{
				const uint32_t lanes = row_masks[col & (op.clear_width - 1)];
				line[col] = (line[col] & ~lanes) | (value & lanes);
			}
		}
	}
}

void CRealImage2100::block_command(uint32_t v)
{
	if (v == 0x01000032 || v == 0x07000032 || v == 0x01000052 || v == 0x07000052 || v == 0x01000072 ||
		v == 0x07000072 || v == 0x06000072 || v == 0x06000062 || v == 0x07000062 ||
		v == 0x06200062 || v == 0x06200072 || v == 0x07200062 || v == 0x07200072)
		start_command(BlockCommand, v);
	else
		execute_block(prepare_block(v));
}

CRealImage2100::StartPrefix CRealImage2100::begin_start(uint32_t a, uint32_t v)
{
	const bool native_readback = a == BlockCommand && v == 0x07000052;
	const bool readback = ((a == HostCommand || a == BlockCommand) && v == 0x01000052) || native_readback;
	const bool cross_copy = a == HostCommand &&
		(v == 0x01008072 || v == 0x00008062 || v == 0x00008072 || v == 0x01008062);
	const uint32_t selected = (peek(DrawControl) >> 12) & 3;
	const uint32_t destination_banks = cross_copy ?
		((selected >> 1) | (selected << 1)) & 3 : selected;
	if (!readback)
		for (unsigned bank = 0; bank < 2; ++bank)
			if (destination_banks & (1u << bank))
				m_clear_cache[bank] = {};
	if (((a == FillCommand && (v == 0x09040832 || v == 0x010408b2)) || a == BitmapCommand ||
		(a == HostCommand && v == 0x010008f2 && (peek(DrawControl) & 0xf000) == 0x4000) ||
		(a == BlockCommand && (v == 0x07000072 || v == 0x06000072 || v == 0x06000062 || v == 0x07000062))) &&
		(peek(DrawControl) & 0x4000))
		m_clear_cache[2] = {};
	if (m_pending.width)
		report("HOST_INTERRUPTED", a, v, "Incomplete native host upload replaced");
	if (m_readback.width)
		report("READBACK_INTERRUPTED", a, v, "Incomplete native host readback replaced");
	m_pending = {};
	m_readback = {};
	return {a, v, selected, destination_banks};
}

CRealImage2100::StartOperation CRealImage2100::prepare_start(const StartPrefix& prefix) const
{
	const uint32_t a = prefix.address, v = prefix.value;
	StartOperation op;
	op.address = a;
	op.value = v;
	op.color = prepare_color_write();
	const bool native_upload = a == BlockCommand && v == 0x07000032;
	const bool native_readback = a == BlockCommand && v == 0x07000052;
	const bool readback = ((a == HostCommand || a == BlockCommand) && v == 0x01000052) || native_readback;
	const bool cross_copy = a == HostCommand &&
		(v == 0x01008072 || v == 0x00008062 || v == 0x00008072 || v == 0x01008062);
	const uint32_t selected = prefix.selected;
	op.banks = selected;
	op.destination_banks = prefix.destination_banks;
	if (!v)
		return op;
	op.kind = StartOperation::Kind::Rejected;
	const bool block_transfer = a == BlockCommand && (v == 0x01000032 || native_upload || readback);
	const bool block_fast_copy = a == BlockCommand &&
		(v == 0x06200062 || v == 0x06200072 || v == 0x07200062 || v == 0x07200072);
	const bool block_copy = block_fast_copy || (a == BlockCommand &&
		(v == 0x01000072 || v == 0x07000072 || v == 0x06000072 || v == 0x06000062 || v == 0x07000062));
	const bool block_geometry = block_transfer || block_copy;
	const bool upload = ((a == HostCommand || a == BlockCommand) && v == 0x01000032) || native_upload;
	const bool fast_copy = block_fast_copy || (a == HostCommand &&
		(v == 0x00200062 || v == 0x00200072 || v == 0x01200062 || v == 0x01200072));
	const bool copy = (a == HostCommand &&
		(v == 0x01000062 || v == 0x00000062 || v == 0x01000072 || v == 0x00000072)) ||
		fast_copy || cross_copy || block_copy;
	const bool initialization = a == FillCommand &&
		(v == 0x09040832 || v == 0x010408b2);
	const bool fill = a == FillCommand && (v == 0x09000832 || v == 0x09040832);
	const bool repeated_mono = a == FillCommand && (v == 0x01000872 || v == 0x010008f2);
	const bool bitmap = a == BitmapCommand && v == 0x01000872;
	const bool host_mono = a == HostCommand && (v & ~0x7780u) == 0x01000872;
	const bool auxiliary_mono = a == HostCommand && v == 0x010008f2 &&
		(peek(DrawControl) & 0xf000) == 0x4000;
	const bool configuration_mono = a == HostCommand && v == 0x010008f2 &&
		(peek(DrawControl) & 0xff000000u) == 0xa5000000u &&
		(peek(DrawControl) & 0xf000) != 0x4000;
	const bool integer_mono = configuration_mono ||
		(a == HostCommand && (v & ~0x700u) == 0x01000872 &&
			((peek(DrawControl) & 0xe0000000u) == 0xa0000000u ||
				(peek(DrawControl) & ~0xff01u) == 0x21000002u));
	op.transparent = bitmap || ((host_mono || repeated_mono) && !(v & 0x80));
	op.mono_offset = host_mono ? (v >> 8) & 7 : 0;
	// Integer glyphs encode only the source offset; their extent supplies the width.
	const uint32_t mono_width = host_mono && !integer_mono && (v & 0x7700) ? ((v >> 12) & 7) + 1 : 0;
	op.mono = bitmap || host_mono || repeated_mono || (a == FillCommand && v == 0x010408b2);
	op.auxiliary = (initialization || bitmap || block_copy || auxiliary_mono) && (peek(DrawControl) & 0x4000);
	const bool profile = auxiliary_mono ? auxiliary_mono_profile() :
		integer_mono ? integer_rgb_profile(configuration_mono) : bitmap ? bitmap_profile() :
		fast_copy ? fast_copy_profile(block_fast_copy) :
		block_copy ? (v == 0x01000072 ?
			block_transfer_profile(false) : block_copy_profile()) :
		block_transfer ? block_transfer_profile(upload, native_readback || native_upload) :
		((fill || initialization) ? fill_profile(initialization) : copy_profile()) &&
		(!(selected & 1) || plane_profile(0)) && (!(selected & 2) || plane_profile(1)) &&
		(!op.auxiliary || plane_profile(2)) &&
		(!cross_copy || plane_profile(selected == 2 ? 0 : 1));
	if ((!upload && !copy && !fill && !op.mono && !readback) || !profile ||
		((copy || readback || native_upload) && selected != 1 && selected != 2))
		return op;
	// Bit 18 selects host geometry for the miniport's initialization rectangles.
	const bool host_geometry = a == HostCommand || initialization;
	const uint32_t origin = peek(block_geometry ? (readback ? BlockSource : BlockDestination) :
		bitmap ? BitmapOrigin : host_geometry ? HostOrigin : FillOrigin),
		extent = peek(block_geometry ? BlockExtent : bitmap ? BitmapExtent : host_geometry ? HostExtent : FillExtent),
		width = (extent & 0xffff) + 1, height = (extent >> 16) + 1;
	op.width = width;
	op.height = height;
	op.banks = (peek(DrawControl) >> 12) & 3;
	op.x = int16_t(origin & 0xffff);
	op.y = int16_t(origin >> 16);
	if ((block_transfer || integer_mono || auxiliary_mono) && (op.x < 0 || op.y < 0 ||
		uint64_t(op.x) + width > m_color_width || uint64_t(op.y) + height > m_color_height))
		return op;
	if (readback)
	{
		const uint32_t source = peek(BlockSource), sx = source & 0xffff,
			sy = source >> 16;
		if (sx >= m_color_width || sy >= m_color_height ||
			width > m_color_width - sx || height > m_color_height - sy)
		{
			op.rejection = StartOperation::Rejection::ReadbackSource;
			return op;
		}
		op.kind = StartOperation::Kind::Readback;
		op.banks = selected;
		op.sx = int32_t(sx);
		op.sy = int32_t(sy);
		return op;
	}
	if (upload)
	{
		if (uint64_t(width) * height > 0xffffffffu)
		{
			op.rejection = StartOperation::Rejection::HostBounds;
			return op;
		}
		if (block_transfer && (peek(DrawControl) & 0x4000))
		{
			op.auxiliary_compare_mask = 0x8000;
			op.auxiliary_reference = (peek(DrawControl) >> 4) & op.auxiliary_compare_mask;
		}
		op.kind = StartOperation::Kind::Upload;
		return op;
	}
	if (copy)
	{
		const uint32_t source = peek(BlockSource);
		op.source_bank = selected == 2 ? 1 : 0;
		op.destination_bank = cross_copy ? op.source_bank ^ 1u : op.source_bank;
		op.right_to_left = !(v & 0x01000000);
		op.bottom_to_top = !(v & 0x10);
		op.fast_copy = fast_copy;
		if (fast_copy && ((source ^ origin) & (block_width() / 2 - 1)))
		{
			op.rejection = StartOperation::Rejection::CopyAlignment;
			return op;
		}
		op.sx = int16_t(source & 0xffff);
		op.sy = int16_t(source >> 16);
		op.left = op.right_to_left ? op.x - int32_t(width) + 1 : op.x;
		op.right = op.x + (op.right_to_left ? 1 : int32_t(width));
		op.top = op.bottom_to_top ? op.y - int32_t(height) + 1 : op.y;
		op.bottom = op.y + (op.bottom_to_top ? 0 : int32_t(height) - 1);
		if (block_copy && (op.left < 0 || op.top < 0 || op.right > int32_t(m_color_width) ||
			op.bottom >= int32_t(m_color_height)))
			return op;
		op.left = std::max(op.left, int32_t(0));
		op.right = std::min(op.right, int32_t(m_color_width));
		op.top = std::max(op.top, int32_t(0));
		op.bottom = std::min(op.bottom, int32_t(m_color_height) - 1);
		if (op.left >= op.right || op.top > op.bottom)
		{
			op.kind = StartOperation::Kind::Empty;
			return op;
		}
		if (op.sx + op.left - op.x < 0 || op.sx + op.right - op.x > int32_t(m_color_width) ||
			op.sy + op.top - op.y < 0 || op.sy + op.bottom - op.y >= int32_t(m_color_height))
		{
			op.rejection = StartOperation::Rejection::CopySource;
			return op;
		}
		if (op.auxiliary)
			op.auxiliary_mask = plane_value(2, 0, 0xffffffff);
		op.kind = StartOperation::Kind::Copy;
		return op;
	}
	// Bitmap rows start in the higher-addressed pattern word.
	op.pattern = bitmap ? std::array<uint32_t, 4>{0, 0, peek(BitmapPattern0), peek(BitmapPattern1)} :
		std::array<uint32_t, 4>{peek(MonoPattern0), peek(MonoPattern1), peek(MonoPattern2), peek(MonoPattern3)};
	// Native rectangle fills can repeat one word beyond the glyph limits.
	const bool repeated_host = configuration_mono && op.pattern[0] == op.pattern[1] &&
		op.pattern[0] == op.pattern[2] && op.pattern[0] == op.pattern[3];
	// Multirow bitmap layouts are accepted only when the two possible row pitches agree.
	if (op.mono && ((mono_width && width != mono_width) ||
		(a == HostCommand && !auxiliary_mono && !repeated_host &&
			(width + op.mono_offset > 8 || height > 16)) ||
		(bitmap && (width > 8 || (height != 1 &&
			(width != 2 || height != 2 || ((op.pattern[3] ^ (op.pattern[3] >> 6)) & 0x00c00000u))))) ||
		(a == FillCommand && (op.pattern[0] != op.pattern[2] || op.pattern[1] != op.pattern[3]))))
	{
		op.rejection = StartOperation::Rejection::MonoLayout;
		return op;
	}
	op.left = std::max(op.x, int32_t(0));
	op.top = std::max(op.y, int32_t(0));
	op.right = std::min(op.x + int32_t(width), int32_t(m_color_width));
	op.bottom = std::min(op.y + int32_t(height), int32_t(m_color_height));
	op.foreground = peek(bitmap ? BitmapForeground : Foreground);
	op.background = peek(bitmap ? BitmapBackground : Background);
	if (bitmap)
	{
		op.left = std::max(op.left, int32_t(peek(ClipXMin) >> 4));
		op.top = std::max(op.top, int32_t(peek(ClipYMin) >> 4));
		op.right = std::min(op.right, int32_t((peek(ClipXMax) >> 4) + 1));
		op.bottom = std::min(op.bottom, int32_t((peek(ClipYMax) >> 4) + 1));
	}
	if (bitmap || integer_mono || auxiliary_mono)
	{
		op.auxiliary_reference = ((peek(DrawControl) & 0x00f00000u) << 8) |
			((peek(DrawControl) & 0x000f0000u) >> 4);
	}
	// Auxiliary payload and window references occupy separate packed fields.
	if (auxiliary_mono)
		op.foreground = op.background = (op.foreground & 0x0fff0fffu) | op.auxiliary_reference;
	if (integer_mono && (peek(DrawControl) & 0x4000))
		op.auxiliary_compare_mask = 0x8000;
	if (op.auxiliary)
	{
		op.auxiliary_mask = plane_value(2, 0, 0xffffffff);
		op.auxiliary_rop = plane_value(2, 4, 0x03030303);
	}
	op.kind = StartOperation::Kind::Pattern;
	return op;
}

void CRealImage2100::execute_start(const StartOperation& op)
{
	if (op.kind == StartOperation::Kind::Empty)
		return;
	if (op.kind == StartOperation::Kind::Rejected)
	{
		const char* message = "REALimage 2D command/profile (command rejected)";
		switch (op.rejection)
		{
		case StartOperation::Rejection::ReadbackSource:
			message = "REALimage readback source bounds (command rejected)";
			break;
		case StartOperation::Rejection::HostBounds:
			report("HOST_BOUNDS", op.address, op.value, "Native host extent exceeds model limit");
			return;
		case StartOperation::Rejection::CopyAlignment:
			message = "REALimage fast-copy alignment (command rejected)";
			break;
		case StartOperation::Rejection::CopySource:
			message = "REALimage copy source bounds (command rejected)";
			break;
		case StartOperation::Rejection::MonoLayout:
			message = "REALimage monochrome layout (command rejected)";
			break;
		default:
			break;
		}
		unimplemented_once(message, op.address, op.value, true);
		return;
	}
	if (op.kind == StartOperation::Kind::Readback)
	{
		m_readback = {uint32_t(op.sx), uint32_t(op.sy), op.width, op.height, 0, op.banks};
		return;
	}
	if (op.kind == StartOperation::Kind::Upload)
	{
		m_pending = {uint32_t(op.x) & 0xffff, uint32_t(op.y) & 0xffff,
			op.width, op.height, 0, op.banks, op.auxiliary_compare_mask, op.auxiliary_reference};
		return;
	}
	if (op.kind == StartOperation::Kind::Copy)
	{
		const int32_t first = op.right_to_left ? op.right - 1 : op.left,
			end = op.right_to_left ? op.left - 1 : op.right, step = op.right_to_left ? -1 : 1,
			first_row = op.bottom_to_top ? op.bottom : op.top,
			end_row = op.bottom_to_top ? op.top - 1 : op.bottom + 1,
			row_step = op.bottom_to_top ? -1 : 1;
		for (int32_t row = first_row; row != end_row; row += row_step)
			for (int32_t col = first; col != end; col += step)
			{
				const size_t offset = size_t(op.source_bank) * m_color_pixels +
					size_t(op.sy + row - op.y) * m_color_width + size_t(op.sx + col - op.x);
				if (op.fast_copy)
				{
					uint32_t& destination = m_color[size_t(op.destination_bank) * m_color_pixels +
						size_t(row) * m_color_width + col];
					destination = (destination & 0xff000000) | (m_color[offset] & 0xffffff);
				}
				else
					color_write(op.color, uint32_t(col), uint32_t(row), m_color[offset], op.destination_banks);
				if (op.auxiliary)
				{
					const uint32_t source = m_auxiliary[size_t(op.sy + row - op.y) * m_color_width +
						size_t(op.sx + col - op.x)];
					uint32_t& destination = m_auxiliary[size_t(row) * m_color_width + col];
					destination = (destination & ~op.auxiliary_mask) | (source & op.auxiliary_mask);
				}
			}
		return;
	}
	for (int32_t row = op.top; row < op.bottom; ++row)
		for (int32_t col = op.left; col < op.right; ++col)
		{
			uint32_t color = op.foreground;
			if (op.mono)
			{
				const uint32_t px = (uint32_t(col - op.x) + op.mono_offset) & 7,
					py = uint32_t(row - op.y) & 15;
				if (!(op.pattern[3 - py / 4] & (1u << (31 - 8 * (py & 3) - px))))
				{
					if (op.transparent)
						continue;
					color = op.background;
				}
			}
			if (op.auxiliary_compare_mask &&
				((m_auxiliary[size_t(row) * m_color_width + col] ^ op.auxiliary_reference) & op.auxiliary_compare_mask))
				continue;
			color_write(op.color, uint32_t(col), uint32_t(row), color, op.banks);
			if (op.auxiliary)
			{
				uint32_t& destination = m_auxiliary[size_t(row) * m_color_width + col];
				// Auxiliary bitmap tag bits come from DrawControl.
				const uint32_t source = op.address == BitmapCommand ? op.auxiliary_reference : color;
				uint32_t result = 0;
				for (unsigned shift = 0; shift < 32; shift += 8)
				{
					const unsigned rop = (op.auxiliary_rop >> shift) & 15;
					uint32_t channel = 0;
					if (rop & 1) channel |= source & destination;
					if (rop & 2) channel |= source & ~destination;
					if (rop & 4) channel |= ~source & destination;
					if (rop & 8) channel |= ~source & ~destination;
					result |= channel & (0xffu << shift);
				}
				destination = (destination & ~op.auxiliary_mask) | (result & op.auxiliary_mask);
			}
		}
}

void CRealImage2100::start_command(uint32_t a, uint32_t v)
{
	const auto prefix = begin_start(a, v);
	execute_start(prepare_start(prefix));
}

bool CRealImage2100::triangle_profile() const
{
	const uint32_t pipeline = peek(PipelineControl0), depth_control = plane_value(2, 5, 0),
		texture_format = pipeline & 0x0f007000u, texture_mode = pipeline & 0x00038000u;
	const bool textured = (pipeline & 0x80000000u) != 0,
		no_depth = depth_control == 0x0a000200;
	// Disabled texturing retains its format, filtering, wrapping and size fields.
	constexpr uint32_t texture_fields = 0x0f7fffff;
	if ((pipeline & ~(0x80000000u | texture_fields)) ||
		(textured && ((pipeline & ~0x0f03fffcu) != 0x804c0000u ||
			(texture_format != 0x0a002000u && texture_format != 0x09003000u &&
				texture_format != 0x0d002000u && texture_format != 0x0d003000u) ||
			texture_mode > 0x00018000u ||
			((pipeline >> 8) & 15) > 10 || ((pipeline >> 4) & 15) > 10)))
		return false;
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15,
		bank = (banks & 3) == 2 ? 1 : 0,
		global = peek(GlobalControl0),
		width = block_width(), columns = (control >> 8) & 15,
		copy_columns = ((peek(MemoryControl) >> 24) & 63) + 1,
		blend_control = peek(PipelineControl2), color_rop = plane_value(bank, 4, 0);
	const bool additive = color_rop == 0x10101010u || color_rop == 0xb0b0b0b0u,
		blend = additive || color_rop == 0xd0d0d0d0u;
	if (additive)
	{
		if (blend_control != 0x10000000u &&
			(color_rop != 0x10101010u || blend_control != 0x30000000u))
			return false;
	}
	else if (blend ? ((blend_control & ~0x30000080u) || !(blend_control & 0x30000000u)) :
		blend_control != 0x10000000u)
		return false;
	if ((control & ~0x000fff01u) != 0x81800002 || (banks != 5 && banks != 6) ||
		(global != 0x180 && global != 0x190) ||
		!native_pixel_profile() || !width || columns != (copy_columns + 1) / 2 ||
		copy_columns > (m_color_width + 10 * width - 1) / (10 * width) ||
		m_pending.width || m_readback.width)
		return false;
	if (textured && !no_depth && global != 0x190)
		return false;
	if (textured)
	{
		const uint32_t base = peek(TextureBase), texture_width = 1u << ((pipeline >> 8) & 15),
			texture_height = 1u << ((pipeline >> 4) & 15),
			texel_bytes = (texture_format & 0x0f000000u) == 0x0d000000u ? 4 : 2,
			row_bytes = m_texture.size() == MaxTextureSize ? 0x4000 : 0x2000;
		if ((base & (texel_bytes - 1)) || (base & 0x3fff) + texture_width * texel_bytes > row_bytes ||
			uint64_t(base) + (texture_height - 1) * 0x4000 + texture_width * texel_bytes > MaxTextureSize)
			return false;
	}
	const std::pair<uint32_t, uint32_t> profile[] = {
		{GlobalControl1, 0x20800}, {GlobalControl2, 0x33},
		{PipelineControl3, 0},
		{0x008005cc, 0}, {0x008005d4, 0}, {0x008005dc, 0}, {ContextControl, 0}};
	for (const auto& reg : profile)
		if (peek(reg.first) != reg.second)
			return false;
	if ((peek(ClipXMin) | peek(ClipXMax) | peek(ClipYMin) | peek(ClipYMax)) & 0xffff000fu)
		return false;
	// SRC_ALPHA + ONE uses the driver's dedicated plane multiplier.
	if (!plane_profile(bank, 0x100, blend ? color_rop : 0,
		color_rop == 0xb0b0b0b0u ? 0x19090909u : 0) ||
		plane_value(bank, 0, 0) != 0xffffffffu ||
		(!blend && color_rop != 0x03030303u) ||
		plane_value(bank, 10, 0) != 0 ||
		(no_depth ? depth_control != 0x0a000200 :
			(depth_control != 0x0a000205 && depth_control != 0x0a000207)))
		return false;
	const uint32_t depth[] = {no_depth ? 0u : 0x0fff0fffu, 0, 0xf000, 0x0fff0fff,
		0x03030303, depth_control, 0, 0, 0, 0, 0x00ff0000, 0x33300000};
	for (unsigned i = 0; i < std::size(depth); ++i)
		if (plane_value(2, i, 0) != depth[i])
			return false;
	if (plane_value(2, 12, 0x100) != 0x100 || plane_value(2, 13, 0) ||
		plane_value(2, 14, 0) || plane_value(2, 15, 0))
		return false;
	for (unsigned plane : {bank, 2u})
	{
		if (m_planes[plane].unknown_masks)
			return false;
		for (unsigned i = 24; i < 24 + width / 2; ++i)
			if (plane_value(plane, i, 0xffffffff) != 0xffffffffu)
				return false;
	}
	return true;
}

CRealImage2100::TrianglePreparation CRealImage2100::prepare_triangle(
	uint32_t address, uint32_t value, TriangleOperation& operation) const
{
	// These commands retain the slot without launching a primitive.
	if (!value || value == 0x10)
		return TrianglePreparation::NoOp;
	const uint32_t pipeline = peek(PipelineControl0), texture_mode = pipeline & 0x00038000u,
		bank = ((peek(DrawControl) >> 12) & 3) == 2 ? 1 : 0,
		color_rop = plane_value(bank, 4, 0),
		source_blend = color_rop == 0xb0b0b0b0u ? 2 : peek(PipelineControl2) >> 28;
	const bool textured = (pipeline & 0x80000000u) != 0,
		flat = flat_vertex_register(address), no_depth = plane_value(2, 5, 0) == 0x0a000200,
		additive = color_rop == 0x10101010u || color_rop == 0xb0b0b0b0u,
		blend = additive || color_rop == 0xd0d0d0d0u, affine = peek(GlobalControl0) == 0x190,
		texture_alpha = (pipeline & 0x00007000u) == 0x00003000u,
		use_color = !textured || texture_mode == 0x8000 || texture_mode == 0x10000 ||
			(texture_mode == 0 && texture_alpha),
		use_alpha = blend && (!additive || source_blend != 1) &&
			!(textured && texture_alpha && texture_mode == 0x18000),
		use_w = textured && !affine;
	// Untextured perspective draws retain the existing linear depth approximation.
	if (value != 0x13 || !triangle_profile())
	{
		return TrianglePreparation::Rejected;
	}
	using Vertex = TriangleOperation::Vertex;
	Vertex vertex[3]{};
	for (unsigned slot = 0; slot < 3; ++slot)
	{
		double component[7]{};
		for (unsigned i = 0; i < 7; ++i)
		{
			if ((i == 0 && (flat || !use_alpha)) ||
				(i > 0 && i < 4 && (flat || !use_color)) || (i == 6 && no_depth && !use_w))
				continue;
			const auto reg = m_shadow.find(VertexBase + slot * VertexStride + i * 4);
			if (reg == m_shadow.end())
			{
				return TrianglePreparation::Rejected;
			}
			float input;
			static_assert(sizeof(input) == sizeof(reg->second), "IEEE vertex word size");
			std::memcpy(&input, &reg->second, sizeof(input));
			if (!std::isfinite(input) ||
				(i == 0 && (input < 0 || input > 256)) ||
				((i == 4 || i == 5) && (input < -32768 || input >= 32768)) ||
				(i == 6 && (use_w ? input == 0 : (input < 0 || input > 1))))
			{
				return TrianglePreparation::Rejected;
			}
			// Lighting can emit overrange RGB; clamp before interpolation.
			component[i] = i > 0 && i < 4 ? std::clamp(double(input), 0.0, 256.0) : input;
		}
		vertex[slot] = {component[0], component[1], component[2], component[3],
			component[4], component[5], component[6], 0, 0};
		if (textured)
		{
			// Optimized driver packets leave the leading texture word unwritten.
			float coordinates[4];
			for (unsigned i = 1; i < 4; ++i)
			{
				const auto reg = m_shadow.find(VertexTextureBase + slot * VertexStride + i * 4);
				if (reg == m_shadow.end())
				{
					return TrianglePreparation::Rejected;
				}
				std::memcpy(&coordinates[i], &reg->second, sizeof(float));
				if (!std::isfinite(coordinates[i]))
				{
					return TrianglePreparation::Rejected;
				}
			}
			if (coordinates[3] != 0)
			{
				return TrianglePreparation::Rejected;
			}
			vertex[slot].s = coordinates[1];
			vertex[slot].t = coordinates[2];
		}
	}
	// The shared projection scale can be negative; reject a horizon crossing.
	if (use_w && ((vertex[0].z < 0) != (vertex[1].z < 0) ||
		(vertex[0].z < 0) != (vertex[2].z < 0)))
	{
		return TrianglePreparation::Rejected;
	}
	double flat_color[4]{};
	if (flat && (use_color || use_alpha))
	{
		// The launching slot supplies flat color, regardless of winding.
		const uint32_t slot = (address - FlatVertexBase) / VertexStride;
		for (unsigned i = 0; i < 4; ++i)
		{
			if (i == 0 ? !use_alpha : !use_color)
				continue;
			const auto reg = m_shadow.find(VertexBase + slot * VertexStride + i * 4);
			if (reg == m_shadow.end())
			{
				return TrianglePreparation::Rejected;
			}
			float input;
			std::memcpy(&input, &reg->second, sizeof(input));
			if (!std::isfinite(input) || (i == 0 && (input < 0 || input > 256)))
			{
				return TrianglePreparation::Rejected;
			}
			flat_color[i] = i ? std::clamp(double(input), 0.0, textured ? 256.0 : 255.0) : input / 256.0;
		}
	}
	auto edge = [](const Vertex& a, const Vertex& b, double x, double y) {
		return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
	};
	double area = edge(vertex[0], vertex[1], vertex[2].x, vertex[2].y);
	if (!area)
		return TrianglePreparation::NoOp;
	if (area < 0)
	{
		std::swap(vertex[1], vertex[2]);
		area = -area;
	}
	const Vertex &a = vertex[0], &b = vertex[1], &c = vertex[2];
	const int left = std::max(int(peek(ClipXMin) >> 4),
		int(std::ceil(std::min({a.x, b.x, c.x}) - 0.5))),
		top = std::max(int(peek(ClipYMin) >> 4),
			int(std::ceil(std::min({a.y, b.y, c.y}) - 0.5))),
		right = std::min({int(m_color_width) - 1, int(peek(ClipXMax) >> 4),
			int(std::floor(std::max({a.x, b.x, c.x}) - 0.5))}),
		bottom = std::min({int(m_color_height) - 1, int(peek(ClipYMax) >> 4),
			int(std::floor(std::max({a.y, b.y, c.y}) - 0.5))});
	if (left > right || top > bottom)
		return TrianglePreparation::NoOp;
	auto top_left = [](const Vertex& p, const Vertex& q) {
		return q.y < p.y || (q.y == p.y && q.x > p.x);
	};
	const bool include_a = top_left(b, c), include_b = top_left(c, a), include_c = top_left(a, b);
	const uint32_t wid = (peek(DrawControl) >> 4) & 0xf000;
	const bool depth_less = plane_value(2, 5, 0) == 0x0a000207;
	const uint32_t texture_profile = peek(PipelineControl0),
		texture_base = textured ? texture_offset(peek(TextureBase)) : 0,
		texture_width = 1u << ((texture_profile >> 8) & 15),
		texture_height = 1u << ((texture_profile >> 4) & 15);
	const unsigned texture_row_shift = m_texture.size() == MaxTextureSize ? 14 : 13;
	operation = {};
	std::copy(std::begin(vertex), std::end(vertex), std::begin(operation.vertex));
	std::copy(std::begin(flat_color), std::end(flat_color), std::begin(operation.flat_color));
	operation.area = area;
	operation.left = left;
	operation.top = top;
	operation.right = right;
	operation.bottom = bottom;
	operation.textured = textured;
	operation.flat = flat;
	operation.blend = blend;
	operation.blend_control = peek(PipelineControl2);
	operation.no_depth = no_depth;
	operation.affine = affine;
	operation.depth_less = depth_less;
	operation.wid = wid;
	operation.bank = bank;
	operation.color_width = m_color_width;
	operation.color_rop = color_rop;
	operation.color_offset = size_t(bank) * m_color_pixels;
	operation.texture_base = texture_base;
	operation.texture_width = texture_width;
	operation.texture_height = texture_height;
	operation.texture_row_shift = texture_row_shift;
	operation.texture_alpha = texture_alpha;
	operation.texture_format = pipeline & 0x0f007000u;
	operation.texture_mode = texture_mode;
	operation.texture_environment = peek(PipelineControl5);
	operation.texture_clamp = pipeline & 0x0c;
	operation.texture_border = peek(PipelineControl1);
	operation.row_edges[0] = {1, 2, c.y == b.y ? 0 : (c.x - b.x) / (c.y - b.y), include_a};
	operation.row_edges[1] = {2, 0, a.y == c.y ? 0 : (a.x - c.x) / (a.y - c.y), include_b};
	operation.row_edges[2] = {0, 1, b.y == a.y ? 0 : (b.x - a.x) / (b.y - a.y), include_c};
	return TrianglePreparation::Ready;
}

void CRealImage2100::execute_line(const LineOperation& operation)
{
	LineOperation::Point a = operation.a, b = operation.b;
	const int ax = int(a.x / 16), ay = int(a.y / 16);
	const int bx = int(b.x / 16), by = int(b.y / 16);
	const int dx = bx - ax, dy = by - ay;
	const int adx = std::abs(dx), ady = std::abs(dy);
	const int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
	const bool x_major = adx >= ady;
	const int major = x_major ? adx : ady;
	const int minor = x_major ? ady : adx;

	// Exclusive lines omit the final endpoint. A zero-length exclusive
	// segment draws nothing, while inclusive still plots the single pixel
	if (!operation.inclusive && major == 0 && minor == 0)
		return;
	const int steps = major + int(operation.inclusive);

	// Stipple phase is anchored on the major axis in pixel space, matching
	// the axial path the drivers already exercise. Diagonal walks advance
	// one major step per iteration, so the same phase formula applies
	const int64_t major_start = x_major ? a.x / 16 : a.y / 16;
	const int64_t major_end = x_major ? b.x / 16 : b.y / 16;
	const unsigned phase = unsigned(major_end < major_start ?
		int64_t(operation.command >> 20) - major_start :
		major_start - int64_t(operation.command >> 20)) & 0xfff;
	const unsigned stipple_scale = (operation.stipple >> 27) & 7;
	auto stipple_visible = [&](unsigned step) {
		if (!operation.patterned)
			return true;
		return ((operation.stipple >>
			(((phase + step) >> stipple_scale) & 15)) & 1) != 0;
	};

	const int left = std::max(0, int(peek(ClipXMin) >> 4));
	const int top = std::max(0, int(peek(ClipYMin) >> 4));
	const int right = std::min(int(m_color_width) - 1, int(peek(ClipXMax) >> 4));
	const int bottom = std::min(int(m_color_height) - 1, int(peek(ClipYMax) >> 4));
	if (left > right || top > bottom)
		return;

	auto invalidate = [&]() {
		for (unsigned bank = 0; bank < 3; ++bank)
			if ((operation.banks & (1u << bank)) &&
				(bank < 2 || !operation.auxiliary_compare))
				m_clear_cache[bank] = {};
	};

	auto write_pixel = [&](int x, int y) {
		const size_t offset = size_t(y) * m_color_width + unsigned(x);
		if (operation.auxiliary_compare &&
			((m_auxiliary[offset] ^ operation.auxiliary) & operation.compare_mask))
			return;
		if (operation.apply_rgb_rop)
			color_write(operation.rgb, unsigned(x), unsigned(y),
				operation.color, operation.banks);
		else if (operation.color_write)
			for (unsigned bank = 0; bank < 2; ++bank)
				if (operation.banks & (1u << bank))
				{
					uint32_t& pixel =
						m_color[size_t(bank) * m_color_pixels + offset];
					pixel = (pixel & ~operation.masks[bank]) |
						(operation.color & operation.masks[bank]);
				}
		if (operation.banks & 4)
			m_auxiliary[offset] =
				(m_auxiliary[offset] & ~operation.masks[2]) |
				(operation.auxiliary & operation.masks[2]);
	};

	int x = ax, y = ay;
	int error = 2 * minor - major;
	bool drawn = false;
	for (int i = 0; i < steps; ++i)
	{
		if (stipple_visible(unsigned(i)) &&
			x >= left && x <= right && y >= top && y <= bottom)
		{
			if (!drawn)
				invalidate();
			drawn = true;
			write_pixel(x, y);
		}
		// Midpoint ties keep the axial step
		if (error > 0)
		{
			if (x_major)
				y += sy;
			else
				x += sx;
			error -= 2 * major;
		}
		error += 2 * minor;
		if (x_major)
			x += sx;
		else
			y += sy;
	}
}

CRealImage2100::LinePreparation CRealImage2100::prepare_line(
	uint32_t address, uint32_t value, LineOperation& operation) const
{
	operation = {};
	const bool floating = flat_vertex_register(address);
	// Rotating slot encodings share the same primitive family:
	// 0x22/0x26/0x2a and 0x62/0x66/0x6a (bits 2..3 select the vertex pair)
	const uint32_t primitive = floating ? (value & ~0xcu) : (value & 0x000fffffu & ~0xcu);
	const bool inclusive = primitive == 0x62;
	const bool line = primitive == 0x22 || inclusive;
	if (!line)
		return LinePreparation::Rejected;

	const unsigned first_vertex = (value >> 2) & 3;
	const uint32_t stipple = peek(PipelineControl3);
	const bool patterned = !floating && (stipple & 0x80000000u);

	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15,
		width = block_width(), columns = ((peek(MemoryControl) >> 24) & 63) + 1,
		global = peek(GlobalControl0);
	const bool direct_rgb = (control & ~0xff01u) == 0x21000002u &&
		(banks == 1 || banks == 2);
	const bool native_line_rgb = !floating &&
		(control & ~0x00ffff01u) == 0xa1000002u && (banks == 1 || banks == 2);
	const bool auxiliary_compare = (control & ~0x00ffff01u) == 0xa1000002u &&
		(banks == 5 || banks == 6) && plane_value(2, 0, 0xffffffff) == 0;
	const bool auxiliary_write =
		(control & ~0x00ef0f01u) == 0xa1004002u &&
		!(plane_value(2, 0, 0xffffffff) & ~0xe000f000u);
	const bool color_write = direct_rgb || native_line_rgb || auxiliary_compare ||
		((control & ~0xff01u) == 0xa1000002u && !(banks & ~3u));

	// Command shape
	if (first_vertex >= 3)
		return LinePreparation::Rejected;
	if (address != (floating ? VertexBase + 0x100 : IntegerVertexColorBase) +
		((first_vertex + 1) % 3) * VertexStride + 0x1c)
		return LinePreparation::Rejected;
	// Non-stippled integer lines shall not carry a phase field in the high bits
	if (!patterned && !floating && (value & 0xfff00000u))
		return LinePreparation::Rejected;
	// Stipple control: enable bit set, scale/pattern in the documented field
	if (patterned && ((stipple & 0x47ff0000u) || !color_write))
		return LinePreparation::Rejected;
	if (inclusive && !floating &&
		!auxiliary_compare && !direct_rgb && !native_line_rgb)
		return LinePreparation::Rejected;
	if (!inclusive && !auxiliary_write && !color_write)
		return LinePreparation::Rejected;
	if (!banks || (banks & ~7u))
		return LinePreparation::Rejected;
	if (!width || !native_pixel_profile() ||
		((control >> 8) & 15) != (columns + 1) / 2 ||
		columns > (m_color_width + 10 * width - 1) / (10 * width))
		return LinePreparation::Rejected;
	if (m_pending.width || m_readback.width)
		return LinePreparation::Rejected;

	// Pipeline profile retained by the NT / OpenVMS drivers for 2D lines
	const std::pair<uint32_t, uint32_t> profile[] = {
		{GlobalControl0, inclusive && global == 3 ? 3u : 1u},
		{GlobalControl1, 0x20810}, {GlobalControl2, 0x33},
		{PipelineControl0, 0}, {PipelineControl1, 0},
		{PipelineControl2, 0x10000000},
		{PipelineControl3, patterned ? stipple : 0u},
		{PipelineControl4, 0}, {PipelineControl5, 0},
		{0x008005cc, 0}, {0x008005d4, 0}, {0x008005dc, 0}};
	for (const auto& reg : profile)
		if (peek(reg.first) != reg.second)
			return LinePreparation::Rejected;

	if ((peek(ClipXMin) | peek(ClipYMin) | peek(ClipXMax) | peek(ClipYMax)) & 0xffff000fu)
		return LinePreparation::Rejected;

	// Plane / ROP validation
	bool uses_color = !floating;
	if (floating)
		for (unsigned bank = 0; bank < 2; ++bank)
			if (banks & (1u << bank))
				for (unsigned shift = 0; shift < 24; shift += 8)
				{
					const unsigned rop = (plane_value(bank, 4, 0) >> shift) & 15;
					if ((plane_value(bank, 0, 0xffffffff) & (0xffu << shift)) &&
						((rop ^ (rop >> 2)) & 3))
						uses_color = true;
				}

	uint32_t color = 0;
	if (uses_color && !auxiliary_write)
		for (uint32_t i = 0; i < 4; ++i)
		{
			const uint32_t component = peek(IntegerVertexColorBase + i * 4);
			if (m_shadow.find(IntegerVertexColorBase + i * 4) == m_shadow.end() ||
				(color_write ? (component & ~0x3fcu) != 0 : component != 0))
				return LinePreparation::Rejected;
			if (i)
				color |= (component >> 2) << ((3 - i) * 8);
		}

	for (unsigned bank = 0; bank < 3; ++bank)
	{
		if (!(banks & (1u << bank)))
			continue;
		const uint32_t rops = plane_value(bank, 4, 0),
			mask = plane_value(bank, 0, 0xffffffff);
		bool valid_rop = rops == 0x03030303u;
		if (color_write && bank < 2)
			valid_rop = !(mask & 0xff000000u) || (rops >> 24) == 5;
		if (m_planes[bank].unknown_masks || !valid_rop ||
			(bank < 2 && !plane_profile(bank)))
			return LinePreparation::Rejected;
		if (bank == 2)
		{
			const uint32_t expected[] = {
				0, auxiliary_compare ? 0x8000u : 0u,
				0x0fff0fff, 0x03030303,
				auxiliary_compare ? 0x0a000200u : 0x0a000000u,
				0, 0, 0, 0, 0x00ff0000, 0x33300000, 0x100, 0, 0x100, 0};
			for (unsigned i = 0; i < std::size(expected); ++i)
				if (plane_value(bank, i + 1, expected[i]) != expected[i])
					return LinePreparation::Rejected;
		}
	}

	// Vertex fetch, both domains normalize to pixel*16 fixed point
	LineOperation::Point vertex[2]{};
	for (unsigned slot = 0; slot < 2; ++slot)
	{
		const unsigned index = (first_vertex + slot) % 3;
		const uint32_t base = (floating ? VertexBase : IntegerVertexColorBase) +
			index * VertexStride + 0x10;
		for (unsigned i = 0; i < 3; ++i)
			if (m_shadow.find(base + i * 4) == m_shadow.end())
				return LinePreparation::Rejected;
		if (floating)
		{
			float component[3];
			for (unsigned i = 0; i < 3; ++i)
			{
				const uint32_t word = peek(base + i * 4);
				std::memcpy(&component[i], &word, sizeof(word));
			}
			if (!std::isfinite(component[0]) || !std::isfinite(component[1]) ||
				component[0] < -32768 || component[0] >= 32768 ||
				component[1] < -32768 || component[1] >= 32768 ||
				component[0] != std::trunc(component[0]) ||
				component[1] != std::trunc(component[1]) || component[2] != 0)
				return LinePreparation::Rejected;
			vertex[slot] = {int64_t(component[0]) * 16, int64_t(component[1]) * 16};
		}
		else
		{
			vertex[slot] = {int32_t(peek(base)), int32_t(peek(base + 4))};
			if (vertex[slot].x < -524288 || vertex[slot].x >= 524288 ||
				vertex[slot].y < -524288 || vertex[slot].y >= 524288 ||
				peek(base + 8))
				return LinePreparation::Rejected;
		}
	}

	// Hardware walks pixel centers, sub-pixel sources are not in the
	// decoded 2D driver profiles
	if ((vertex[0].x | vertex[0].y | vertex[1].x | vertex[1].y) & 15)
		return LinePreparation::Rejected;

	const ColorWriteContext rgb = prepare_color_write();
	operation.a = vertex[0];
	operation.b = vertex[1];
	operation.rgb = rgb;
	operation.color = color;
	operation.banks = banks;
	operation.masks[0] = rgb.masks[0] & 0xffffff;
	operation.masks[1] = rgb.masks[1] & 0xffffff;
	operation.masks[2] = plane_value(2, 0, 0xffffffff);
	operation.auxiliary = (auxiliary_write || auxiliary_compare) ?
		((control & 0x00f00000u) << 8) | ((control & 0x000f0000u) >> 4) : 0;
	operation.compare_mask = 0x8000u;
	operation.stipple = stipple;
	operation.command = value;
	operation.inclusive = inclusive;
	operation.patterned = patterned;
	operation.apply_rgb_rop = color_write &&
		(((banks & 1) && (rgb.rops[0] & 0xffffff) != 0x030303) ||
		 ((banks & 2) && (rgb.rops[1] & 0xffffff) != 0x030303));
	operation.auxiliary_compare = auxiliary_compare;
	operation.color_write = color_write;
	return LinePreparation::Ready;
}

void CRealImage2100::raster_vertex_command(uint32_t address, uint32_t value)
{
	if (!value)
		return;

	const bool floating = flat_vertex_register(address);
	// Rotating slot encodings (0x26/0x2a, 0x66/0x6a) share the same primitive
	// family as 0x22 / 0x62. Bits 2..3 only select the vertex pair
	const uint32_t primitive = floating ? (value & ~0xcu) : (value & 0x000fffffu & ~0xcu);
	const bool inclusive = primitive == 0x62;
	const bool line = primitive == 0x22 || inclusive;

	if (line)
	{
		LineOperation operation;
		const LinePreparation result = prepare_line(address, value, operation);
		if (result == LinePreparation::Rejected)
			unimplemented_once(
				floating ? "REALimage floating line command/profile (command rejected)" :
					"REALimage integer line command/profile (command rejected)",
				address, value, true);
		else if (result == LinePreparation::Ready)
			execute_line(operation);
		return;
	}

	// Integer triangle path continues below (value == 3)
	auto reject = [&]() {
		unimplemented_once(
			"REALimage integer triangle command/profile (command rejected)",
			address, value, true);
	};
	if (value != 3)
	{
		reject();
		return;
	}

	const unsigned first_vertex = (value >> 2) & 3;
	const uint32_t control = peek(DrawControl), banks = (control >> 12) & 15,
		width = block_width(), columns = ((peek(MemoryControl) >> 24) & 63) + 1,
		global = peek(GlobalControl0);
	const bool direct_rgb = (control & ~0xff01u) == 0x21000002u &&
		(banks == 1 || banks == 2),
		auxiliary_compare = (control & ~0x00ffff01u) == 0xa1000002u &&
		(banks == 5 || banks == 6) && plane_value(2, 0, 0xffffffff) == 0,
		auxiliary_write = (control & ~0x00ff0f01u) == 0xa1004002u &&
		!(plane_value(2, 0, 0xffffffff) & ~0xf000f000u),
		color_write = direct_rgb || auxiliary_compare ||
		((control & ~0xff01u) == 0xa1000002u && !(banks & ~3u));
	if ((!auxiliary_write && !color_write &&
		(control & ~0xff01u) != 0x21000002u) ||
		!banks || (banks & ~7u) ||
		!width || !native_pixel_profile() || ((control >> 8) & 15) != (columns + 1) / 2 ||
		columns > (m_color_width + 10 * width - 1) / (10 * width) ||
		m_pending.width || m_readback.width)
	{
		reject();
		return;
	}
	const bool direct_polygon = !line && direct_rgb && global == 1;
	// Connected terminal commands can retain mode 1 until the next state flush.
	const std::pair<uint32_t, uint32_t> profile[] = {
		{GlobalControl0, line ? (inclusive && global == 3 ? 3u : 1u) : direct_polygon ? 1u : 0u},
		{GlobalControl1, 0x20810}, {GlobalControl2, 0x33},
		{PipelineControl0, 0}, {PipelineControl1, 0}, {PipelineControl2, 0x10000000},
		{PipelineControl3, patterned ? stipple : 0u}, {PipelineControl4, 0}, {PipelineControl5, 0},
		{0x008005cc, 0}, {0x008005d4, 0}, {0x008005dc, 0}};
	for (const auto& reg : profile)
		if (peek(reg.first) != reg.second)
		{
			reject();
			return;
		}
	bool uses_color = !floating;
	if (floating)
		for (unsigned bank = 0; bank < 2; ++bank)
			if (banks & (1u << bank))
				for (unsigned shift = 0; shift < 24; shift += 8)
				{
					const unsigned rop = (plane_value(bank, 4, 0) >> shift) & 15;
					if ((plane_value(bank, 0, 0xffffffff) & (0xffu << shift)) &&
						((rop ^ (rop >> 2)) & 3))
						uses_color = true;
				}
	uint32_t color = 0;
	// Floating 2D lines share integer ARGB; source-independent ROPs need no color.
	if (uses_color && !auxiliary_write)
		for (uint32_t i = 0; i < 4; ++i)
		{
			const uint32_t component = peek(IntegerVertexColorBase + i * 4);
			if (m_shadow.find(IntegerVertexColorBase + i * 4) == m_shadow.end() ||
				(color_write ? (component & ~0x3fcu) != 0 : component != 0))
			{
				reject();
				return;
			}
			if (i)
				color |= (component >> 2) << ((3 - i) * 8);
		}
	if ((peek(ClipXMin) | peek(ClipYMin) | peek(ClipXMax) | peek(ClipYMax)) & 0xffff000fu)
	{
		reject();
		return;
	}
	for (unsigned bank = 0; bank < 3; ++bank)
	{
		if (!(banks & (1u << bank)))
			continue;
		const uint32_t rops = plane_value(bank, 4, 0), mask = plane_value(bank, 0, 0xffffffff);
		bool valid_rop = rops == 0x03030303u;
		// Preserve the existing all-zero initialization triangle profile.
		const bool zero_clear = !line && direct_rgb && !color && !peek(IntegerVertexColorBase) && valid_rop;
		if (color_write && bank < 2 && !zero_clear)
			valid_rop = !(mask & 0xff000000u) || (rops >> 24) == 5;
		if (m_planes[bank].unknown_masks || !valid_rop ||
			(bank < 2 && !plane_profile(bank)))
		{
			reject();
			return;
		}
		if (bank == 2)
		{
			const uint32_t expected[] = {0, auxiliary_compare ? (line ? 0x8000u : 0x10000000u) : 0u,
				0x0fff0fff, 0x03030303, auxiliary_compare ? 0x0a000200u : 0x0a000000u,
				0, 0, 0, 0, 0x00ff0000, 0x33300000, 0x100, 0, 0x100, 0};
			for (unsigned i = 0; i < std::size(expected); ++i)
				if (plane_value(bank, i + 1, expected[i]) != expected[i])
				{
					reject();
					return;
				}
		}
	}
	struct Point { int64_t x, y; } vertex[3];
	for (unsigned slot = 0; slot < (line ? 2u : 3u); ++slot)
	{
		const unsigned index = line ? (first_vertex + slot) % 3 : slot;
		const uint32_t base = (floating ? VertexBase : IntegerVertexColorBase) +
			index * VertexStride + 0x10;
		for (unsigned i = 0; i < 3; ++i)
			if (m_shadow.find(base + i * 4) == m_shadow.end())
			{
				reject();
				return;
			}
		if (floating)
		{
			float component[3];
			for (unsigned i = 0; i < 3; ++i)
			{
				const uint32_t word = peek(base + i * 4);
				std::memcpy(&component[i], &word, sizeof(word));
			}
			if (!std::isfinite(component[0]) || !std::isfinite(component[1]) ||
				component[0] < -32768 || component[0] >= 32768 ||
				component[1] < -32768 || component[1] >= 32768 ||
				component[0] != std::trunc(component[0]) ||
				component[1] != std::trunc(component[1]) || component[2] != 0)
			{
				reject();
				return;
			}
			vertex[slot] = {int64_t(component[0]) * 16, int64_t(component[1]) * 16};
		}
		else
		{
			vertex[slot] = {int32_t(peek(base)), int32_t(peek(base + 4))};
			if (vertex[slot].x < -524288 || vertex[slot].x >= 524288 ||
				vertex[slot].y < -524288 || vertex[slot].y >= 524288 || peek(base + 8))
			{
				reject();
				return;
			}
		}
	}
	const ColorWriteContext rgb = prepare_color_write();
	const bool apply_rgb_rop = color_write && (line ||
		((banks & 1) && (rgb.rops[0] & 0xffffff) != 0x030303) ||
		((banks & 2) && (rgb.rops[1] & 0xffffff) != 0x030303));
	const uint32_t masks[] = {rgb.masks[0] & 0xffffff,
		rgb.masks[1] & 0xffffff, plane_value(2, 0, 0xffffffff)};
	const uint32_t auxiliary = (auxiliary_write || auxiliary_compare) ?
		((control & 0x00f00000u) << 8) | ((control & 0x000f0000u) >> 4) : 0,
		compare_mask = line ? 0x8000u : 0x10000000u;
	auto invalidate = [&]() {
		for (unsigned bank = 0; bank < 3; ++bank)
			if ((banks & (1u << bank)) && (bank < 2 || !auxiliary_compare))
				m_clear_cache[bank] = {};
	};
	auto write_pixel = [&](int x, int y) {
		const size_t offset = size_t(y) * m_color_width + unsigned(x);
		if (auxiliary_compare && ((m_auxiliary[offset] ^ auxiliary) & compare_mask))
			return;
		if (apply_rgb_rop)
			this->color_write(rgb, unsigned(x), unsigned(y), color, banks);
		else
			for (unsigned bank = 0; bank < 2; ++bank)
				if (banks & (1u << bank))
				{
					uint32_t& pixel = m_color[size_t(bank) * m_color_pixels + offset];
					pixel = (pixel & ~masks[bank]) | (color & masks[bank]);
				}
		if (banks & 4)
			m_auxiliary[offset] = (m_auxiliary[offset] & ~masks[2]) | (auxiliary & masks[2]);
	};
	if (line)
	{
		const Point& a = vertex[0];
		Point b = vertex[1];
		if (((a.x | a.y | b.x | b.y) & 15) ||
			((floating || patterned) && a.x != b.x && a.y != b.y))
		{
			reject();
			return;
		}
		const bool stipple_x_major = std::abs(b.x - a.x) >= std::abs(b.y - a.y);
		const int64_t major_start = (stipple_x_major ? a.x : a.y) / 16,
			major_end = (stipple_x_major ? b.x : b.y) / 16;
		// The 12-bit anchor preserves the repeated, LSB-first pattern in either direction.
		const unsigned phase = unsigned(major_end < major_start ?
			int64_t(value >> 20) - major_start : major_start - int64_t(value >> 20)) & 0xfff;
		auto stipple_visible = [&](unsigned step) {
			return !patterned || ((stipple >> (((phase + step) >> ((stipple >> 27) & 7)) & 15)) & 1);
		};
		if (a.x != b.x && a.y != b.y)
		{
			int x = int(a.x / 16), y = int(a.y / 16);
			const int dx = int((b.x - a.x) / 16), dy = int((b.y - a.y) / 16),
				adx = std::abs(dx), ady = std::abs(dy), sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
			const bool x_major = adx >= ady;
			const int major = x_major ? adx : ady, minor = x_major ? ady : adx,
				left = std::max(0, int(peek(ClipXMin) >> 4)),
				top = std::max(0, int(peek(ClipYMin) >> 4)),
				right = std::min(int(m_color_width) - 1, int(peek(ClipXMax) >> 4)),
				bottom = std::min(int(m_color_height) - 1, int(peek(ClipYMax) >> 4));
			if (left > right || top > bottom)
				return;
			int error = 2 * minor - major;
			bool drawn = false;
			for (int i = 0; i < major + int(inclusive); ++i)
			{
				if (x >= left && x <= right && y >= top && y <= bottom)
				{
					if (!drawn)
						invalidate();
					drawn = true;
					write_pixel(x, y);
				}
				// Midpoint ties keep the axial step, matching RV200's convention.
				if (error > 0)
				{
					if (x_major) y += sy;
					else x += sx;
					error -= 2 * major;
				}
				error += 2 * minor;
				if (x_major) x += sx;
				else y += sy;
			}
			return;
		}
		// Bit 6 includes the final endpoint for any rotating line slot.
		if (!inclusive)
		{
			if (a.x == b.x && a.y == b.y)
				return;
			b.x += a.x > b.x ? 16 : a.x < b.x ? -16 : 0;
			b.y += a.y > b.y ? 16 : a.y < b.y ? -16 : 0;
		}
		const int left = std::max({0, int(peek(ClipXMin) >> 4), int(std::min(a.x, b.x) / 16)}),
			top = std::max({0, int(peek(ClipYMin) >> 4), int(std::min(a.y, b.y) / 16)}),
			right = std::min({int(m_color_width) - 1, int(peek(ClipXMax) >> 4), int(std::max(a.x, b.x) / 16)}),
			bottom = std::min({int(m_color_height) - 1, int(peek(ClipYMax) >> 4), int(std::max(a.y, b.y) / 16)});
		if (left > right || top > bottom)
			return;
		bool drawn = false;
		for (int y = top; y <= bottom; ++y)
			for (int x = left; x <= right; ++x)
			{
				if (!stipple_visible(unsigned(std::abs(int64_t(stipple_x_major ? x : y) - major_start))))
					continue;
				if (!drawn)
					invalidate();
				drawn = true;
				write_pixel(x, y);
			}
		return;
	}
	auto edge = [](const Point& p, const Point& q, const Point& at) {
		return (q.x - p.x) * (at.y - p.y) - (q.y - p.y) * (at.x - p.x);
	};
	const int64_t area = edge(vertex[0], vertex[1], vertex[2]);
	if (!area)
		return;
	if (area < 0)
		std::swap(vertex[1], vertex[2]);
	const Point &a = vertex[0], &b = vertex[1], &c = vertex[2];
	const int left = std::max(int(peek(ClipXMin) >> 4),
		int(std::ceil(double(std::min({a.x, b.x, c.x}) - 8) / 16))),
		top = std::max(int(peek(ClipYMin) >> 4),
			int(std::ceil(double(std::min({a.y, b.y, c.y}) - 8) / 16))),
		right = std::min({int(m_color_width) - 1, int(peek(ClipXMax) >> 4),
			int(std::floor(double(std::max({a.x, b.x, c.x}) - 8) / 16))}),
		bottom = std::min({int(m_color_height) - 1, int(peek(ClipYMax) >> 4),
			int(std::floor(double(std::max({a.y, b.y, c.y}) - 8) / 16))});
	auto top_left = [](const Point& p, const Point& q) {
		return q.y < p.y || (q.y == p.y && q.x > p.x);
	};
	const bool include[] = {top_left(b, c), top_left(c, a), top_left(a, b)};
	invalidate();
	for (int y = top; y <= bottom; ++y)
		for (int x = left; x <= right; ++x)
		{
			const Point at{int64_t(x) * 16 + 8, int64_t(y) * 16 + 8};
			const int64_t edges[] = {edge(b, c, at), edge(c, a, at), edge(a, b, at)};
			if (edges[0] < 0 || (!edges[0] && !include[0]) ||
				edges[1] < 0 || (!edges[1] && !include[1]) ||
				edges[2] < 0 || (!edges[2] && !include[2]))
				continue;
			write_pixel(x, y);
		}
}

void CRealImage2100::triangle_command(uint32_t address, uint32_t value)
{
	TriangleOperation operation;
	const TrianglePreparation result = prepare_triangle(address, value, operation);
	if (result == TrianglePreparation::Rejected)
		unimplemented_once("REALimage triangle command/profile (command rejected)",
			address, value, true);
	else if (result == TrianglePreparation::Ready)
		execute_triangle(operation);
}

void CRealImage2100::execute_triangle(const TriangleOperation& operation)
{
	using Vertex = TriangleOperation::Vertex;
	const Vertex* const vertex = operation.vertex;
	const Vertex &a = vertex[0], &b = vertex[1], &c = vertex[2];
	const double area = operation.area;
	const double* const flat_color = operation.flat_color;
	const int left = operation.left, top = operation.top,
		right = operation.right, bottom = operation.bottom;
	const bool textured = operation.textured, flat = operation.flat,
		blend = operation.blend, no_depth = operation.no_depth, depth_less = operation.depth_less,
		affine = operation.affine, texture_alpha = operation.texture_alpha,
		simple_texture = textured && operation.texture_format == 0x0a002000u && operation.texture_mode == 0 &&
			!affine && !blend && no_depth && !operation.texture_clamp,
		include_a = operation.row_edges[0].inclusive,
		include_b = operation.row_edges[1].inclusive,
		include_c = operation.row_edges[2].inclusive;
	const uint32_t wid = operation.wid, bank = operation.bank,
		texture_base = operation.texture_base, texture_width = operation.texture_width,
		texture_height = operation.texture_height;
	const unsigned texture_row_shift = operation.texture_row_shift;
	const unsigned source_blend = operation.color_rop == 0xb0b0b0b0u ? 2 : operation.blend_control >> 28;
	const bool additive = operation.color_rop == 0x10101010u || operation.color_rop == 0xb0b0b0b0u,
		inverse_destination = (operation.blend_control & 0x80) != 0;
	auto blend_channel = [&](double source, double alpha, unsigned destination) {
		const double sf = source_blend == 1 ? 1.0 : source_blend == 2 ? alpha : 1 - alpha,
			df = additive ? 1.0 : inverse_destination ? 1 - alpha : alpha;
		return std::clamp(source * sf + destination * df, 0.0, 255.0);
	};
	auto edge = [](const Vertex& p, const Vertex& q, double x, double y) {
		return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
	};
	// Validated triangle profiles write every RGB channel.
	uint32_t* const color_plane = m_color.data() + operation.color_offset;
	m_clear_cache[bank] = {};
#if defined(__AVX2__) && defined(REALIMAGE_SSE2)
	// Pack wide-stride textures once per large draw to improve sampling locality.
	const uint8_t* sample_texture = m_texture.data();
	uint32_t sample_base = texture_base;
	unsigned sample_row_shift = texture_row_shift;
	std::vector<uint8_t> dense_texture;
	if (simple_texture && (texture_width != 1 || texture_height != 1) &&
		(_mm_getcsr() & _MM_ROUND_MASK) == _MM_ROUND_NEAREST &&
		uint64_t(right - left + 1) * unsigned(bottom - top + 1) >= uint64_t(texture_width) * texture_height * 4)
	{
		const unsigned row_bytes = texture_width * 2;
		dense_texture.resize(size_t(row_bytes) * texture_height);
		for (unsigned y = 0; y < texture_height; ++y)
			std::memcpy(dense_texture.data() + size_t(y) * row_bytes,
				m_texture.data() + texture_base + (size_t(y) << texture_row_shift), row_bytes);
		sample_texture = dense_texture.data();
		sample_base = 0;
		sample_row_shift = 0;
		while ((1u << sample_row_shift) < row_bytes)
			++sample_row_shift;
	}
#endif
	// Skip only spans rejected by the original edge test.
	auto row_span = [&](int y, int& span_left, int& span_right) {
		span_left = left;
		span_right = right;
		for (const auto& limit : operation.row_edges)
		{
			if (span_left > span_right)
				break;
			const Vertex &p = vertex[limit.p], &q = vertex[limit.q];
			auto outside = [&](int x) {
				const double e = edge(p, q, x + 0.5, y + 0.5);
				return e < 0 || (e == 0 && !limit.inclusive);
			};
			if (q.y == p.y)
			{
				if (outside(span_left))
					span_left = span_right + 1;
				continue;
			}
			const double crossing = p.x + (y + 0.5 - p.y) * limit.slope - 0.5;
			if (!std::isfinite(crossing))
				continue;
			if (q.y < p.y)
			{
				const int bound = int(std::clamp(std::floor(crossing) - 1,
					double(span_left), double(span_right + 1)));
				if (bound > span_left && outside(bound - 1))
					span_left = bound;
			}
			else
			{
				const int bound = int(std::clamp(std::ceil(crossing) + 1,
					double(span_left - 1), double(span_right)));
				if (bound < span_right && outside(bound + 1))
					span_right = bound;
			}
		}
	};

#if defined(__AVX2__) && defined(REALIMAGE_SSE2)
	if (simple_texture && (_mm_getcsr() & _MM_ROUND_MASK) == _MM_ROUND_NEAREST)
	{
		const __m256d zero = _mm256_setzero_pd(), area2 = _mm256_set1_pd(area);
		auto vector_edge = [&] (const Vertex& p, const Vertex& q, __m256d xx, __m256d yy) {
			return _mm256_sub_pd(_mm256_mul_pd(_mm256_set1_pd(q.x - p.x), _mm256_sub_pd(yy, _mm256_set1_pd(p.y))),
				_mm256_mul_pd(_mm256_set1_pd(q.y - p.y), _mm256_sub_pd(xx, _mm256_set1_pd(p.x))));
		};
		auto inside = [&] (__m256d e, bool include) {
			return include ? _mm256_cmp_pd(e, zero, _CMP_GE_OQ) : _mm256_cmp_pd(e, zero, _CMP_GT_OQ);
		};
		for (int y = top; y <= bottom; ++y)
		{
			int span_left, span_right;
			row_span(y, span_left, span_right);
			struct Group
			{
				__m256d s, t;
				unsigned active;
			};
			auto prepare_group = [&](int x) {
				const __m256d xx = _mm256_set_pd(x + 3.5, x + 2.5, x + 1.5, x + 0.5), yy = _mm256_set1_pd(y + 0.5);
				const __m256d ea = vector_edge(b, c, xx, yy), eb = vector_edge(c, a, xx, yy), ec = vector_edge(a, b, xx, yy);
				unsigned active = unsigned(_mm256_movemask_pd(_mm256_and_pd(inside(ea, include_a),
					_mm256_and_pd(inside(eb, include_b), inside(ec, include_c))))) & (x > span_right ? 0u : x + 3 <= span_right ? 15u : (1u << (span_right - x + 1)) - 1);
				if (!active)
					return Group{zero, zero, 0};
				const size_t offset = size_t(y) * operation.color_width + unsigned(x);
				if (x + 3 <= span_right)
				{
					const __m128i auxiliary = _mm_loadu_si128(reinterpret_cast<const __m128i*>(m_auxiliary.data() + offset));
					active &= unsigned(_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(
						_mm_and_si128(auxiliary, _mm_set1_epi32(0xf000)), _mm_set1_epi32(wid)))));
				}
				else
					for (unsigned lane = 0; lane < 4; ++lane)
						if ((active & (1u << lane)) && (m_auxiliary[offset + lane] & 0xf000) != wid)
							active &= ~(1u << lane);

				if (!active)
					return Group{zero, zero, 0};
				const __m256d wb = _mm256_div_pd(eb, area2), wc = _mm256_div_pd(ec, area2),
					wa = _mm256_div_pd(_mm256_div_pd(ea, area2), _mm256_set1_pd(a.z)),
					tb = _mm256_div_pd(wb, _mm256_set1_pd(b.z)), tc = _mm256_div_pd(wc, _mm256_set1_pd(c.z));
				const __m256d mask = _mm256_castsi256_pd(_mm256_set_epi64x(active & 8 ? -1ll : 0, active & 4 ? -1ll : 0, active & 2 ? -1ll : 0, active & 1 ? -1ll : 0));
				const __m256d denominator = _mm256_or_pd(_mm256_and_pd(mask, _mm256_add_pd(_mm256_add_pd(wa, tb), tc)),
					_mm256_andnot_pd(mask, _mm256_set1_pd(1)));
				const __m256d s = _mm256_div_pd(_mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(wa, _mm256_set1_pd(a.s)),
					_mm256_mul_pd(tb, _mm256_set1_pd(b.s))), _mm256_mul_pd(tc, _mm256_set1_pd(c.s))), denominator),
					t = _mm256_div_pd(_mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(wa, _mm256_set1_pd(a.t)),
					_mm256_mul_pd(tb, _mm256_set1_pd(b.t))), _mm256_mul_pd(tc, _mm256_set1_pd(c.t))), denominator);
				return Group{s, t, active};
			};
			struct Packet
			{
				__m256d s0, t0, s1, t1;
				unsigned active;
			};
			// Stage coordinates to reduce register pressure while sampling.
			Packet packets[16];
			for (int begin = span_left; begin <= span_right; begin += 128)
			{
				const unsigned count = unsigned(std::min(16, (span_right - begin) / 8 + 1));
				for (unsigned i = 0; i < count; ++i)
				{
					const int x = begin + int(i) * 8;
					const auto low = prepare_group(x), high = prepare_group(x + 4);
					packets[i] = {low.s, low.t, high.s, high.t, low.active | (high.active << 4)};
				}
				for (unsigned i = 0; i < count; ++i)
				{
					const int x = begin + int(i) * 8;
					const Packet& packet = packets[i];
					const unsigned active = packet.active;
					if (!active)
						continue;
					const __m256i color = realimage_texture8(packet.s0, packet.t0, packet.s1, packet.t1, sample_texture,
						sample_base, texture_width, texture_height, sample_row_shift);
					const size_t offset = size_t(y) * operation.color_width + unsigned(x);
					if (active == 255)
					{
						const __m256i old_color = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(color_plane + offset));
						_mm256_storeu_si256(reinterpret_cast<__m256i*>(color_plane + offset),
							_mm256_or_si256(_mm256_and_si256(old_color, _mm256_set1_epi32(0xff000000u)), color));
					}
					else
					{
						const __m256i lane_mask = _mm256_set_epi32(active & 128 ? -1 : 0, active & 64 ? -1 : 0,
							active & 32 ? -1 : 0, active & 16 ? -1 : 0, active & 8 ? -1 : 0, active & 4 ? -1 : 0,
							active & 2 ? -1 : 0, active & 1 ? -1 : 0);
						const __m256i old_color = _mm256_maskload_epi32(reinterpret_cast<const int*>(color_plane + offset), lane_mask);
						_mm256_maskstore_epi32(reinterpret_cast<int*>(color_plane + offset), lane_mask,
							_mm256_or_si256(_mm256_and_si256(old_color, _mm256_set1_epi32(0xff000000u)), color));
					}
				}
			}
		}
		return;
	}
#elif defined(REALIMAGE_SSE2)
	if (simple_texture && (_mm_getcsr() & _MM_ROUND_MASK) == _MM_ROUND_NEAREST)
	{
		const __m128d zero = _mm_setzero_pd(), area2 = _mm_set1_pd(area);
		auto vector_edge = [&] (const Vertex& p, const Vertex& q, __m128d xx, __m128d yy) {
			return _mm_sub_pd(_mm_mul_pd(_mm_set1_pd(q.x - p.x), _mm_sub_pd(yy, _mm_set1_pd(p.y))),
				_mm_mul_pd(_mm_set1_pd(q.y - p.y), _mm_sub_pd(xx, _mm_set1_pd(p.x))));
		};
		auto inside = [&] (__m128d e, bool include) {
			return include ? _mm_cmpge_pd(e, zero) : _mm_cmpgt_pd(e, zero);
		};
		for (int y = top; y <= bottom; ++y)
		{
			int span_left, span_right;
			row_span(y, span_left, span_right);
			struct Pair
			{
				__m128d s, t;
				unsigned active;
			};
			auto prepare_pair = [&](int x) {
				const __m128d xx = _mm_set_pd(x + 1.5, x + 0.5), yy = _mm_set1_pd(y + 0.5);
				const __m128d ea = vector_edge(b, c, xx, yy), eb = vector_edge(c, a, xx, yy), ec = vector_edge(a, b, xx, yy);
				unsigned active = unsigned(_mm_movemask_pd(_mm_and_pd(inside(ea, include_a),
					_mm_and_pd(inside(eb, include_b), inside(ec, include_c))))) & (x > span_right ? 0u : x < span_right ? 3u : 1u);
				const size_t offset = size_t(y) * operation.color_width + unsigned(x);
				if ((active & 1) && (m_auxiliary[offset] & 0xf000) != wid)
					active &= ~1u;
				if ((active & 2) && (m_auxiliary[offset + 1] & 0xf000) != wid)
					active &= ~2u;

				const __m128d wb = _mm_div_pd(eb, area2), wc = _mm_div_pd(ec, area2),
					wa = _mm_div_pd(_mm_div_pd(ea, area2), _mm_set1_pd(a.z)),
					tb = _mm_div_pd(wb, _mm_set1_pd(b.z)), tc = _mm_div_pd(wc, _mm_set1_pd(c.z));
				const __m128d mask = _mm_castsi128_pd(_mm_set_epi64x(active & 2 ? -1ll : 0, active & 1 ? -1ll : 0));
				const __m128d denominator = _mm_or_pd(_mm_and_pd(mask, _mm_add_pd(_mm_add_pd(wa, tb), tc)),
					_mm_andnot_pd(mask, _mm_set1_pd(1)));
				const __m128d s = _mm_div_pd(_mm_add_pd(_mm_add_pd(_mm_mul_pd(wa, _mm_set1_pd(a.s)),
					_mm_mul_pd(tb, _mm_set1_pd(b.s))), _mm_mul_pd(tc, _mm_set1_pd(c.s))), denominator),
					t = _mm_div_pd(_mm_add_pd(_mm_add_pd(_mm_mul_pd(wa, _mm_set1_pd(a.t)),
					_mm_mul_pd(tb, _mm_set1_pd(b.t))), _mm_mul_pd(tc, _mm_set1_pd(c.t))), denominator);
				return Pair{s, t, active};
			};
			struct Packet
			{
				__m128d s0, t0, s1, t1;
			};
			// Stage coordinates to reduce register pressure while sampling.
			Packet packets[32];
			unsigned active_masks[32];
			for (int begin = span_left; begin <= span_right; begin += 128)
			{
				const unsigned count = unsigned(std::min(32, (span_right - begin) / 4 + 1));
				for (unsigned i = 0; i < count; ++i)
				{
					const int x = begin + int(i) * 4;
					const auto low = prepare_pair(x);
					packets[i].s0 = low.s;
					packets[i].t0 = low.t;
					active_masks[i] = low.active;
					const auto high = prepare_pair(x + 2);
					packets[i].s1 = high.s;
					packets[i].t1 = high.t;
					active_masks[i] |= high.active << 2;
				}
				for (unsigned i = 0; i < count; ++i)
				{
					const int x = begin + int(i) * 4;
					const Packet& packet = packets[i];
					const unsigned active = active_masks[i];
					if (!active)
						continue;
					const __m128i color = realimage_texture4(packet.s0, packet.t0, packet.s1, packet.t1, m_texture.data(),
						texture_base, texture_width, texture_height, texture_row_shift);
					const size_t offset = size_t(y) * operation.color_width + unsigned(x);
					if (active == 15)
					{
						const __m128i old_color = _mm_loadu_si128(reinterpret_cast<const __m128i*>(color_plane + offset));
						_mm_storeu_si128(reinterpret_cast<__m128i*>(color_plane + offset),
							_mm_or_si128(_mm_and_si128(old_color, _mm_set1_epi32(0xff000000u)), color));
					}
					else
					{
						uint32_t pixels[4];
						_mm_storeu_si128(reinterpret_cast<__m128i*>(pixels), color);
						for (unsigned lane = 0; lane < 4; ++lane)
							if (active & (1u << lane))
								color_plane[offset + lane] = (color_plane[offset + lane] & 0xff000000u) | pixels[lane];
					}
				}
			}
		}
		return;
	}
#endif
	// Launches reuse all three slots; sorting must leave their registers intact.
	for (int y = top; y <= bottom; ++y)
	{
		int span_left, span_right;
		row_span(y, span_left, span_right);
		for (int x = span_left; x <= span_right; ++x)
		{
			const double ea = edge(b, c, x + 0.5, y + 0.5),
				eb = edge(c, a, x + 0.5, y + 0.5), ec = edge(a, b, x + 0.5, y + 0.5);
			if (ea < 0 || (ea == 0 && !include_a) ||
				eb < 0 || (eb == 0 && !include_b) || ec < 0 || (ec == 0 && !include_c))
				continue;
			const size_t offset = size_t(y) * operation.color_width + unsigned(x);
			uint32_t& auxiliary = m_auxiliary[offset];
			if ((auxiliary & 0xf000) != wid)
				continue;
			uint32_t& destination = color_plane[offset];
			const double wb = eb / area, wc = ec / area;
			if (!no_depth)
			{
				const uint32_t z = uint32_t(std::clamp(a.z + wb * (b.z - a.z) + wc * (c.z - a.z),
					0.0, 1.0) * 16777215),
					old_z = (auxiliary & 0xfff) | ((auxiliary >> 4) & 0xfff000);
				if (z > old_z || (depth_less && z == old_z))
					continue;
				auxiliary = (auxiliary & ~0x0fff0fffu) | (z & 0xfff) | ((z & 0xfff000) << 4);
			}
			if (textured)
			{
				double s, t;
				if (affine)
				{
					s = a.s + wb * (b.s - a.s) + wc * (c.s - a.s);
					t = a.t + wb * (b.t - a.t) + wc * (c.t - a.t);
				}
				else
				{
					const double wa = ea / area / a.z, tb = wb / b.z, tc = wc / c.z,
						denominator = wa + tb + tc;
					s = (wa * a.s + tb * b.s + tc * c.s) / denominator;
					t = (wa * a.t + tb * b.t + tc * c.t) / denominator;
				}
				if (simple_texture)
				{
					const uint32_t color = texture_color(s, t, texture_base,
						texture_width, texture_height, texture_row_shift);
					destination = (destination & 0xff000000u) | color;
					continue;
				}
				const auto texel = texture_sample(s, t, texture_base,
					texture_width, texture_height, texture_row_shift, operation.texture_format,
					operation.texture_clamp, operation.texture_border);
				auto primary = [&](double av, double bv, double cv, unsigned i) {
					return flat ? flat_color[i] / (i ? 256.0 : 1.0) :
						std::clamp(av + wb * (bv - av) + wc * (cv - av), 0.0, 256.0) / 256.0;
				};
				double alpha = primary(a.alpha, b.alpha, c.alpha, 0);
				if (texture_alpha && operation.texture_mode != 0)
					alpha = operation.texture_mode == 0x18000 ? texel[0] : alpha * texel[0];
				auto channel = [&](double av, double bv, double cv, unsigned i) {
					const unsigned shift = (3 - i) * 8;
					const double fragment = primary(av, bv, cv, i), texture = texel[i];
					double source;
					switch (operation.texture_mode)
					{
					case 0:
						source = texture_alpha ? fragment * (1 - texel[0]) + texture * texel[0] : texture;
						break;
					case 0x8000:
						source = fragment * texture;
						break;
					case 0x10000:
						source = fragment * (1 - texture) +
							((operation.texture_environment >> shift) & 255) / 255.0 * texture;
						break;
					default:
						source = texture;
						break;
					}
					const double result = blend ? blend_channel(source * 255, alpha,
						(destination >> shift) & 255) : source * 255;
					return uint32_t(std::clamp(result, 0.0, 255.0));
				};
				const uint32_t color = (channel(a.red, b.red, c.red, 1) << 16) |
					(channel(a.green, b.green, c.green, 2) << 8) | channel(a.blue, b.blue, c.blue, 3);
				destination = (destination & 0xff000000u) | color;
				continue;
			}
			if (flat)
			{
				uint32_t color = 0;
				for (unsigned i = 1; i < 4; ++i)
				{
					const unsigned shift = (3 - i) * 8;
					const double channel = blend ? blend_channel(flat_color[i], flat_color[0],
						(destination >> shift) & 255) : flat_color[i];
					color |= uint32_t(channel) << shift;
				}
				destination = (destination & 0xff000000u) | color;
				continue;
			}
			const double alpha = blend ? std::clamp(a.alpha + wb * (b.alpha - a.alpha) +
				wc * (c.alpha - a.alpha), 0.0, 256.0) / 256.0 : 1.0;
			auto channel = [&](double av, double bv, double cv, unsigned shift) {
				const double source = std::clamp(av + wb * (bv - av) + wc * (cv - av), 0.0, 255.0);
				return uint32_t(blend ? blend_channel(source, alpha, (destination >> shift) & 255) : source);
			};
			const uint32_t color = (channel(a.red, b.red, c.red, 16) << 16) |
				(channel(a.green, b.green, c.green, 8) << 8) | channel(a.blue, b.blue, c.blue, 0);
			destination = (destination & 0xff000000u) | color;
		}
	}
}

// RGB565, linear/repeat sampling, RGB decal mode.
uint32_t CRealImage2100::texture_color(double s, double t, uint32_t base,
	uint32_t width, uint32_t height, unsigned row_shift) const
{
	const double u = realimage_repeat_fraction(s) * width - 0.5,
		v = realimage_repeat_fraction(t) * height - 0.5;
	const int x = int(std::floor(u)), y = int(std::floor(v));
	const double fx = u - x, fy = v - y;
	auto texel = [&](int tx, int ty) {
		const uint32_t offset = base + ((uint32_t(ty) & (height - 1)) << row_shift) +
			((uint32_t(tx) & (width - 1)) << 1);
		return uint32_t(m_texture[offset]) | (uint32_t(m_texture[offset + 1]) << 8);
	};
	const uint32_t pixels[] = {texel(x, y), texel(x + 1, y), texel(x, y + 1), texel(x + 1, y + 1)};
	auto channel = [&](unsigned shift, uint32_t mask) {
		const double a = (pixels[0] >> shift) & mask, b = (pixels[1] >> shift) & mask,
			c = (pixels[2] >> shift) & mask, d = (pixels[3] >> shift) & mask;
		const double top = a + fx * (b - a), bottom = c + fx * (d - c);
		return uint32_t(std::clamp((top + fy * (bottom - top)) * 255 / mask, 0.0, 255.0));
	};
	return (channel(11, 31) << 16) | (channel(5, 63) << 8) | channel(0, 31);
}

std::array<double, 4> CRealImage2100::texture_sample(double s, double t, uint32_t base,
	uint32_t width, uint32_t height, unsigned row_shift, uint32_t format,
	uint32_t clamp, uint32_t border) const
{
	const bool clamp_s = (clamp & 8) != 0, clamp_t = (clamp & 4) != 0,
		rgba = (format & 0x7000) == 0x3000, wide = (format & 0x0f000000u) == 0x0d000000u;
	const double u = (clamp_s ? std::clamp(s, 0.0, 1.0) : realimage_repeat_fraction(s)) * width - 0.5,
		v = (clamp_t ? std::clamp(t, 0.0, 1.0) : realimage_repeat_fraction(t)) * height - 0.5;
	const int x = int(std::floor(u)), y = int(std::floor(v));
	const double fx = u - x, fy = v - y;
	auto texel = [&](int tx, int ty) {
		// GL_CLAMP blends edge samples with the packed constant border.
		if ((clamp_s && (tx < 0 || tx >= int(width))) || (clamp_t && (ty < 0 || ty >= int(height))))
			return border;
		const uint32_t offset = base + ((uint32_t(ty) & (height - 1)) << row_shift) +
			((uint32_t(tx) & (width - 1)) << (wide ? 2 : 1));
		uint32_t value = uint32_t(m_texture[offset]) | (uint32_t(m_texture[offset + 1]) << 8);
		if (wide)
			value |= (uint32_t(m_texture[offset + 2]) << 16) | (uint32_t(m_texture[offset + 3]) << 24);
		return value;
	};
	const uint32_t pixels[] = {texel(x, y), texel(x + 1, y), texel(x, y + 1), texel(x + 1, y + 1)};
	auto channel = [&](unsigned shift, uint32_t mask) {
		const double a = (pixels[0] >> shift) & mask, b = (pixels[1] >> shift) & mask,
			c = (pixels[2] >> shift) & mask, d = (pixels[3] >> shift) & mask;
		const double top = a + fx * (b - a), bottom = c + fx * (d - c);
		return (top + fy * (bottom - top)) / mask;
	};
	if (wide)
		return {rgba ? channel(24, 255) : 1, channel(16, 255), channel(8, 255), channel(0, 255)};
	return rgba ? std::array<double, 4>{channel(12, 15), channel(8, 15), channel(4, 15), channel(0, 15)} :
		std::array<double, 4>{1, channel(11, 31), channel(5, 63), channel(0, 31)};
}

void CRealImage2100::host_data(uint32_t v)
{
	if (!m_pending.width)
	{
		unimplemented_once("REALimage host data without upload", HostData, v, true);
		return;
	}
	const int32_t x = int16_t(m_pending.x) +
		int32_t(m_pending.word % m_pending.width);
	const int32_t y = int16_t(m_pending.y) +
		int32_t(m_pending.word / m_pending.width);
	// Masked-out pixels still consume their host-data word.
	if (!m_pending.auxiliary_compare_mask ||
		(uint32_t(x) < m_color_width && uint32_t(y) < m_color_height &&
			!((m_auxiliary[size_t(y) * m_color_width + x] ^ m_pending.auxiliary_reference) &
				m_pending.auxiliary_compare_mask)))
		execute_pixel(prepare_pixel(uint32_t(x), uint32_t(y), v, m_pending.banks));
	if (++m_pending.word == uint64_t(m_pending.width) * m_pending.height)
		m_pending = {};
}

uint32_t CRealImage2100::host_read()
{
	// Readback returns RGB directly, without destination ROP or write masks.
	const uint32_t value = readback_pixel(m_readback.word);
	if (++m_readback.word == m_readback.width * m_readback.height)
		m_readback = {};
	return value;
}

uint32_t CRealImage2100::readback_pixel(uint32_t word) const
{
	const uint32_t bank = m_readback.banks == 2 ? 1 : 0,
		x = m_readback.x + word % m_readback.width,
		y = m_readback.y + word / m_readback.width;
	return m_color[size_t(bank) * m_color_pixels + y * m_color_width + x];
}

void CRealImage2100::update_irq()
{
	// OpenVMS enables DMA notifications with 7 and masks them with 0 or 1.
	const bool level = m_dma_irq_pending && m_dma_regs[1] == 7;
	if (level == m_irq)
		return;
	m_irq = level;
	if (m_irq_callback)
		m_irq_callback(level);
}

void CRealImage2100::dma_completed()
{
	m_dma_irq_pending = true;
	update_irq();
}

bool CRealImage2100::dma_setup_supported(uint32_t v) const
{
	if ((m_dma_regs[0] & ~0x200u) ||
		(m_dma_regs[1] != 0 && m_dma_regs[1] != 1 && m_dma_regs[1] != 7) ||
		(m_dma_regs[13] && (m_dma_regs[13] != 0xffffe000u ||
			((v & ~DMACommandListCountMask) != 0xc0000000u &&
				(v & 0xffff0000u) != 0xc4800000u))) || m_dma_regs[14] != 8)
		return false;
	for (unsigned i : {2u, 3u, 4u, 5u, 6u, 10u, 15u, 16u, 17u, 18u})
		if (m_dma_regs[i])
			return false;
	return true;
}

void CRealImage2100::dma_command(uint32_t v)
{
	const uint32_t mode = v & ~DMACommandListCountMask;
	if (mode == 0xa1800000u || mode == 0xc0800000u)
	{
		dma_texture_upload(v);
		return;
	}
	if (mode == 0xc0000000u || mode == 0xc0400000u || mode == 0xa1000000u)
	{
		dma_command_list(v);
		return;
	}
	// Emulator staging limit, independent of driver buffer allocation.
	constexpr uint32_t DMABufferSize = 32768;
	const uint32_t destination = m_dma_regs[8], source = m_dma_regs[9],
		completion = m_dma_regs[12], words = v & 0xffff, bytes = words * 4;
	if ((v & 0xffff0000u) != 0xc4800000u || !words || bytes > DMABufferSize ||
		!dma_setup_supported(v) ||
		!m_readback.width ||
		uint64_t(m_readback.width) * m_readback.height - m_readback.word < words ||
		((destination | source | completion) & 3) ||
		source < HostData || source > HostData + HostReadSize - bytes ||
		uint64_t(destination) + bytes > 0x100000000ull ||
		uint64_t(completion) + 4 > 0x100000000ull ||
		(uint64_t(completion) < uint64_t(destination) + bytes &&
			uint64_t(completion) + 4 > destination))
	{
		unimplemented(
			"REALimage DMA transfer (rejected; completion not written)", DMACommand, v, true);
		return;
	}
	std::array<uint8_t, DMABufferSize> data{};
	for (uint32_t word = 0; word < words; ++word)
	{
		const uint32_t pixel = readback_pixel(m_readback.word + word);
		for (unsigned lane = 0; lane < 4; ++lane)
			data[word * 4 + lane] = uint8_t(pixel >> (lane * 8));
	}
	// Commit FIFO progress only after payload and completion writes succeed.
	if (!m_dma_writer || !m_dma_writer(destination, data.data(), bytes, completion))
	{
		report("DMA_WRITE", destination, bytes, "PCI DMA write unavailable or rejected");
		return;
	}
	m_readback.word += words;
	if (m_readback.word == m_readback.width * m_readback.height)
		m_readback = {};
	dma_completed();
}

void CRealImage2100::dma_texture_upload(uint32_t v)
{
	const uint32_t completion = m_dma_regs[12];
	auto reject = [&]() {
		unimplemented("REALimage DMA texture upload (rejected; completion not written)",
			DMACommand, v, true);
	};
	auto source_range = [&](uint32_t address, uint32_t bytes) {
		return !((address | bytes) & 3) &&
			uint64_t(address) + bytes <= 0x100000000ull &&
			!(uint64_t(completion) < uint64_t(address) + bytes &&
				uint64_t(completion) + 4 > address);
	};
	if (!dma_setup_supported(v) || m_dma_list_active || (completion & 3))
	{
		reject();
		return;
	}
	if (!m_dma_reader || !m_dma_completer)
	{
		report("DMA_READ", m_dma_regs[8], v, "PCI DMA upload callbacks unavailable");
		return;
	}
	// Stage the chain so a bad link cannot leave a partial upload.
	auto texture = m_texture;
	std::set<uint32_t> visited;
	uint32_t command = v, source = m_dma_regs[8], target = m_dma_regs[9],
		next = m_dma_regs[11];
	for (;;)
	{
		const uint32_t mode = command & ~DMACommandListCountMask,
			bytes = (command & DMACommandListCountMask) * 4;
		if ((mode != 0xa1800000u && mode != 0xc0800000u) || !bytes ||
			!source_range(source, bytes) || (target & 3) || target < 0x04000000u ||
			uint64_t(target) + bytes > 0x04000000ull + MaxTextureSize ||
			(mode == 0xa1800000u &&
				(!source_range(next, 16) || !visited.insert(next).second)))
		{
			reject();
			return;
		}
		std::vector<uint8_t> data(bytes);
		if (!m_dma_reader(source, data.data(), bytes, completion))
		{
			report("DMA_READ", source, bytes, "PCI DMA texture data unavailable or rejected");
			return;
		}
		for (uint32_t i = 0; i < bytes; i += 4)
		{
			const uint32_t offset = texture_offset(target - 0x04000000u + i);
			if (offset != 0xffffffffu)
				std::copy_n(data.data() + i, 4, texture.data() + offset);
		}
		if (mode == 0xc0800000u)
			break;
		std::array<uint8_t, 16> descriptor{};
		if (!m_dma_reader(next, descriptor.data(), descriptor.size(), completion))
		{
			report("DMA_READ", next, uint32_t(descriptor.size()),
				"PCI DMA descriptor unavailable or rejected");
			return;
		}
		auto word = [&](unsigned i) {
			const uint8_t* p = descriptor.data() + i * 4;
			return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
				(uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
		};
		command = word(0);
		source = word(1);
		target = word(2);
		next = word(3);
	}
	m_texture.swap(texture);
	if (!m_dma_completer(completion))
		report("DMA_COMPLETE", completion, v, "PCI DMA completion unavailable or rejected");
	else
		dma_completed();
}

static bool dma_board_update(uint32_t address)
{
	return address == CRealImage2100::BoardIO + 1 ||
		address == CRealImage2100::WindowMask + 2;
}

bool CRealImage2100::dma_list_target(uint32_t a) const
{
	if (dma_board_update(a))
		return true;
	if (a & 3)
		return false;
	if (vertex_register(a) || integer_vertex_register(a) ||
		a == ContextLink || zero_context_register(a))
		return true;
	if (a >= HostData && a < HostData + HostDataSize)
		return true;
	if (a >= 0x01000000 && a < 0x03000000)
		return ((a & 0x1fff) >> 2) < m_color_width &&
			((a & 0x00ffffff) >> 13) < m_color_height;
	const int plane_index = plane_register(a);
	if (plane_index >= 0)
	{
		const uint32_t available = m_color_width == MaxColorWidth ? 255u : 15u;
		return plane_index < 16 || plane_index > 24 ||
			a == PlanePixelMask || (a & ~0x1fu) == PlaneClearColor ||
			!(((a >> 13) & 255) & ~available);
	}
	// Other command-list targets use the existing register handlers.
	switch (a)
	{
	case Status: case UnitReset:
	case BoardIO: case WindowMask:
	case ClipXMax: case ClipYMax: case ClipXMin: case ClipYMin:
	case GlobalControl0: case GlobalControl1: case GlobalControl2:
	case TextureBase:
	case PipelineControl0: case PipelineControl1:
	case PipelineControl2: case PipelineControl3:
	case PipelineControl4: case PipelineControl5:
	case DrawControl: case Foreground: case Background:
	case MonoPattern0: case MonoPattern1: case MonoPattern2: case MonoPattern3:
	case HostOrigin: case HostExtent: case HostCommand:
	case FillOrigin: case FillExtent: case FillCommand:
	case BitmapForeground: case BitmapBackground: case BitmapPattern0: case BitmapPattern1:
	case BitmapOrigin: case BitmapExtent: case BitmapCommand:
	case BlockSource: case BlockDestination: case BlockExtent: case BlockCommand:
		return true;
	default:
		return false;
	}
}

void CRealImage2100::dma_command_list(uint32_t v)
{
	DMAListOperation operation;
	if (prepare_dma_list(v, operation))
		execute_dma_list(operation);
}

bool CRealImage2100::prepare_dma_list(uint32_t v, DMAListOperation& operation)
{
	operation = {};
	operation.command = v;
	operation.completion = m_dma_regs[12];
	const uint32_t completion = operation.completion;
	uint32_t command = v, source = m_dma_regs[8], initial_address = m_dma_regs[9],
		next = m_dma_regs[11];
	auto reject = [&]() {
		unimplemented("REALimage DMA command list (rejected; completion not written)",
			DMACommand, v, true);
	};
	auto source_range = [&](uint32_t address, uint32_t bytes) {
		return !((address | bytes) & 3) &&
			uint64_t(address) + bytes <= 0x100000000ull &&
			!(uint64_t(completion) < uint64_t(address) + bytes &&
				uint64_t(completion) + 4 > address);
	};
	if (m_dma_list_active || !dma_setup_supported(v) || (completion & 3))
	{
		reject();
		return false;
	}
	if (!m_dma_reader || !m_dma_completer)
	{
		report("DMA_READ", source, v, "PCI DMA command-list callbacks unavailable");
		return false;
	}
	auto decode_word = [](const uint8_t* p) {
		return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
			(uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
	};
	auto& data = operation.data;
	auto word_at = [&](uint32_t index) {
		return decode_word(data.data() + size_t(index) * 4);
	};
	auto& packets = operation.packets;
	std::set<uint32_t> visited;
	// Preflight every link within the existing staging budget before executing.
	for (;;)
	{
		const uint32_t mode = command & ~DMACommandListCountMask,
			words = command & DMACommandListCountMask, bytes = words * 4;
		if ((mode != 0xc0000000u && mode != 0xc0400000u && mode != 0xa1000000u) || !words ||
			(mode == 0xc0000000u && !data.empty()) ||
			bytes > DMACommandListMaxBytes - data.size() || !source_range(source, bytes) ||
			((initial_address & 3) && !dma_board_update(initial_address)) || (mode == 0xa1000000u &&
				(!source_range(next, 16) || !visited.insert(next).second)))
		{
			reject();
			return false;
		}
		const uint32_t first = uint32_t(data.size() / 4), end = first + words;
		data.resize(data.size() + bytes);
		if (!m_dma_reader(source, data.data() + size_t(first) * 4, bytes, completion))
		{
			report("DMA_READ", source, bytes, "PCI DMA read unavailable or rejected");
			return false;
		}
		uint32_t cursor = first, address = initial_address;
		while (cursor < end)
		{
			const uint32_t control = word_at(cursor++), count = control & 0xffff;
			if (count > end - cursor ||
				uint64_t(address) + uint64_t(count) * 4 > 0x100000000ull)
			{
				reject();
				return false;
			}
			// OpenVMS DMA board updates carry packed state and echo the read-only board ID.
			if (dma_board_update(address) && (mode != 0xc0000000u || count != 1 ||
				(address == BoardIO + 1 && (word_at(cursor) >> 24) != BoardIDPCGA3)))
			{
				report("DMA_TARGET", address, control, "Unsupported DMA board-update profile");
				reject();
				return false;
			}
			if (!dma_list_target(address))
			{
				report("DMA_TARGET", address, control, "Unsupported DMA command-list target");
				reject();
				return false;
			}
			for (uint32_t i = 0; i < count; ++i)
				if (!dma_list_target(address + i * 4))
				{
					report("DMA_TARGET", address + i * 4, word_at(cursor + i),
						"Unsupported DMA command-list target");
					reject();
					return false;
				}
			packets.push_back({address, control & 0xffff0000u, cursor, count});
			cursor += count;
			if (cursor == end)
				break;
			address = word_at(cursor++);
			if (cursor == end)
			{
				reject();
				return false;
			}
		}
		if (mode == 0xc0000000u || mode == 0xc0400000u)
			break;
		std::array<uint8_t, 16> descriptor{};
		if (!m_dma_reader(next, descriptor.data(), descriptor.size(), completion))
		{
			report("DMA_READ", next, uint32_t(descriptor.size()),
				"PCI DMA descriptor unavailable or rejected");
			return false;
		}
		command = decode_word(descriptor.data());
		source = decode_word(descriptor.data() + 4);
		initial_address = decode_word(descriptor.data() + 8);
		next = decode_word(descriptor.data() + 12);
		// Captured chains retain the latched completion address at their terminal link.
		if ((command & ~DMACommandListCountMask) == 0xc0400000u && next != completion)
		{
			reject();
			return false;
		}
	}
	return true;
}

void CRealImage2100::execute_dma_list(const DMAListOperation& operation)
{
	const uint32_t completion = operation.completion;
	auto word_at = [&](uint32_t index) {
		const uint8_t* p = operation.data.data() + size_t(index) * 4;
		return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
			(uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
	};
	auto reject = [&]() {
		unimplemented("REALimage DMA command list (rejected; completion not written)",
			DMACommand, operation.command, true);
	};
	struct ActiveList
	{
		bool& active;
		~ActiveList() { active = false; }
	} active{m_dma_list_active};
	m_dma_list_active = true;
	m_dma_list_rejected = false;
	for (const auto& packet : operation.packets)
	{
		if (packet.wait_mask)
		{
			bool ready = false;
			for (uint32_t i = 0; i < StatusFrameReads; ++i)
				if (!(status_read() & packet.wait_mask))
				{
					ready = true;
					break;
				}
			if (!ready)
			{
				report("DMA_WAIT", packet.address, packet.wait_mask,
					"DMA command-list status wait did not complete");
				return;
			}
		}
		for (uint32_t i = 0; i < packet.count; ++i)
		{
			const uint32_t address = packet.address + i * 4, value = word_at(packet.first + i);
			if (address == BoardIO + 1)
			{
				WriteMem(BoardIO, 16, uint16_t(value));
				WriteMem(BoardIO + 2, 8, uint8_t(value >> 16));
			}
			else if (address == WindowMask + 2)
				WriteMem(WindowMask, 32, value);
			else
				WriteMem(address, 32, value);
			if (m_dma_list_rejected)
			{
				reject();
				return;
			}
		}
	}
	m_dma_list_active = false;
	if (!m_dma_completer(completion))
		report("DMA_COMPLETE", completion, 0,
			"PCI DMA completion unavailable or rejected");
	else
		dma_completed();
}

CRealImage2100::Frame CRealImage2100::scanout(std::string* error) const
{
	return scanout(Frame{}, error);
}

CRealImage2100::Frame CRealImage2100::scanout(Frame frame, std::string* error) const
{
	frame = dac_frame(std::move(frame), error);
	if (!(m_dac_regs[0x0d] & 4))
		std::fill(frame.argb.begin(), frame.argb.end(), 0xff000000u);
	return frame;
}

CRealImage2100::Frame CRealImage2100::dac_frame(Frame frame, std::string* error, bool vram_input) const
{
	auto reject = [&](const char* text) {
		if (error)
			*error = text;
		frame.width = frame.height = 0;
		frame.argb.clear();
		return std::move(frame);
	};
	if (error)
		error->clear();
	if (!native_display() || (m_board_io & 0x80))
		return reject("Native display disabled/blanked");
	if (!native_pixel_profile())
		return reject("Native pixel layout unsupported");
	if (!(m_dac_regs[0x0b] & 1))
		return reject("Native DAC disabled");
	// Each pixel's WID selects an RGB640 window attribute entry.
	std::array<bool, 16> supported_windows{};
	for (uint32_t i = 0; i < 16; ++i)
	{
		const uint32_t fb = 0x100 + i * 4, overlay = 0x200 + i * 4;
		const bool rgb = (m_dac_regs[fb] == 8 && m_dac_regs[fb + 1] == 12) ||
			m_dac_regs[fb] == 9;
		supported_windows[i] = vram_input || (rgb && !m_dac_regs[fb + 2] && !m_dac_regs[fb + 3] &&
			m_dac_regs[overlay] == 4 && !m_dac_regs[overlay + 1] &&
			!m_dac_regs[overlay + 2] &&
			(m_dac_regs[overlay + 3] == 0x48 || m_dac_regs[overlay + 3] == 0x44));
	}
	// Unlike VGA, vertical display is a count, not a last-line index.
	const uint32_t horizontal = peek(TimingBase),
		overflow = peek(TimingBase + 4) >> 24,
		vertical = peek(TimingBase + 0x10),
		extension = (peek(TimingBase + 0x1c) >> 16) & 255;
	// The captured VMS clock setup retains eight-pixel timing counts with 4:1 serialization.
	const bool unscaled_timing = m_color_width == MaxColorWidth &&
		peek(DisplaySelect) == 0x4700 && peek(BoardTiming) == 0x26c03 &&
		m_board_timing == 0x38 && horizontal == 0x9fce &&
		peek(TimingBase + 4) == 0x2814a2 && vertical == 0x301 && extension == 0x0d;
	const uint32_t horizontal_unit = m_dac_regs[0x08] == 0 ? 8 :
		m_dac_regs[0x08] == 1 && m_color_width == MaxColorWidth ?
		(unscaled_timing ? 8 : 16) : 0;
	const uint32_t width = ((((horizontal >> 8) & 255) |
		((extension & 0x40) << 2)) + 1) * horizontal_unit,
		vertical_extension = extension & 0x3f;
	// Whole extension patterns are established by both drivers' mode tables.
	const uint32_t height = ((vertical >> 16) & 255) |
		((overflow & 2) << 7) | ((overflow & 0x40) << 3) |
		(vertical_extension == 0x0d ? 1024u : 0u);
	if ((vertical_extension != 0 && vertical_extension != 8 && vertical_extension != 0x0d) ||
		!width || !height ||
		width > m_color_width || height > m_color_height)
		return reject("Native timing unsupported");
	frame.width = width;
	frame.height = height;
	frame.argb.resize(size_t(width) * height);
	// Board I/O bits 8..23 select the color bank for each WID.
	const uint32_t bank_select = (m_board_io >> 8) & 0xffff;
	const bool all_windows_supported = std::all_of(supported_windows.begin(),
		supported_windows.end(), [](bool v) { return v; });
	if ((bank_select == 0 || bank_select == 0xffff) && all_windows_supported)
	{
		for (uint32_t y = 0; y < height; ++y)
		{
			const uint32_t* source = m_color.data() +
				(bank_select ? m_color_pixels : 0) + size_t(y) * m_color_width;
			uint32_t* destination = frame.argb.data() + size_t(y) * width;
			for (uint32_t x = 0; x < width; ++x)
				destination[x] = 0xff000000 | source[x];
		}
		if (!vram_input)
			composite_cursor(frame);
		return frame;
	}
	if (all_windows_supported)
	{
		for (uint32_t y = 0; y < height; ++y)
		{
			const size_t row = size_t(y) * m_color_width;
			uint32_t* destination = frame.argb.data() + size_t(y) * width;
			uint32_t x = 0;
#if defined(REALIMAGE_SSE2)
			for (; x + 4 <= width; x += 4)
			{
				const __m128i wids = _mm_and_si128(_mm_srli_epi32(_mm_loadu_si128(
					reinterpret_cast<const __m128i*>(m_auxiliary.data() + row + x)), 12),
					_mm_set1_epi32(15));
				const unsigned wid = unsigned(_mm_cvtsi128_si32(wids));
				if (_mm_movemask_epi8(_mm_cmpeq_epi32(wids, _mm_set1_epi32(wid))) == 0xffff)
				{
					const uint32_t* source = m_color.data() + row + x +
						(((bank_select >> wid) & 1) ? m_color_pixels : 0);
					_mm_storeu_si128(reinterpret_cast<__m128i*>(destination + x),
						_mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(source)),
							_mm_set1_epi32(int(0xff000000u))));
				}
				else
					for (unsigned i = 0; i < 4; ++i)
					{
						const unsigned pixel_wid = (m_auxiliary[row + x + i] >> 12) & 15;
						const size_t bank = (bank_select >> pixel_wid) & 1;
						destination[x + i] = 0xff000000 | m_color[bank * m_color_pixels + row + x + i];
					}
			}
#endif
			for (; x < width; ++x)
			{
				const unsigned wid = (m_auxiliary[row + x] >> 12) & 15;
				const size_t bank = (bank_select >> wid) & 1;
				destination[x] = 0xff000000 | m_color[bank * m_color_pixels + row + x];
			}
		}
		if (!vram_input)
			composite_cursor(frame);
		return frame;
	}

	for (uint32_t y = 0; y < height; ++y)
		for (uint32_t x = 0; x < width; ++x)
		{
			const size_t offset = size_t(y) * m_color_width + x;
			const unsigned wid = (m_auxiliary[offset] >> 12) & 15;
			if (!supported_windows[wid])
				return reject("Native DAC window format unsupported");
			const size_t bank = (bank_select >> wid) & 1;
			frame.argb[size_t(y) * width + x] =
				0xff000000 | m_color[bank * m_color_pixels + offset];
		}
	if (!vram_input)
		composite_cursor(frame);
	return frame;
}

void CRealImage2100::composite_cursor(Frame& frame, bool ten_bit) const
{
	// Blink-to-transparent has no effect while blinking is disabled.
	const unsigned control = m_dac_regs[0x4b] & ~0x10u;
	const bool x11 = control == 0x0b;
	if (control != 0x0a && !x11)
		return;
	// Position uses twelve data bits and bit 15 as the sign.
	const int origin_x = int(m_dac_regs[0x40] | ((m_dac_regs[0x41] & 15) << 8)) -
		((m_dac_regs[0x41] & 0x80) ? 4096 : 0) - (m_dac_regs[0x44] & 63);
	const int origin_y = int(m_dac_regs[0x42] | ((m_dac_regs[0x43] & 15) << 8)) -
		((m_dac_regs[0x43] & 0x80) ? 4096 : 0) - (m_dac_regs[0x45] & 63);
	uint32_t colors[2];
	for (unsigned i = 0; i < 2; ++i)
	{
		const unsigned address = 0x4800 + 3 * (i + 1);
		colors[i] = 0xff000000u | (uint32_t(m_dac_regs[address]) << 16) |
			(uint32_t(m_dac_regs[address + 1]) << 8) | m_dac_regs[address + 2];
		if (ten_bit)
			colors[i] = dac_rgb30(colors[i]);
	}
	for (int cy = 0; cy < 64; ++cy)
	{
		const int y = origin_y + cy;
		if (y < 0 || uint32_t(y) >= frame.height)
			continue;
		for (int cx = 0; cx < 64; ++cx)
		{
			const int x = origin_x + cx;
			if (x < 0 || uint32_t(x) >= frame.width)
				continue;
			// Low pairs come first; X11 uses bit 1 as the mask and bit 0 as the color.
			const unsigned code = (m_dac_regs[0x1000 + cy * 16 + cx / 4] >>
				(2 * (cx & 3))) & 3;
			uint32_t& pixel = frame.argb[size_t(y) * frame.width + unsigned(x)];
			if (x11 ? code >= 2 : code < 2)
				pixel = colors[code & 1];
			else if (!x11 && code == 3)
				pixel ^= ten_bit ? 0x20080200u : 0x00808080u;
		}
	}
}

// BAR2 index/data pair: dword index at +0, width-aware data at +4.
uint32_t CRealImage2100::io_read(uint32_t a, int bits)
{
	if (a == 0 && bits == 32)
		return m_io_index;
	if (a == 4 && valid_width(bits))
		return ReadMem(m_io_index, bits);
	unimplemented("REALimage BAR2 port", a, 0, false);
	return width_mask(valid_width(bits) ? bits : 32);
}

void CRealImage2100::io_write(uint32_t a, int bits, uint32_t v)
{
	if (a == 0 && bits == 32)
	{
		m_io_index = v;
		return;
	}
	if (a == 4 && valid_width(bits))
	{
		WriteMem(m_io_index, bits, v);
		return;
	}
	unimplemented("REALimage BAR2 port", a, v, true);
}

uint32_t CRealImage2100::mem_read(uint32_t a, int bits)
{
	if (!valid_width(bits) || (a & (unsigned(bits) / 8 - 1)))
	{
		report("ACCESS_WIDTH", a, uint32_t(bits), "Invalid BAR1 width or alignment");
		return 0xffffffffu;
	}
	const uint32_t offset = texture_offset(a);
	if (offset == 0xffffffffu)
		return width_mask(bits);
	uint32_t value = 0;
	for (unsigned i = 0; i < unsigned(bits) / 8; ++i)
		value |= uint32_t(m_texture[offset + i]) << (i * 8);
	return value;
}

void CRealImage2100::mem_write(uint32_t a, int bits, uint32_t v)
{
	if (!valid_width(bits) || (a & (unsigned(bits) / 8 - 1)))
	{
		report("ACCESS_WIDTH", a, v, "Invalid BAR1 width or alignment");
		return;
	}
	const uint32_t offset = texture_offset(a);
	if (offset == 0xffffffffu)
		return;
	for (unsigned i = 0; i < unsigned(bits) / 8; ++i)
		m_texture[offset + i] = uint8_t(v >> (i * 8));
}

uint32_t CRealImage2100::texture_offset(uint32_t a) const
{
	if (a >= MaxTextureSize)
		return 0xffffffffu;
	if (m_texture.size() == MaxTextureSize)
		return a;
	// The 300 populates two of four 4 KiB tiles per aperture row.
	if (a & 0x2000)
		return 0xffffffffu;
	return (a & 0x1fff) | ((a >> 14) << 13);
}

void CRealImage2100::configure_framebuffer(uint32_t ram_chips)
{
	if (ram_chips != 12 && ram_chips != 24)
		throw std::invalid_argument("Invalid REALimage framebuffer population");
	const uint32_t width = ram_chips == 24 ? MaxColorWidth : ColorWidth,
		height = ram_chips == 24 ? MaxColorHeight : ColorHeight;
	if (width == m_color_width && height == m_color_height)
		return;
	std::vector<uint32_t> color(size_t(width) * height * 2);
	std::vector<uint32_t> auxiliary(size_t(width) * height);
	m_color.swap(color);
	m_auxiliary.swap(auxiliary);
	m_color_width = width;
	m_color_height = height;
	m_color_pixels = width * height;
	m_board_straps = ram_chips == 24 ? 3 : 0;
	m_planes = {};
	m_clear_cache = {};
	m_pending = {};
	m_readback = {};
}

void CRealImage2100::configure_texture_memory(uint32_t bytes)
{
	if (bytes != MinTextureSize && bytes != MaxTextureSize)
		throw std::invalid_argument("Invalid PowerStorm texture memory size");
	if (m_texture.size() != bytes)
		m_texture.assign(bytes, 0);
}

// Device lifecycle and diagnostics

CRealImage2100::CRealImage2100()
	: m_vga_memory(VGAMemorySize), m_dac_regs(DACRegisterCount),
	  m_color(m_color_pixels * 2), m_auxiliary(m_color_pixels), m_texture(MinTextureSize)
{
	dac_port_map(m_dac_ports);
	reset();
}

void CRealImage2100::reset(bool clear)
{
	// PCI configuration belongs to the board wrapper and is NOT reset here.
	if (clear)
	{
		std::fill(m_vga_memory.begin(), m_vga_memory.end(), uint8_t(0));
		std::fill(m_color.begin(), m_color.end(), uint32_t(0));
		std::fill(m_auxiliary.begin(), m_auxiliary.end(), uint32_t(0));
		std::fill(m_texture.begin(), m_texture.end(), uint8_t(0));
	}
	m_dma_regs.fill(0);
	m_dma_irq_pending = false;
	m_planes = {};
	m_clear_cache = {};
	m_pending = {};
	m_readback = {};
	m_palette.fill(0);
	std::fill(m_dac_regs.begin(), m_dac_regs.end(), uint8_t(0));
	m_dac_regs[0xff] = 0x3f;
	m_shadow.clear();
	m_warned.clear();
	m_aperture_warned = m_shadow_full_warned = false;
	// Power-on values are undocumented; the card presents VGA before POST.
	m_unit_reset = m_interrupt_enable = m_display_control = 0;
	m_vga_control = VGAControlVGA;
	m_status_phase = 0;
	m_status_mask = ~0u;
	m_frame_counter = 0;
	m_board_control = m_board_timing = 0;
	m_board_io = 0;
	m_io_index = 0;
	m_dac_index = 0;
	m_dac_component = 0;
	m_palette_read = m_palette_write = 0;
	update_irq();
}

void CRealImage2100::report(
	const char* code, uint32_t a, uint32_t v, const char* message, bool fatal)
{
	if (m_dma_list_active)
		m_dma_list_rejected = true;
	if (m_diagnostic)
		m_diagnostic(Diagnostic{code, message, a, v, fatal});
}

void CRealImage2100::unimplemented(
	const char* what, uint32_t a, uint32_t v, bool write)
{
	if (m_dma_list_active)
		m_dma_list_rejected = true;
	if (m_unimplemented)
		m_unimplemented(what, a, v, write);
}

void CRealImage2100::unimplemented_once(
	const char* what, uint32_t a, uint32_t v, bool write)
{
	if (m_dma_list_active)
		m_dma_list_rejected = true;
	if (m_warned.size() < MaxShadowRegisters && m_warned.insert(a & ~3u).second)
		unimplemented(what, a, v, write);
}

void CRealImage2100::write_ppm(std::ostream& out, const Frame& f)
{
	if (!f.width || !f.height || f.argb.size() != uint64_t(f.width) * f.height)
		throw std::runtime_error("Invalid PowerStorm frame");
	out << "P6\n" << f.width << ' ' << f.height << "\n255\n";
	for (const uint32_t c : f.argb)
	{
		out.put(char(c >> 16));
		out.put(char(c >> 8));
		out.put(char(c));
	}
	if (!out)
		throw std::runtime_error("PowerStorm frame write failed");
}

// Snapshots

static void put32(std::ostream& out, uint32_t v)
{
	for (unsigned i = 0; i < 4; ++i)
		out.put(char(v >> (8 * i)));
	if (!out)
		throw std::runtime_error("PowerStorm snapshot write failed");
}

static uint32_t get32(std::istream& in)
{
	uint32_t v = 0;
	for (unsigned i = 0; i < 4; ++i)
	{
		const int b = in.get();
		if (b == std::char_traits<char>::eof())
			throw std::runtime_error("PowerStorm snapshot truncated");
		v |= uint32_t(uint8_t(b)) << (8 * i);
	}
	return v;
}

static uint32_t crc32(const std::string& bytes)
{
	uint32_t crc = 0xffffffff;
	for (const unsigned char byte : bytes)
	{
		crc ^= byte;
		for (unsigned i = 0; i < 8; ++i)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
	}
	return ~crc;
}

// Selectors, latches, transfers, palette/DAC, shadow, VGA and color banks.
static constexpr uint32_t FixedPayload = CRealImage2100::MinStateSize - 16;

void CRealImage2100::SaveState(std::ostream& out) const
{
	std::ostringstream p(std::ios::binary);
	put32(p, m_io_index);
	put32(p, m_dac_index);
	put32(p, m_palette_read);
	put32(p, m_palette_write);
	put32(p, m_unit_reset);
	put32(p, m_interrupt_enable);
	put32(p, m_vga_control);
	put32(p, m_display_control);
	put32(p, m_status_phase);
	put32(p, m_frame_counter);
	put32(p, m_board_control);
	put32(p, m_board_io);
	put32(p, m_board_timing);
	put32(p, m_dac_component);
	put32(p, m_pending.x);
	put32(p, m_pending.y);
	put32(p, m_pending.width);
	put32(p, m_pending.height);
	put32(p, m_pending.word);
	put32(p, m_pending.banks);
	put32(p, uint32_t(m_texture.size()));
	for (const uint32_t reg : m_dma_regs)
		put32(p, reg);
	for (const auto& plane : m_planes)
	{
		put32(p, plane.written);
		put32(p, plane.unknown_masks);
		const unsigned count = m_color_width == MaxColorWidth ?
			PlaneRegisterCount : LegacyPlaneRegisterCount;
		for (unsigned i = 0; i < count; ++i)
			put32(p, plane.regs[i]);
	}
	for (const auto& cache : m_clear_cache)
	{
		put32(p, cache.source);
		put32(p, cache.color);
		put32(p, cache.known);
	}
	put32(p, m_readback.x);
	put32(p, m_readback.y);
	put32(p, m_readback.width);
	put32(p, m_readback.height);
	put32(p, m_readback.word);
	put32(p, m_readback.banks);
	for (const uint32_t c : m_palette)
		put32(p, c);
	p.write(reinterpret_cast<const char*>(m_dac_regs.data()), DACRegisterCount);
	put32(p, uint32_t(m_shadow.size()));
	for (const auto& reg : m_shadow)
	{
		put32(p, reg.first);
		put32(p, reg.second);
	}
	p.write(reinterpret_cast<const char*>(m_vga_memory.data()), VGAMemorySize);
	for (const uint32_t c : m_color)
		put32(p, c);
	for (const uint32_t word : m_auxiliary)
		put32(p, word);
	p.write(reinterpret_cast<const char*>(m_texture.data()), m_texture.size());
	put32(p, m_dma_irq_pending ? 1 : 0);
	for (const auto& plane : m_planes)
	{
		for (const uint8_t written : plane.clear_written)
			put32(p, written);
		for (const auto& group : plane.clear_colors)
			for (const uint32_t value : group)
				put32(p, value);
	}
	for (const auto& cache : m_clear_cache)
	{
		put32(p, cache.patterned ? 1 : 0);
		for (const uint32_t value : cache.pattern)
			put32(p, value);
	}
	put32(p, m_pending.auxiliary_compare_mask);
	put32(p, m_pending.auxiliary_reference);
	const auto bytes = p.str();
	put32(out, 0x30324952); // RI20
	put32(out, m_color_width == MaxColorWidth ? 21 : 20);
	put32(out, uint32_t(bytes.size()));
	put32(out, crc32(bytes));
	out.write(bytes.data(), bytes.size());
	if (!out)
		throw std::runtime_error("PowerStorm snapshot write failed");
}

void CRealImage2100::RestoreState(std::istream& in)
{
	const auto magic = get32(in), version = get32(in);
	if (magic != 0x30324952 || version < 9 || version > 21)
		throw std::runtime_error("Wrong REALimage snapshot version");
	const bool large = version == 11 || version == 13 || version == 15 || version == 17 || version == 19 || version == 21,
		packed_auxiliary = version >= 12;
	if (large != (m_color_width == MaxColorWidth))
		throw std::runtime_error("REALimage snapshot framebuffer mismatch");
	const uint32_t fixed_payload = FixedPayload + (version >= 10 ? 24 : 0) +
		(large ? (MaxColorPixels - ColorPixels) * 9 +
			PlaneCount * (PlaneRegisterCount - LegacyPlaneRegisterCount) * 4 : 0) +
		(packed_auxiliary ? m_color_pixels * 3 + 12 : 0) + (version >= 14 ? 4 : 0) +
		(version >= 18 ? PatternStateSize : 0) + (version >= 20 ? 8 : 0);
	const auto size = get32(in), crc = get32(in);
	if (size < fixed_payload || size > MaxStateSize - 16)
		throw std::runtime_error("Invalid REALimage snapshot length");
	std::string bytes(size, '\0');
	in.read(&bytes[0], size);
	if (!in || crc32(bytes) != crc)
		throw std::runtime_error("REALimage snapshot truncated/corrupt");
	std::istringstream p(bytes, std::ios::binary);
	const auto index = get32(p), dac = get32(p), rd = get32(p), wr = get32(p);
	if (dac > 65535 || rd > 255 || wr > 255)
		throw std::runtime_error("Invalid REALimage snapshot selectors");
	const auto unit_reset = get32(p), interrupt_enable = get32(p),
			   vga_control = get32(p), display_control = get32(p),
			   status_phase = get32(p), frame_counter = get32(p),
			   board_control = get32(p), board_io = get32(p),
			   board_timing = get32(p);
	if (status_phase >= StatusFrameReads || frame_counter > 0xffff ||
		board_control > 0xff || board_io > 0xffffff || board_timing > 0xff)
		throw std::runtime_error("Invalid REALimage status state");
	const uint32_t dac_component = get32(p);
	Pending pending;
	pending.x = get32(p);
	pending.y = get32(p);
	pending.width = get32(p);
	pending.height = get32(p);
	pending.word = get32(p);
	pending.banks = get32(p);
	const uint64_t words = uint64_t(pending.width) * pending.height;
	if (dac_component > 2 || pending.x > 65535 || pending.y > 65535 ||
		pending.width > 65536 || pending.height > 65536 ||
		(pending.width && (!pending.height || words > 0xffffffffu ||
			pending.word >= words || !pending.banks || (pending.banks & ~3u))) ||
		(!pending.width && (pending.x || pending.y || pending.height ||
			pending.word || pending.banks)))
		throw std::runtime_error("Invalid REALimage native transfer state");
	const uint32_t texture_size = get32(p);
	if (texture_size != MinTextureSize && texture_size != MaxTextureSize)
		throw std::runtime_error("Invalid REALimage texture memory size");
	std::array<uint32_t, DMARegisterCount> dma_regs{};
	for (uint32_t& reg : dma_regs)
		reg = get32(p);
	std::array<PlaneState, PlaneCount> planes{};
	for (auto& plane : planes)
	{
		plane.written = get32(p);
		plane.unknown_masks = get32(p);
		if ((plane.written & ~(large ? 0xffffffffu : 0x0fffffffu)) ||
			plane.unknown_masks > (large ? 255u : 15u))
			throw std::runtime_error("Invalid REALimage plane register mask");
		const unsigned registers = large ? PlaneRegisterCount : LegacyPlaneRegisterCount;
		for (unsigned i = 0; i < registers; ++i)
		{
			plane.regs[i] = get32(p);
			if (!(plane.written & (1u << i)) && plane.regs[i])
				throw std::runtime_error("Invalid REALimage plane register state");
		}
	}
	std::array<ClearCache, 3> clear_cache{};
	for (unsigned bank = 0; bank < (packed_auxiliary ? 3u : 2u); ++bank)
	{
		auto& cache = clear_cache[bank];
		cache.source = get32(p);
		cache.color = get32(p);
		cache.known = get32(p);
		if ((cache.source & ~0x07ff07ffu) ||
			(version < 16 && bank < 2 && (cache.known & 0xff000000)) ||
			(cache.color & ~cache.known) || (!cache.known && cache.source))
			throw std::runtime_error("Invalid REALimage clear cache");
	}
	Pending readback;
	if (version >= 10)
	{
		readback.x = get32(p);
		readback.y = get32(p);
		readback.width = get32(p);
		readback.height = get32(p);
		readback.word = get32(p);
		readback.banks = get32(p);
		if ((readback.width && (pending.width || !readback.height ||
			readback.x >= m_color_width || readback.y >= m_color_height ||
			readback.width > m_color_width - readback.x ||
			readback.height > m_color_height - readback.y ||
			readback.word >= uint64_t(readback.width) * readback.height ||
			(readback.banks != 1 && readback.banks != 2))) ||
			(!readback.width && (readback.x || readback.y || readback.height ||
				readback.word || readback.banks)))
			throw std::runtime_error("Invalid REALimage readback state");
	}
	std::array<uint32_t, 256> pal{};
	for (auto& c : pal)
	{
		c = get32(p);
		if (c & 0xff000000)
			throw std::runtime_error("Invalid palette encoding");
	}
	std::vector<uint8_t> dac_regs(DACRegisterCount);
	p.read(reinterpret_cast<char*>(dac_regs.data()), dac_regs.size());
	const uint32_t count = get32(p);
	if (count > MaxShadowRegisters ||
		fixed_payload + uint64_t(count) * 8 + texture_size - MinTextureSize != size)
		throw std::runtime_error("Invalid REALimage shadow register count");
	std::map<uint32_t, uint32_t> shadow;
	for (uint32_t i = 0; i < count; ++i)
	{
		const uint32_t address = get32(p), value = get32(p);
		if ((address & 3) || !shadow.emplace(address, value).second)
			throw std::runtime_error("Invalid REALimage shadow register");
	}
	std::vector<uint8_t> memory(VGAMemorySize);
	p.read(reinterpret_cast<char*>(memory.data()), memory.size());
	std::vector<uint32_t> color(m_color_pixels * 2);
	for (uint32_t& c : color)
	{
		c = get32(p);
		if (version < 16 && (c & 0xff000000))
			throw std::runtime_error("Invalid REALimage color buffer");
	}
	std::vector<uint32_t> auxiliary(m_color_pixels);
	for (uint32_t& word : auxiliary)
	{
		if (packed_auxiliary)
			word = get32(p);
		else
		{
			const int id = p.get();
			if (id < 0 || id > 15)
				throw std::runtime_error("Invalid REALimage window ID buffer");
			word = uint32_t(id) << 12;
		}
	}
	std::vector<uint8_t> texture(texture_size);
	p.read(reinterpret_cast<char*>(texture.data()), texture.size());
	const uint32_t dma_irq_pending = version >= 14 ? get32(p) : 0;
	if (dma_irq_pending > 1)
		throw std::runtime_error("Invalid REALimage DMA interrupt state");
	if (version >= 18)
	{
		const unsigned groups = large ? 8 : 4;
		for (auto& plane : planes)
		{
			for (unsigned group = 0; group < 8; ++group)
			{
				const uint32_t written = get32(p);
				if (written > 255 || (group >= groups && written))
					throw std::runtime_error("Invalid REALimage clear latch mask");
				plane.clear_written[group] = uint8_t(written);
			}
			for (unsigned group = 0; group < 8; ++group)
				for (unsigned word = 0; word < 8; ++word)
				{
					const uint32_t value = get32(p);
					if (!(plane.clear_written[group] & (1u << word)) && value)
						throw std::runtime_error("Invalid REALimage clear latch state");
					plane.clear_colors[group][word] = value;
				}
		}
		const auto draw = shadow.find(DrawControl);
		const uint32_t format = draw == shadow.end() ? 0 : draw->second & 255,
			width = format == 2 ? 8 : format == 3 && large ? 16 : 0;
		for (auto& cache : clear_cache)
		{
			const uint32_t patterned = get32(p);
			if (patterned > 1 || (patterned && (!width || !cache.known)))
				throw std::runtime_error("Invalid REALimage clear pattern state");
			cache.patterned = patterned != 0;
			bool uniform = true;
			for (unsigned i = 0; i < cache.pattern.size(); ++i)
			{
				const uint32_t value = get32(p);
				if ((!patterned || i >= width * 4) ? value != 0 : (value & ~cache.known) != 0)
					throw std::runtime_error("Invalid REALimage clear pattern value");
				if (i < width * 4 && value != cache.color)
					uniform = false;
				cache.pattern[i] = value;
			}
			if (patterned && (cache.pattern[0] != cache.color || uniform))
				throw std::runtime_error("Invalid REALimage clear pattern encoding");
		}
	}
	if (version >= 20)
	{
		pending.auxiliary_compare_mask = get32(p);
		pending.auxiliary_reference = get32(p);
		if ((pending.auxiliary_reference & ~pending.auxiliary_compare_mask) ||
			(pending.auxiliary_compare_mask && (pending.auxiliary_compare_mask != 0x8000 ||
				!pending.width || (pending.banks != 1 && pending.banks != 2) ||
				pending.x >= m_color_width || pending.y >= m_color_height ||
				pending.width > m_color_width - pending.x ||
				pending.height > m_color_height - pending.y)))
			throw std::runtime_error("Invalid REALimage upload comparison state");
	}
	if (!p || p.peek() != std::char_traits<char>::eof())
		throw std::runtime_error("Invalid REALimage snapshot payload");
	// Commit only after validation; keep the storage vga_device borrows in place.
	std::copy(memory.begin(), memory.end(), m_vga_memory.begin());
	m_palette = pal;
	m_dac_regs.swap(dac_regs);
	m_shadow.swap(shadow);
	m_color.swap(color);
	m_auxiliary.swap(auxiliary);
	m_texture.swap(texture);
	m_dma_regs = dma_regs;
	m_dma_irq_pending = dma_irq_pending != 0;
	m_planes = planes;
	m_clear_cache = clear_cache;
	m_pending = pending;
	m_readback = readback;
	m_warned.clear();
	m_aperture_warned = m_shadow_full_warned = false;
	m_unit_reset = unit_reset;
	m_interrupt_enable = interrupt_enable;
	m_vga_control = vga_control;
	m_display_control = display_control;
	m_status_phase = status_phase;
	m_status_mask = ~0u;
	m_frame_counter = uint16_t(frame_counter);
	m_board_control = uint8_t(board_control);
	m_board_io = board_io;
	m_board_timing = uint8_t(board_timing);
	m_io_index = index;
	m_dac_index = uint16_t(dac);
	m_dac_component = uint8_t(dac_component);
	m_palette_read = uint8_t(rd);
	m_palette_write = uint8_t(wr);
	update_irq();
}
