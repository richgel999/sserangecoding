# sserangecoding
This repo contains a fast SSE 4.1 optimized 24-bit [Range Coder](https://en.wikipedia.org/wiki/Range_coding) for 8-bit alphabets. 

The vectorized decoder uses 16 interleaved streams (in 4 groups of 4 lanes). 24-bit integers are used to enable using fast lossless (non-approximated) vectorized divides using `_mm_div_ps`. The encoder swizzles each individual range encoder's output bytes into the proper order right after compression. SSE 4.1 decoding is very fast on the Intel CPU's I've tried, at around 550-700 MiB/sec (2.2-2.4 cycles/byte on Ice Lake). This seems roughly competitive vs. [rANS](https://en.wikipedia.org/wiki/Asymmetric_numeral_systems) decoding, at least on modern Intel CPU's. No special signaling or sideband information is needed between the encoder and decoder, because it's easy to predict how many bytes will be fetched from each stream during each coding/decoding step.

The performance of a vectorized range decoder like this is highly dependent (really, completely lives and dies!) on the availability and performance of a fast hardware divider. This implementation specifically uses 24-bit integers to exploit `_mm_div_ps`. After many experiments, this is the only way I could find to make this decoder competitive. Using 24-bit ints sacrifices some small amount of coding efficiency (a small fraction of a percent), but compared to length-limited Huffman coding it's still more efficient. The test app displays the theoretical file entropy along with the # of bits it would take to encode the input using the [Package Merge algorithm](https://create.stephan-brumme.com/length-limited-prefix-codes/) at various maximum code lengths, for comparison purposes.

The one advantage range coding has vs. ANS is patents. At least one corporation (Microsoft) has at least one [ANS patent](https://www.theregister.com/2022/02/17/microsoft_ans_patent/). **By comparison range coding is >40 years old and is unlikely to be a patent minefield.**

Note the encoder is not optimized yet- just the vectorized decoder, which is my primary concern.

## Compiling

Use [cmake](https://cmake.org/) under Linux or under Windows with Visual Studio 2019 or 2022:

`cmake .`  
`make`

Under Windows I've tested with clang, VS 2019 and VS 2022. Under Linux I've tested with clang v10 and gcc v9.3.0.

## Testing

Running `sserangecoding` with no command line parameters will load book1 from the current directory, compress it, then uncompress it in various ways. 

`sserangecoding -h` displays help.

`sserangecoding c in_file cmp_file` will compress in_file to out_file using order-0 range coding. 

`sserangecoding d cmp_file out_file` will decompress cmp_file to out_file using order-0 range coding. A CRC-32 check (which isn't very fast) is used to verify the decompressed data.

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

## License

See [unlicense.org](https://unlicense.org/). The files in this repo are Public Domain in the US and in jurisdictions that recognize copyright law, i.e. they are not copyrighted or anyone's intellectual propery.
