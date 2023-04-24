// SSE 4.1 Interleaved Range Coding example with an 8-bit alphabet, Richard Geldreich, Jr., public domain (see full text at unlicense.org)
// Simple test app with 3 modes (compression/decompression testing, compression, or decompression)
#include "sserangecoder.h"
#include <stdarg.h>
#include <time.h>
#include <math.h>

// The CRC-32 check is so slow it's the bottleneck in this app during decompression (using the 'd' mode command), ignoring file I/O.
// Disable if you only want to benchmark the decompressor (and file I/O) and not the slow CRC-32.
#define DECOMP_CRC32_CHECKING 1

// Package merge is only used for efficiency comparison purposes against Huffman coding, it's not used by the range coder.
#include "packagemerge.h"

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma warning (disable:4127) // warning C4127: conditional expression is constant
#endif

using namespace sserangecoder;

typedef std::vector<float> float_vec;

typedef uint64_t timer_ticks;

#if defined(_WIN32)
inline void query_counter(timer_ticks* pTicks)
{
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(pTicks));
}
inline void query_counter_frequency(timer_ticks* pTicks)
{
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(pTicks));
}
#elif defined(__APPLE__)
#include <sys/time.h>
inline void query_counter(timer_ticks* pTicks)
{
	struct timeval cur_time;
	gettimeofday(&cur_time, NULL);
	*pTicks = static_cast<unsigned long long>(cur_time.tv_sec) * 1000000ULL + static_cast<unsigned long long>(cur_time.tv_usec);
}
inline void query_counter_frequency(timer_ticks* pTicks)
{
	*pTicks = 1000000;
}
#elif defined(__GNUC__)
#include <sys/timex.h>
inline void query_counter(timer_ticks* pTicks)
{
	struct timeval cur_time;
	gettimeofday(&cur_time, NULL);
	*pTicks = static_cast<unsigned long long>(cur_time.tv_sec) * 1000000ULL + static_cast<unsigned long long>(cur_time.tv_usec);
}
inline void query_counter_frequency(timer_ticks* pTicks)
{
	*pTicks = 1000000;
}
#else
#error TODO
#endif

static uint64_t get_clock()
{
	uint64_t res;
	query_counter(&res);
	return res;
}

static uint64_t get_ticks_per_sec()
{
	uint64_t res;
	query_counter_frequency(&res);
	return res;
}

static void panic(const char* pMsg, ...)
{
	fprintf(stderr, "ERROR: ");

	va_list args;
	va_start(args, pMsg);
	vfprintf(stderr, pMsg, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

static bool read_file_to_vec(const char* pFilename, uint8_vec& data)
{
	FILE* pFile = nullptr;
#ifdef _WIN32
	fopen_s(&pFile, pFilename, "rb");
#else
	pFile = fopen(pFilename, "rb");
#endif
	if (!pFile)
		return false;

	fseek(pFile, 0, SEEK_END);
#ifdef _WIN32
	int64_t filesize = _ftelli64(pFile);
#else
	int64_t filesize = ftello(pFile);
#endif
	if (filesize < 0)
	{
		fclose(pFile);
		return false;
	}
	fseek(pFile, 0, SEEK_SET);

	if (sizeof(size_t) == sizeof(uint32_t))
	{
		if (filesize > 0x70000000)
		{
			// File might be too big to load safely in one alloc
			fclose(pFile);
			return false;
		}
	}

	data.resize((size_t)filesize);

	if (filesize)
	{
		if (fread(&data[0], 1, (size_t)filesize, pFile) != (size_t)filesize)
		{
			fclose(pFile);
			return false;
		}
	}

	fclose(pFile);
	return true;
}

static bool write_data_to_file(const char* pFilename, const void* pData, size_t len)
{
	FILE* pFile = nullptr;
#ifdef _WIN32
	fopen_s(&pFile, pFilename, "wb");
#else
	pFile = fopen(pFilename, "wb");
#endif
	if (!pFile)
		return false;

	if (len)
	{
		if (fwrite(pData, 1, len, pFile) != len)
		{
			fclose(pFile);
			return false;
		}
	}

	return fclose(pFile) != EOF;
}

static void test_plain_range_coding(
	const uint8_vec &file_data, 
	const uint32_vec &scaled_cum_prob, 
	const uint32_vec &dec_table, 
	double total_theoretical_bits)
{
	printf("\nTesting plain range coding:\n");

	const uint32_t file_size = (uint32_t)file_data.size();
		
	range_enc enc;
	enc.get_buf().reserve(file_size);

	const uint64_t enc_start_time = get_clock();

	for (uint32_t i = 0; i < file_size; i++)
	{
		uint32_t sym = file_data[i];
		enc.enc_val(scaled_cum_prob[sym], scaled_cum_prob[sym + 1]);
	}
	enc.flush();

	const double total_enc_time = (double)(get_clock() - enc_start_time) / (double)get_ticks_per_sec();

	printf("Total encoding time: %f seconds, %.1f MiB/sec.\n", total_enc_time, ((double)file_size / total_enc_time) / (1024 * 1024));
		
	printf("Compressed file from %zu bytes to %zu bytes, %.3f%% vs. theoretical limit\n", 
		file_data.size(), enc.get_buf().size(), total_theoretical_bits ? enc.get_buf().size() / (total_theoretical_bits / 8.0f) * 100.0f : 0.0f);

	const uint8_t* pCur_buf = &enc.get_buf()[0];

	uint8_vec decoded_buf(file_size);
	memset(&decoded_buf[0], 0xCD, file_size);

	const uint64_t dec_start_time = get_clock();

	range_dec dec;
	dec.init(pCur_buf);
		
	for (uint32_t i = 0; i < file_size; i++)
		decoded_buf[i] = (uint8_t)dec.dec_sym(&dec_table[0], pCur_buf);
	
	const double total_dec_time = (double)(get_clock() - dec_start_time) / (double)get_ticks_per_sec();

	printf("Total decoding time: %f seconds, %.1f MiB/sec.\n", total_dec_time, ((double)file_size / total_dec_time) / (1024 * 1024));

	size_t bytes_read = pCur_buf - &enc.get_buf()[0];
	if (bytes_read > enc.get_buf().size())
		panic("Decompressor read too many bytes!\n");

	if (memcmp(&decoded_buf[0], &file_data[0], file_data.size()) != 0)
		panic("Decompression failed!\n");

	printf("Decompression OK\n");
}

static void test_vectorized_range_coding(
	const uint8_vec& file_data,
	const uint32_vec& scaled_cum_prob,
	const uint32_vec& dec_table,
	double total_theoretical_bits)
{
	printf("\nTesting vectorized interleaved range decoding (encoding is not vectorized):\n");

	const uint32_t file_size = (uint32_t)file_data.size();
	
	const uint64_t enc_start_time = get_clock();

	uint8_vec enc_buf;
	vrange_encode(file_data, enc_buf, scaled_cum_prob);
		
	const double total_enc_time = (double)(get_clock() - enc_start_time) / (double)get_ticks_per_sec();

	printf("Total encoding time: %f, %.1f MiB/sec.\n", total_enc_time, ((double)file_size / total_enc_time) / (1024 * 1024));

	printf("Compressed file from %zu bytes to %zu bytes, %.3f%% vs. theoretical limit\n",
		file_data.size(), enc_buf.size(), total_theoretical_bits ? enc_buf.size() / (total_theoretical_bits / 8.0f) * 100.0f : 0.0f);
	
	uint8_vec decoded_buf(file_size);
	memset(&decoded_buf[0], 0xCD, file_size);

	const uint32_t OUTER_TIMES = 8;
	for (uint32_t r = 0; r < OUTER_TIMES; r++)
	{
		const uint64_t before_time = get_clock();
		uint64_t total_cycles = 0;

#ifdef _DEBUG
		const uint32_t TIMES_TO_DECODE = 1;
#else
		const uint32_t TIMES_TO_DECODE = 100;
#endif
		for (uint32_t times = 0; times < TIMES_TO_DECODE; times++)
		{
			const uint64_t start_cycles = __rdtsc();

			if (!vrange_decode(&enc_buf[0], enc_buf.size(), &decoded_buf[0], file_size, &dec_table[0]))
				panic("vrange_decode() failed!\n");

			total_cycles += __rdtsc() - start_cycles;

		} // times
				
		double total_time = ((double)(get_clock() - before_time) / (double)get_ticks_per_sec()) / TIMES_TO_DECODE;

		if (memcmp(&decoded_buf[0], &file_data[0], file_size) != 0)
			panic("Decompression failed!\n");

		printf("\nDecompression OK\n");

		printf("%.6f seconds, %.1f MiB/sec., %.1f cycles per byte\n", total_time, ((double)file_size / total_time) / (1024*1024), ((double)total_cycles / TIMES_TO_DECODE) / file_size);
	} // r
}

static const char *g_file_sig = "Rc";
const uint32_t TOTAL_HEADER_SIZE = 2 + sizeof(uint32_t) * 3 + 256 * 2;

// Karl Malbrain's compact CRC-32. See "A compact CCITT crc16 and crc32 C implementation that balances processor cache usage against speed": http://www.geocities.com/malbrain/
// This CRC-32 function is quite slow, but it's small.
static uint32_t crc32(uint32_t crc, const uint8_t* ptr, size_t buf_len)
{
	static const uint32_t s_crc32[16] = { 0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
	  0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
	uint32_t crcu32 = (uint32_t)crc;
	if (!ptr) return 0;
	crcu32 = ~crcu32; while (buf_len--) { uint8_t b = *ptr++; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b & 0xF)]; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b >> 4)]; }
	return ~crcu32;
}

static bool interleaved_encode(const uint8_vec& file_data, uint8_vec& comp_data)
{
	const uint32_t file_size = (uint32_t)file_data.size();
	if ((!file_size) || (file_size > UINT32_MAX))
		return false;

	const uint32_t file_data_crc32 = crc32(0, &file_data[0], file_size);

	uint32_vec sym_freq(256);
	for (uint32_t i = 0; i < file_data.size(); i++)
		sym_freq[file_data[i]]++;
	
	uint32_t max_freq = 0;
	for (uint32_t i = 0; i < 256; i++)
		max_freq = std::max<uint32_t>(max_freq, sym_freq[i]);
	
	// Reduce frequencies to 16-bits (hurts efficiency, but reduces the overhead).
	for (uint32_t i = 0; i < 256; i++)
		if (sym_freq[i])
			sym_freq[i] = std::max<uint32_t>(1, (UINT16_MAX * sym_freq[i] + (max_freq / 2)) / max_freq);

	comp_data.resize(0);
	comp_data.reserve(file_data.size());
	
	comp_data.push_back(g_file_sig[0]);
	comp_data.push_back(g_file_sig[1]);

	for (uint32_t i = 0; i < 4; i++)
		comp_data.push_back((uint8_t)(file_size >> (i * 8)) & 0xFF);
	
	const size_t comp_size_ofs = comp_data.size();

	for (uint32_t i = 0; i < 4; i++)
		comp_data.push_back(0);

	for (uint32_t i = 0; i < 4; i++)
		comp_data.push_back((uint8_t)(file_data_crc32 >> (i * 8)) & 0xFF);

	for (uint32_t i = 0; i < 256; i++)
	{
		comp_data.push_back((uint8_t)sym_freq[i]);
		comp_data.push_back((uint8_t)(sym_freq[i] >> 8));
	}

	assert(TOTAL_HEADER_SIZE == comp_data.size());

	// Create the scaled cumulative probability table needed for encoding
	uint32_vec scaled_cum_prob;
	if (!vrange_create_cum_probs(scaled_cum_prob, sym_freq))
		return false;

	// Encode the symbols
	uint8_vec enc_buf;
	vrange_encode(file_data, enc_buf, scaled_cum_prob);
	if (enc_buf.size() > UINT32_MAX)
		return false;
			
	const size_t comp_data_ofs = comp_data.size();
	if (comp_data.size() + enc_buf.size() > UINT32_MAX)
		return false;

	comp_data.resize(comp_data.size() + enc_buf.size());
	memcpy(&comp_data[comp_data_ofs], &enc_buf[0], enc_buf.size());

	for (uint32_t i = 0; i < 4; i++)
		comp_data[comp_size_ofs + i] = (uint8_t)(enc_buf.size() >> (i * 8));

	return true;
}

static bool interleaved_decode(const uint8_vec& comp_data, uint8_vec& decomp_data, uint32_t &expected_crc32)
{
	// Sanity check the input size
	if (comp_data.size() < (TOTAL_HEADER_SIZE + LANES * 3))
		return false;

	// Check for compressed file signature
	if ((comp_data[0] != g_file_sig[0]) || (comp_data[1] != g_file_sig[1]))
		return false;

	const uint32_t orig_size = comp_data[2] | (comp_data[3] << 8) | (comp_data[4] << 16) | (comp_data[5] << 24);
	const uint32_t comp_size = comp_data[6] | (comp_data[7] << 8) | (comp_data[8] << 16) | (comp_data[9] << 24);
	expected_crc32 = comp_data[10] | (comp_data[11] << 8) | (comp_data[12] << 16) | (comp_data[13] << 24);

	// Sanity check the sizes in the header
	if ((!orig_size) || (comp_size < LANES * 3))
		return false;

	if (comp_data.size() < (TOTAL_HEADER_SIZE + comp_size))
		return false;

	// Read the 16-bit symbol frequencies
	uint32_vec sym_freq(256);
	for (uint32_t i = 0; i < 256; i++)
		sym_freq[i] = comp_data[14 + i * 2] | (comp_data[14 + i * 2 + 1] << 8);
		
	// Compute the tables needed for decompression
	uint32_vec scaled_cum_prob;
	if (!vrange_create_cum_probs(scaled_cum_prob, sym_freq))
		return false;
		
	uint32_vec dec_table;
	vrange_init_table(256, scaled_cum_prob, dec_table);
				
	decomp_data.resize(orig_size);
	
	// Decode the symbols
	if (!vrange_decode(&comp_data[TOTAL_HEADER_SIZE], comp_size, &decomp_data[0], orig_size, &dec_table[0]))
		return false;
		
	return true;
}

enum 
{
	cModeTest,
	cModeComp,
	cModeDecomp
};

static void print_usage()
{
	printf("Usage: sserangecoding with no args tests the codec with \"book1\"\n");
	printf("sserangecoding <filename> : Tests compression/decompression on a specific file\n");
	printf("sserangecoding c <source_filename> <comp_filename> : Compresses file\n");
	printf("sserangecoding d <comp_filename> <decomp_filename> : Decompresses file with CRC-32 check\n");
}
	
int main(int argc, char **argv)
{
	vrange_init();

	int mode = cModeTest;
	const char* pSrc_filename = "book1";
	const char* pOut_filename = "outfile";
	
	if (argc == 1)
	{
		// Test mode
	}
	else if ((argc == 2) && (strcmp(argv[1], "-h") == 0))
	{
		print_usage();
		exit(EXIT_FAILURE);
	}
	else if (argc == 2)
	{
		pSrc_filename = argv[1];
	}
	else if (argc == 4)
	{
		if (argv[1][0] == 'c')
			mode = cModeComp;
		else if (argv[1][0] == 'd')
			mode = cModeDecomp;
		else
		{
			print_usage();
			panic("Invalid mode!\n");
		}

		pSrc_filename = argv[2];
		pOut_filename = argv[3];
	}
	else
	{
		print_usage();
		panic("Invalid command line arguments!\n");
	}

	printf("Reading file %s\n", pSrc_filename);

	uint8_vec file_data;
	if (!read_file_to_vec(pSrc_filename, file_data))
		panic("Failed reading source file!\n");

	if (!file_data.size())
		panic("File empty!\n");

	if (file_data.size() >= UINT32_MAX)
		panic("File too big!\n");

	const uint32_t file_size = (uint32_t)file_data.size();

	if (mode == cModeTest)
	{
		uint32_vec sym_freq(256);
		for (uint32_t i = 0; i < file_data.size(); i++)
			sym_freq[file_data[i]]++;

		double total_theoretical_bits = 0.0f;
		for (uint32_t i = 0; i < 256; i++)
			if (sym_freq[i])
				total_theoretical_bits += sym_freq[i] * -log2((double)sym_freq[i] / file_size);
		printf("Source file size %u bytes contains %.1f bytes of entropy\n", file_size, total_theoretical_bits / 8.0f);

		uint32_vec scaled_cum_prob;
		bool status = vrange_create_cum_probs(scaled_cum_prob, sym_freq);
		if (!status)
			panic("vrange_create_cum_probs() failed!\n");

		// Compare vs. Huffman coding using package merge to limit the max code size at various sizes
		for (uint32_t h = 0; h < 4; h++)
		{
			const uint8_t s_max_huff_lens[4] = { 12, 13, 15, 16 };

			uint8_vec huff_code_lens(256);
			unsigned char actualMaxLen = packageMerge(s_max_huff_lens[h], 256, &sym_freq[0], &huff_code_lens[0]);
			(void)actualMaxLen;

			uint64_t total_huff_bits = 0;
			for (uint32_t i = 0; i < 256; i++)
				total_huff_bits += huff_code_lens[i] * sym_freq[i];
			printf("Source would compress to %.1f bytes using length limited Huffman coding (max code len=%u bits), %.3f%% vs. theoretical limit\n", total_huff_bits / 8.0f, s_max_huff_lens[h],
				total_theoretical_bits ? (total_huff_bits / total_theoretical_bits * 100.0f) : 0.0f);
		}

		uint32_vec dec_table;
		vrange_init_table(256, scaled_cum_prob, dec_table);
				
		test_plain_range_coding(file_data, scaled_cum_prob, dec_table, total_theoretical_bits);

		test_vectorized_range_coding(file_data, scaled_cum_prob, dec_table, total_theoretical_bits);
	}
	else 
	{
		bool status = false;
		uint8_vec out_data;

		printf("Processing file\n");
		
		if (mode == cModeComp)
		{
			const uint64_t start_time = get_clock();

			status = interleaved_encode(file_data, out_data);

			const double total_time = (double)(get_clock() - start_time) / (double)get_ticks_per_sec();

			if ((!status) || (!out_data.size()))
				panic("Compression failed!\n");

			printf("Total compression time: %.3f secs, %.1f MiB/sec.\n", total_time,
				(file_data.size() / total_time) / (1024 * 1024));
		}
		else
		{
			const uint64_t start_time = get_clock();

			uint32_t expected_crc32;
			status = interleaved_decode(file_data, out_data, expected_crc32);

			const double total_time = (double)(get_clock() - start_time) / (double)get_ticks_per_sec();

			if ((!status) || (!out_data.size()))
				panic("Decompression failed!\n");

			printf("Total decompression time: %.3f secs, %.1f MiB/sec.\n", total_time,
				(out_data.size() / total_time) / (1024 * 1024));

#if DECOMP_CRC32_CHECKING			
			uint32_t decoded_crc32 = crc32(0, &out_data[0], out_data.size());
			if (decoded_crc32 != expected_crc32)
				panic("Decompressed CRC-32 doesn't match!\n");
			else
				printf("CRC-32 check OK\n");
#endif
		}

		printf("Input size: %zu\nOutput size: %zu\n", file_data.size(), out_data.size());
				
		if (!write_data_to_file(pOut_filename, &out_data[0], out_data.size()))
			panic("Failed writing output data!\n");
	}

	printf("Success\n");
		
    return EXIT_SUCCESS;
}

