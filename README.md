# sserangecoding
This repo contains a fast SSE 4.1 optimized 24-bit interleaved [Range Coder](https://en.wikipedia.org/wiki/Range_coding) for 8-bit alphabets. 

SSE 4.1 decoding is very fast on the Intel/AMD CPU's I've tried, at around 550-700 MiB/sec (2.2-2.4 cycles/byte) on Ice Lake, 650 MiB/sec. on 4GHz Skylake, and 550 MiB/sec. on a Ryzen 5 3450U. This seems roughly competitive vs. [rANS](https://en.wikipedia.org/wiki/Asymmetric_numeral_systems) decoding using SSE 4.1.

The one advantage Range Coding has vs. ANS is patents. At least one corporation (Microsoft) has at least one [ANS patent](https://www.theregister.com/2022/02/17/microsoft_ans_patent/). **By comparison range coding is >40 years old and is unlikely to be a patent minefield.**

Disadvantages vs. rANS: less precise (possibly - on book1 24-bit range coding is more efficient than this [FSE implementation](https://github.com/Cyan4973/FiniteStateEntropy/tree/dev)), slower encode (ultimately due to the post-encode swizzle step to get the byte streams in the right order), and decoding is heavily reliant on fast vectorized hardware division. On modern CPU's vectorized single precision division is not a deal breaker.

A relatively straighforward AVX-2 port of this code (bumped up to 64 interleaved streams) gets 1204 MiB/sec. decoding book1.

## Implementation Notes

- The vectorized decoder uses 16 interleaved streams (in 4 groups of 4 lanes). 24-bit integers are used to enable using fast precise integer vectorized divides with `_mm_div_ps`, which is crucial for performance. The performance and practicality of a vectorized range decoder like this is highly dependent (really, completely lives and dies!) on the availability and performance of fast hardware division. This implementation specifically uses 24-bit integers, otherwise the results from `_mm_div_ps` (with a subsequent conversion back to int with truncation) wouldn't be accurate. After many experiments, this is the only way I could find to make this decoder competitive. 
- Using 24-bit ints sacrifices some small amount of coding efficiency (a small fraction of a percent), but compared to length-limited Huffman coding it's still more efficient. The test app displays the theoretical file entropy along with the # of bytes it would take to encode the input using [Huffman coding](https://en.wikipedia.org/wiki/Huffman_coding) with the [Package Merge algorithm](https://create.stephan-brumme.com/length-limited-prefix-codes/) at various maximum code lengths, for comparison purposes.
- The encoder swizzles each individual range encoder's output bytes into the proper order right after compression. No special signaling or sideband information is needed between the encoder and decoder, because it's easy to predict how many bytes will be fetched from each stream during each coding/decoding step. (Notably, at each encode step you can record the # of bytes flushed to the output, which in this implementation is always [0,2] bytes per step. The decoder always reads the same # of bytes from the stream as the encoder wrote for that step, but from a different offset.) This post-compression byte swizzling step is an annoying cost that rANS doesn't pay. I'm unsure if this step can be further optimized.
- The decoder is safe against accidental or purposeful corruption, i.e. it shouldn't ever read past the end of the input buffer or crash on invalid/corrupt inputs. I am still testing this, however. 
- The encoder is not optimized yet: just the vectorized decoder, which is my primary concern. 

## Compiling

Use [cmake](https://cmake.org/) under Linux or Windows with Visual Studio 2019 or 2022:

`cmake .`  
`make`

Under Windows I've tested with clang, MSVC 2019 and MSVC 2022. Under Linux I've tested with clang v10 and gcc v9.3.0.

## Testing

Running `sserangecoding` with no command line parameters will load book1 from the current directory, compress it, then uncompress it in various ways. 

`sserangecoding filename` will run the test mode using the specified file instead of book1.

`sserangecoding -h` displays help.

## Additional Options

The test app is not intended to be a good file compressor: it stores 256 scaled 16-bit symbol frequencies to the compressed file (512 bytes of overhead). The goal of the 'c' and 'd' commands is to prove that this codec works and facilitate automated fuzz testing.

`sserangecoding c in_file cmp_file` will compress in_file to cmp_file using order-0 range coding. The symbol frequencies are scaled to 16-bits which will likely impact compression efficiency vs. the test mode, which uses 32-bit frequencies.

`sserangecoding d cmp_file out_file` will decompress cmp_file to out_file using order-0 range coding. A CRC-32 check (which isn't very fast) is used to verify the decompressed data. Set `DECOMP_CRC32_CHECKING` to 0 in test.cpp to disable the CRC-32 check.

## Usage

Include `sserangecoder.h`. Call `sserangecoder::vrange_init()` somewhere before using any other functionality.

For encoding: construct an array of symbol frequencies, then call `vrange_create_cum_probs()` with this array to create an array of scaled cumulative frequencies. Then the easiest thing to do is next call `vrange_encode()` to encode a buffer which can be decoded using `vrange_decode()`.

For decoding: in addition to the scaled cumulative frequencies table, you'll need to build a lookup table used to accelerate decoding by calling `vrange_init_table()`. `vrange_decode()` can be used to decode a buffer. See the lower level helper functions `vrange_decode()` (which is an overloaded name) and `vrange_normalize()` (which work together) for the lower level vectorized decoding functions.

## Example output for book1 (Core i7 1065G7, Ice Lake, 2020 Dell Inspiron 5000 ~3.9 GHz)

```
Reading file book1
Source file size 768771 bytes contains 435042.6 bytes of entropy
Source would compress to 438768.2 bytes using length limited Huffman coding (max code len=12 bits), 100.856% vs. theoretical limit
Source would compress to 438504.9 bytes using length limited Huffman coding (max code len=13 bits), 100.796% vs. theoretical limit
Source would compress to 438400.1 bytes using length limited Huffman coding (max code len=15 bits), 100.772% vs. theoretical limit
Source would compress to 438385.2 bytes using length limited Huffman coding (max code len=16 bits), 100.768% vs. theoretical limit

Testing plain range coding:
Total encoding time: 0.005332 seconds, 137.5 MiB/sec.
Compressed file from 768771 bytes to 436186 bytes, 100.263% vs. theoretical limit
Total decoding time: 0.007671 seconds, 95.6 MiB/sec.
Decompression OK

Testing vectorized interleaved range decoding (encoding is not vectorized):
Total encoding time: 0.014139, 51.9 MiB/sec.
Compressed file from 768771 bytes to 436229 bytes, 100.273% vs. theoretical limit

Decompression OK
0.001270 seconds, 577.1 MiB/sec., 2.5 cycles per byte

Decompression OK
0.001204 seconds, 608.7 MiB/sec., 2.3 cycles per byte

Decompression OK
0.001184 seconds, 619.1 MiB/sec., 2.3 cycles per byte

Decompression OK
0.001190 seconds, 615.9 MiB/sec., 2.3 cycles per byte

Decompression OK
0.001265 seconds, 579.4 MiB/sec., 2.5 cycles per byte

Decompression OK
0.001328 seconds, 552.2 MiB/sec., 2.6 cycles per byte

Decompression OK
0.001256 seconds, 583.9 MiB/sec., 2.4 cycles per byte

Decompression OK
0.001300 seconds, 563.8 MiB/sec., 2.5 cycles per byte
Success
```

For comparison purposes, here's how Collet's scalar [FSE benchmark](https://github.com/Cyan4973/FiniteStateEntropy/tree/dev) performs on book1 on the same machine, compiled with the same compiler. Interestingly, 24-bit range coding resuls in a smaller file vs. this FSE implementation (-b book1):

```
FSE : Finite State Entropy, 64-bits demo by Yann Collet (Apr 27 2023)
gary_corpus\book1 :    768771 ->    437232 (56.87%),  331.4 MB/s ,  370.3 MB/s
```

In Huffman mode (-h -b book1):

```
FSE : Finite State Entropy, 64-bits demo by Yann Collet (Apr 27 2023)
gary_corpus\book1 :    768771 ->    439150 (57.12%),  648.8 MB/s ,  800.9 MB/s
```

Here's [ryg_rans](https://github.com/rygorous/ryg_rans) (SSE 4.1 interleaved rANS) on the same machine, but running under Linux and built with clang:

```
/mnt/d/dev/ryg_rans-master$ ./exam_simd_sse41
rANS encode:
10269804 clocks, 13.4 clocks/symbol (106.8MiB/s)
6694371 clocks, 8.7 clocks/symbol (164.0MiB/s)
7874894 clocks, 10.2 clocks/symbol (139.4MiB/s)
10604580 clocks, 13.8 clocks/symbol (103.5MiB/s)
6448287 clocks, 8.4 clocks/symbol (170.2MiB/s)
rANS: 435604 bytes
5312245 clocks, 6.9 clocks/symbol (206.6MiB/s)
5622944 clocks, 7.3 clocks/symbol (195.2MiB/s)
5077465 clocks, 6.6 clocks/symbol (216.2MiB/s)
5023212 clocks, 6.5 clocks/symbol (218.5MiB/s)
4921714 clocks, 6.4 clocks/symbol (223.0MiB/s)
decode ok!

interleaved rANS encode:
4755808 clocks, 6.2 clocks/symbol (230.7MiB/s)
4336588 clocks, 5.6 clocks/symbol (253.1MiB/s)
4096033 clocks, 5.3 clocks/symbol (268.0MiB/s)
4802643 clocks, 6.2 clocks/symbol (228.5MiB/s)
5357016 clocks, 7.0 clocks/symbol (204.9MiB/s)
interleaved rANS: 435606 bytes
3441391 clocks, 4.5 clocks/symbol (318.9MB/s)
3352760 clocks, 4.4 clocks/symbol (327.3MB/s)
3309458 clocks, 4.3 clocks/symbol (331.6MB/s)
3448932 clocks, 4.5 clocks/symbol (318.0MB/s)
4722032 clocks, 6.1 clocks/symbol (232.3MB/s)
decode ok!

interleaved SIMD rANS encode: (encode itself isn't SIMD)
4428328 clocks, 5.8 clocks/symbol (247.8MiB/s)
4440744 clocks, 5.8 clocks/symbol (247.2MiB/s)
4812514 clocks, 6.3 clocks/symbol (228.0MiB/s)
5777703 clocks, 7.5 clocks/symbol (189.9MiB/s)
4701167 clocks, 6.1 clocks/symbol (233.4MiB/s)
SIMD rANS: 435626 bytes
2057385 clocks, 2.7 clocks/symbol (533.3MB/s)
2022259 clocks, 2.6 clocks/symbol (542.6MB/s)
1940053 clocks, 2.5 clocks/symbol (565.6MB/s)
2032723 clocks, 2.6 clocks/symbol (539.0MB/s)
2138407 clocks, 2.8 clocks/symbol (512.8MB/s)
decode ok!
```

## Special Thanks

Thanks to PowTurbo for their "Turbo Range Coder" and "Turbo Histogram" repositories, which I studied while working on this code:
https://github.com/powturbo/Turbo-Range-Coder
https://github.com/powturbo/Turbo-Histogram

Also thanks to Igor Pavlov and Dmitry Subbotin for open sourcing their scalar range coders, which were essential resources.

## License

See [unlicense.org](https://unlicense.org/). The files in this repo are Public Domain in the US and in jurisdictions that recognize copyright law, i.e. they are not copyrighted or anyone's intellectual propery.

