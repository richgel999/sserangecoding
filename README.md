# sserangecoding
Fast SSE 4.1 24-bit range coder for 8-bit alphabets. Note the encoder is not optimized, just the vectorized decoder. 

The decoder uses 16 interleaved streams. No special signaling or sideband information is needed between the encoder and decoder. The encoder swizzles each individual range encoder's output bytes in the proper order right after compression. SSE 4.1 decoding is very fast on the Intel CPU's I've tried, at around 550-700 MiB/sec.

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
