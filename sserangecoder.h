// sserangecoder.h
// SSE 4.1 Interleaved Range Coding example with an 8-bit alphabet, Richard Geldreich, Jr., public domain (see full text at unlicense.org)
#pragma once
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <vector>
#include <assert.h>
#include <memory.h>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

#include <smmintrin.h>

#ifndef _MSC_VER
#define sser_forceinline __attribute__((always_inline))
#else
#define sser_forceinline __forceinline
#endif

namespace sserangecoder
{
	typedef std::vector<uint8_t> uint8_vec;
	typedef std::vector<uint32_t> uint32_vec;

	template <typename S> inline S clamp(S value, S low, S high) { return (value < low) ? low : ((value > high) ? high : value); }
	template <typename T> inline void clear_obj(T& obj) { memset(&obj, 0, sizeof(obj)); }
	
	const uint32_t cRangeCodecMinSyms = 2, cRangeCodecMaxSyms = 256;
	const uint32_t cRangeCodecMinLen = 0x00010000U, cRangeCodecMaxLen = 0x00FFFFFFU;
	const uint32_t cRangeCodecProbBits = 12;
	const uint32_t cRangeCodecProbScale = 1 << cRangeCodecProbBits;

	const uint32_t LANES = 16;
	const uint32_t LANE_MASK = LANES - 1;

	static uint32_t g_num_bytes[256];
	static __m128i g_shift_shuf[256];
	static __m128i g_dist_shuf[256];
	static __m128i g_byte_shuffle_mask;

	void vrange_init();
	
	// Scalar range encoder
	class range_enc
	{
	public:
		range_enc()	{ init(); }

		void init()
		{
			m_arith_base = 0;
			m_arith_length = cRangeCodecMaxLen;
			m_buf.resize(0);
			m_buf.reserve(4096);
		}

		inline void enc_val(uint32_t low_prob, uint32_t high_prob)
		{
			assert((low_prob < high_prob) && (high_prob <= cRangeCodecProbScale));
			assert((high_prob - low_prob) < cRangeCodecProbScale);

			uint32_t l = low_prob * (m_arith_length >> cRangeCodecProbBits);
			uint32_t h = high_prob * (m_arith_length >> cRangeCodecProbBits);

			uint32_t orig_base = m_arith_base;

			m_arith_base = (m_arith_base + l) & cRangeCodecMaxLen;
			m_arith_length = h - l;

			if (orig_base > m_arith_base)
				propagate_carry();

			if (m_arith_length < cRangeCodecMinLen)
				renorm_enc_interval();
		}

		void flush();
		
		const uint8_vec& get_buf() const { return m_buf; }
		uint8_vec& get_buf() { return m_buf; }

	private:
		uint32_t m_arith_base, m_arith_length;
		uint8_vec m_buf;

		inline void propagate_carry()
		{
			if (!m_buf.size())
				return;

			size_t index = m_buf.size() - 1;

			for (; ; )
			{
				uint8_t& c = m_buf[index];

				if (c == 0xFF)
					c = 0;
				else
				{
					c++;
					break;
				}

				if (!index)
					break;

				index--;
			}
		}

		inline void renorm_enc_interval()
		{
			assert((m_arith_base & (~cRangeCodecMaxLen)) == 0);

			do
			{
				m_buf.push_back((uint8_t)(m_arith_base >> 16));

				m_arith_base = (m_arith_base << 8) & cRangeCodecMaxLen;
				m_arith_length <<= 8;

			} while (m_arith_length < cRangeCodecMinLen);
		}
	};

	// Scalar range decoder
	class range_dec
	{
	public:
		range_dec()
		{
			assert(cRangeCodecProbBits == 12);

			clear();
		}

		void clear()
		{
			m_arith_length = 0;
			m_arith_value = 0;
		}

		void init(const uint8_t*& pBuf)
		{
			m_arith_length = cRangeCodecMaxLen;

			m_arith_value = 0;
			m_arith_value |= (pBuf[0] << 16);
			m_arith_value |= (pBuf[1] << 8);
			m_arith_value |= pBuf[2];
			pBuf += 3;
		}

		inline uint32_t dec_sym(const uint32_t* pTable, const uint8_t*& pCur_buf)
		{
			const uint32_t r = (m_arith_length >> cRangeCodecProbBits);

			uint32_t q = m_arith_value / r;
			
			// AND is for safety in case the input stream is corrupted, it's not stricly necessary if you know it can't be
			uint32_t encoded_val = pTable[q & (cRangeCodecProbScale - 1)];

			uint32_t sym = encoded_val & 255;

			uint32_t low_prob = (encoded_val >> 8) & (cRangeCodecProbScale - 1);
			uint32_t prob_range = (encoded_val >> (8 + 12));

			assert(q >= low_prob && (q < (low_prob + prob_range)));

			uint32_t l = low_prob * r;

			m_arith_value -= l;
			m_arith_length = prob_range * r;

			// Reads [0,2] bytes
			while (m_arith_length < cRangeCodecMinLen)
			{
				uint32_t c = *pCur_buf++;
				m_arith_value = (m_arith_value << 8) | c;
				m_arith_length <<= 8;
			}

			return sym;
		}

		uint32_t m_arith_length, m_arith_value;
	};

	// Create lookup table for the vectorized range decoder
	void vrange_init_table(uint32_t num_syms, const uint32_vec& scaled_cum_prob, uint32_vec& table);
	
	// freq may be modified if the number of used syms was 1
	bool vrange_create_cum_probs(uint32_vec& scaled_cum_prob, uint32_vec& freq);
	
	// Decode 4 symbols from 4 range encoded streams using the specified lookup table
	static sser_forceinline uint32_t vrange_decode(__m128i& arith_value, __m128i& arith_length, const uint32_t* pTable)
	{
		__m128i r = _mm_srli_epi32(arith_length, cRangeCodecProbBits);

		// The float divide is safe because arith_value is always <= 24 bits. (Thanks to Jan Wassenberg for suggesting _mm_cvttps_epi32() vs. _mm_cvtps_epi32() and using the rounding mode here.)
		__m128i q = _mm_cvttps_epi32(_mm_div_ps(_mm_cvtepi32_ps(arith_value), _mm_cvtepi32_ps(r)));
				
		// Sanity check for bugs or corrupted data
		assert(_mm_extract_epi32(q, 0) < 4096 && _mm_extract_epi32(q, 1) < 4096 && _mm_extract_epi32(q, 2) < 4096 && _mm_extract_epi32(q, 3) < 4096);

		// AND against table size mask only needed for safety from corrupted data, normally does nothing.
		q = _mm_and_si128(q, _mm_set1_epi32(4095));

		uint32_t q1 = _mm_cvtsi128_si32(q);
		uint32_t q2 = _mm_extract_epi32(q, 1);
		uint32_t q3 = _mm_extract_epi32(q, 2);
		uint32_t q4 = _mm_extract_epi32(q, 3);

		uint32_t encoded_val1 = pTable[q1];
		uint32_t encoded_val2 = pTable[q2];
		uint32_t encoded_val3 = pTable[q3];
		uint32_t encoded_val4 = pTable[q4];

		__m128i e = _mm_cvtsi32_si128(encoded_val1);
		e = _mm_insert_epi32(e, encoded_val2, 1);
		e = _mm_insert_epi32(e, encoded_val3, 2);
		e = _mm_insert_epi32(e, encoded_val4, 3);

		__m128i bytes = _mm_shuffle_epi8(e, g_byte_shuffle_mask);
		uint32_t syms = _mm_cvtsi128_si32(bytes);

		__m128i low_prob = _mm_and_si128(_mm_srli_epi32(e, 8), _mm_set1_epi32(cRangeCodecProbScale - 1));
		__m128i prob_range = _mm_srli_epi32(e, 20);

		arith_value = _mm_sub_epi32(arith_value, _mm_mullo_epi32(low_prob, r));
		arith_length = _mm_mullo_epi32(prob_range, r);

		return syms;
	}

	// Normalize 4 range encoders, fetching up to 2 bytes per stream (or 8 total bytes) from pSrc
	static sser_forceinline void vrange_normalize(__m128i& arith_value, __m128i& arith_length, const uint8_t*& pSrc)
	{
		__m128i cmp_mask0 = _mm_cmplt_epi32(arith_length, _mm_set1_epi32(cRangeCodecMinLen));
		__m128i cmp_mask1 = _mm_cmplt_epi32(arith_length, _mm_set1_epi32(256));

		uint32_t msk_bits0 = _mm_movemask_ps(_mm_castsi128_ps(cmp_mask0));
		uint32_t msk_bits1 = _mm_movemask_ps(_mm_castsi128_ps(cmp_mask1));
		uint32_t msk_bits = msk_bits0 | (msk_bits1 << 4);

		__m128i src_bytes = _mm_loadl_epi64((const __m128i*)pSrc);

		__m128i shift = g_shift_shuf[msk_bits];
		__m128i dist = g_dist_shuf[msk_bits];

		arith_value = _mm_or_si128(_mm_shuffle_epi8(arith_value, shift), _mm_shuffle_epi8(src_bytes, dist));
		arith_length = _mm_shuffle_epi8(arith_length, shift);

		pSrc += g_num_bytes[msk_bits];
	}

	// Encodes file_data to 16 interleaved range coded streams
	void vrange_encode(const uint8_vec& file_data, uint8_vec& enc_buf, const uint32_vec& scaled_cum_prob);
		
	// Decodes interleaved data created by vrange_encode()
	bool vrange_decode(const uint8_t* pSrc_start, size_t comp_size, uint8_t* pDst_start, size_t orig_size, const uint32_t* pDec_table);
	
} // sserangecoder
