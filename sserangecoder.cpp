// sserangecoder.cpp
// SSE 4.1 Interleaved Range Coding example with an 8-bit alphabet, Richard Geldreich, Jr., public domain (see full text at unlicense.org)
#include "sserangecoder.h"

#ifdef _MSC_VER
#pragma warning(disable:4310) // warning C4310: cast truncates constant value
#endif

namespace sserangecoder
{
	void vrange_init()
	{
		g_byte_shuffle_mask = _mm_set_epi8((char)0x80, (char)0x80, (char)0x80, (char)0x80,
			(char)0x80, (char)0x80, (char)0x80, (char)0x80,
			(char)0x80, (char)0x80, (char)0x80, (char)0x80,
			12, 8, 4, 0);

		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t num_bytes = 0;

			for (uint32_t j = 0; j < 4; j++)
			{
				if ((i >> j) & 0x10)
					num_bytes += 2;
				else if ((i >> j) & 1)
					num_bytes++;
			}

			g_num_bytes[i] = num_bytes;

			uint8_t x[16];

			for (uint32_t j = 0; j < 4; j++)
			{
				if ((i >> j) & 0x10)
				{
					x[j * 4 + 0] = 0x80;
					x[j * 4 + 1] = 0x80;
					x[j * 4 + 2] = (uint8_t)(j * 4 + 0);
					x[j * 4 + 3] = (uint8_t)(j * 4 + 1);
				}
				else if ((i >> j) & 1)
				{
					x[j * 4 + 0] = 0x80;
					x[j * 4 + 1] = (uint8_t)(j * 4 + 0);
					x[j * 4 + 2] = (uint8_t)(j * 4 + 1);
					x[j * 4 + 3] = (uint8_t)(j * 4 + 2);
				}
				else
				{
					x[j * 4 + 0] = (uint8_t)(j * 4 + 0);
					x[j * 4 + 1] = (uint8_t)(j * 4 + 1);
					x[j * 4 + 2] = (uint8_t)(j * 4 + 2);
					x[j * 4 + 3] = (uint8_t)(j * 4 + 3);
				}
			}
			g_shift_shuf[i] = _mm_loadu_si128((__m128i *)&x);

			uint32_t src_ofs = 0;
			for (uint32_t j = 0; j < 4; j++)
			{
				if ((i >> j) & 0x10)
				{
					x[j * 4 + 0] = (uint8_t)(src_ofs + 1);
					x[j * 4 + 1] = (uint8_t)(src_ofs);
					x[j * 4 + 2] = 0x80;
					x[j * 4 + 3] = 0x80;
					src_ofs += 2;
				}
				else if ((i >> j) & 1)
				{
					x[j * 4 + 0] = (uint8_t)(src_ofs++);
					x[j * 4 + 1] = 0x80;
					x[j * 4 + 2] = 0x80;
					x[j * 4 + 3] = 0x80;
				}
				else
				{
					x[j * 4 + 0] = 0x80;
					x[j * 4 + 1] = 0x80;
					x[j * 4 + 2] = 0x80;
					x[j * 4 + 3] = 0x80;
				}
			}

			g_dist_shuf[i] = _mm_loadu_si128((__m128i *)&x);
		}
	}

	void range_enc::flush()
	{
		uint32_t orig_base = m_arith_base;

		if (m_arith_length > 2 * cRangeCodecMinLen)
		{
			m_arith_base = (m_arith_base + cRangeCodecMinLen) & cRangeCodecMaxLen;
			m_arith_length = (cRangeCodecMinLen >> 1);
		}
		else
		{
			m_arith_base = (m_arith_base + (cRangeCodecMinLen >> 1)) & cRangeCodecMaxLen;
			m_arith_length = (cRangeCodecMinLen >> 9);
		}

		if (orig_base > m_arith_base)
			propagate_carry();

		renorm_enc_interval();

		while (m_buf.size() < 3)
			m_buf.push_back(0);

		for (uint32_t i = 0; i < 2; i++)
			m_buf.push_back(0);
	}

	// Create lookup table for the vectorized range decoder
	void vrange_init_table(uint32_t num_syms, const uint32_vec& scaled_cum_prob, uint32_vec& table)
	{
		table.resize(cRangeCodecProbScale);
		assert(scaled_cum_prob.size() == (num_syms + 1));

		for (uint32_t sym_index = 0; sym_index < num_syms; sym_index++)
		{
			const uint32_t n = scaled_cum_prob[sym_index + 1] - scaled_cum_prob[sym_index];
			if (!n)
				continue;

			assert(scaled_cum_prob[sym_index] < cRangeCodecProbScale);
			assert((scaled_cum_prob[sym_index + 1] - scaled_cum_prob[sym_index]) < cRangeCodecProbScale);

			const uint32_t k = sym_index | (scaled_cum_prob[sym_index] << 8) | ((scaled_cum_prob[sym_index + 1] - scaled_cum_prob[sym_index]) << 20);

			uint32_t* pDst = &table[scaled_cum_prob[sym_index]];
			for (uint32_t j = 0; j < n; j++)
				*pDst++ = k;
		}
	}

	// freq may be modified if the number of used syms was 1
	bool vrange_create_cum_probs(uint32_vec& scaled_cum_prob, uint32_vec& freq)
	{
		const uint32_t num_syms = (uint32_t)freq.size();
		assert((num_syms >= cRangeCodecMinSyms) && (num_syms <= cRangeCodecMaxSyms));

		if ((num_syms < cRangeCodecMinSyms) || (num_syms > cRangeCodecMaxSyms))
			return false;

		uint64_t total_freq = 0;
		uint32_t total_used_syms = 0;
		for (uint32_t i = 0; i < num_syms; i++)
		{
			total_freq += freq[i];
			if (freq[i])
				total_used_syms++;
		}

		assert(total_used_syms >= 1);
		if (!total_used_syms)
			return false;

		if (total_used_syms == 1)
		{
			for (uint32_t i = 0; i < num_syms; i++)
			{
				if (!freq[i])
				{
					freq[i]++;
					total_freq++;
					break;
				}
			}

			total_used_syms++;
		}

		assert((total_used_syms >= 2) && (total_freq >= 2));

		scaled_cum_prob.resize(num_syms + 1);

		uint32_t sym_index_to_boost = 0, boost_amount = 0;

		uint32_t adjusted_prob_scale = cRangeCodecProbScale;
		for (; ; )
		{
			// Count how many used symbols would get truncated to a frequency of 0 
			// These symbols could cause the total frequency to be too large, because they get assigned a minimum frequency of 1 (not 0)
			uint32_t num_truncated_syms = 0;
			for (uint32_t i = 0; i < num_syms; i++)
			{
				if (freq[i])
				{
					uint32_t l = (uint32_t)(((uint64_t)freq[i] * adjusted_prob_scale) / total_freq);
					if (!l)
						num_truncated_syms++;
				}
			}

			// If no symbols get a truncated freq of 0 then our scale is good
			if (!num_truncated_syms)
				break;

			// Compute new lower scale, compensating for the # of symbols which get a boosted freq of 1
			uint32_t new_adjusted_prob_scale = cRangeCodecProbScale - num_truncated_syms;
			if (new_adjusted_prob_scale == adjusted_prob_scale)
				break;

			// The prob scale is now lower, so recount how many symbols get truncated. This can't loop forever, because num_truncated_syms can only go so high (255)
			adjusted_prob_scale = new_adjusted_prob_scale;
		}

		for (uint32_t pass = 0; pass < 2; pass++)
		{
			uint32_t most_prob_sym_freq = 0, most_prob_sym_index = 0;

			uint32_t ci = 0;
			for (uint32_t i = 0; i < num_syms; i++)
			{
				scaled_cum_prob[i] = ci;

				if (!freq[i])
					continue;

				if (freq[i] > most_prob_sym_freq)
				{
					most_prob_sym_freq = freq[i];
					most_prob_sym_index = i;
				}

				uint32_t l = (uint32_t)(((uint64_t)freq[i] * adjusted_prob_scale) / total_freq);
				l = clamp<uint32_t>(l, 1, cRangeCodecProbScale - (total_used_syms - 1));

				if ((pass) && (i == sym_index_to_boost))
					l += boost_amount;

				ci += l;
				assert(ci <= cRangeCodecProbScale);

				// shouldn't happen
				if (ci > cRangeCodecProbScale)
					return false;
			}
			scaled_cum_prob[num_syms] = cRangeCodecProbScale;
						
			if (ci == cRangeCodecProbScale)
				break;

			// shouldn't happen
			if (pass)
				return false;

			assert(!pass);

			// On first pass and the total frequency isn't cRangeCodecProbScale, so boost the freq of the max used symbol

			sym_index_to_boost = most_prob_sym_index;
			boost_amount = cRangeCodecProbScale - ci;
		}

		return true;
	}

	void vrange_encode(const uint8_vec& file_data, uint8_vec& enc_buf, const uint32_vec& scaled_cum_prob)
	{
		const size_t file_size = file_data.size();
		assert(file_size);

		range_enc encs[LANES];
		uint8_vec bytes_written(file_size);
		uint64_t total_enc_size = 0;
				
		for (uint32_t i = 0; i < LANES; i++)
			encs[i].get_buf().reserve(1 + (file_size / LANES));

		for (size_t i = 0; i < file_size; i++)
		{
			const uint32_t sym = file_data[i];
			const uint32_t lane = i & LANE_MASK;

			const size_t cur_enc_size = encs[lane].get_buf().size();

			encs[lane].enc_val(scaled_cum_prob[sym], scaled_cum_prob[sym + 1]);

			const uint32_t enc_bytes = (uint32_t)(encs[lane].get_buf().size() - cur_enc_size);

			bytes_written[i] = (uint8_t)(enc_bytes);
			total_enc_size += enc_bytes;
		}

		for (uint32_t lane = 0; lane < LANES; lane++)
			encs[lane].flush();

		uint32_t cur_ofs[16];
		clear_obj(cur_ofs);

		const uint64_t final_enc_buf_size = LANES * 3 + total_enc_size + 2;

		enc_buf.resize((size_t)final_enc_buf_size);

		uint8_t* pDst_enc_buf = &enc_buf[0];

		for (uint32_t lane = 0; lane < LANES; lane++)
		{
			for (uint32_t j = 0; j < 3; j++)
			{
				*pDst_enc_buf++ = encs[lane].get_buf()[cur_ofs[lane]];
				cur_ofs[lane]++;
			}
		}

		for (size_t i = 0; i < file_size; i++)
		{
			const uint32_t num_bytes = bytes_written[i];

			if (num_bytes)
			{
				const uint32_t lane = i & LANE_MASK;
				const uint8_vec& src_bytes = encs[lane].get_buf();

				memcpy(pDst_enc_buf, &src_bytes[cur_ofs[lane]], num_bytes);
				pDst_enc_buf += num_bytes;

				cur_ofs[lane] += num_bytes;
			}
		}

		for (uint32_t i = 0; i < 2; i++)
			*pDst_enc_buf++ = 0;

		assert(pDst_enc_buf - &enc_buf[0] == enc_buf.size());
	}

	static sser_forceinline uint32_t read_be24(const uint8_t*& pSrc)
	{
		const uint32_t res = (pSrc[0] << 16) | (pSrc[1] << 8) | pSrc[2];
		pSrc += 3;
		return res;
	}

	bool vrange_decode(const uint8_t *pSrc_start, size_t comp_size, uint8_t *pDst_start, size_t orig_size, const uint32_t *pDec_table)
	{
		const uint8_t* pSrc = pSrc_start;

		__m128i arith_value0, arith_value1, arith_value2, arith_value3;
		__m128i arith_length0 = _mm_set1_epi32(cRangeCodecMaxLen), arith_length1 = _mm_set1_epi32(cRangeCodecMaxLen), 
			arith_length2 = _mm_set1_epi32(cRangeCodecMaxLen), arith_length3 = _mm_set1_epi32(cRangeCodecMaxLen);

		__m128i* arith_lens[4] = { &arith_length0, &arith_length1, &arith_length2, &arith_length3 };
		__m128i* arith_vals[4] = { &arith_value0, &arith_value1, &arith_value2, &arith_value3 };

		for (uint32_t vec_index = 0; vec_index < 4; vec_index++)
		{
			__m128i x = _mm_cvtsi32_si128(read_be24(pSrc));
			x = _mm_insert_epi32(x, read_be24(pSrc), 1);
			x = _mm_insert_epi32(x, read_be24(pSrc), 2);
			x = _mm_insert_epi32(x, read_be24(pSrc), 3);
			*arith_vals[vec_index] = x;
		}
						
		const uint8_t* pSrc_end = pSrc + comp_size;
		size_t dst_ofs = 0;
		
		uint32_t* pDst32 = (uint32_t*)pDst_start;

		// Vectorized decode
		for (dst_ofs = 0; ((dst_ofs + LANES) <= orig_size) && (pSrc + 8*4) <= pSrc_end; dst_ofs += LANES)
		{
			pDst32[0] = vrange_decode(arith_value0, arith_length0, pDec_table);
			pDst32[1] = vrange_decode(arith_value1, arith_length1, pDec_table);
			pDst32[2] = vrange_decode(arith_value2, arith_length2, pDec_table);
			pDst32[3] = vrange_decode(arith_value3, arith_length3, pDec_table);

			pDst32 += 4;

			vrange_normalize(arith_value0, arith_length0, pSrc);
			vrange_normalize(arith_value1, arith_length1, pSrc);
			vrange_normalize(arith_value2, arith_length2, pSrc);
			vrange_normalize(arith_value3, arith_length3, pSrc);
		}
				
		// Finish the end with scalar code
		range_dec scalar_dec;
		while (dst_ofs < orig_size)
		{
			// This check can never be true on valid inputs - the end is always padded.
			if ((pSrc + 2) > pSrc_end)
				return false;

			const uint32_t vec_index = (dst_ofs & LANE_MASK) >> 2;
			const uint32_t vec_lane = dst_ofs & 3;

			scalar_dec.m_arith_length = ((const uint32_t *)arith_lens[vec_index])[vec_lane];
			scalar_dec.m_arith_value = ((const uint32_t *)arith_vals[vec_index])[vec_lane];
						
			uint32_t sym = scalar_dec.dec_sym(pDec_table, pSrc);

			pDst_start[dst_ofs++] = (uint8_t)sym;

			((uint32_t *)arith_lens[vec_index])[vec_lane] = scalar_dec.m_arith_length;
			((uint32_t *)arith_vals[vec_index])[vec_lane] = scalar_dec.m_arith_value;
		}

		size_t bytes_read = pSrc - pSrc_start;
		if (bytes_read > comp_size)
			return false;

		return true;
	}

} // namespace sserangecoder


