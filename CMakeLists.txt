cmake_minimum_required(VERSION 3.5)

project(sserangecoding)

set(CMAKE_CXX_STANDARD 11)

add_executable(sserangecoding test.cpp sserangecoder.cpp packagemerge.c)

target_compile_options(sserangecoding PRIVATE "-msse4.1")

target_compile_options(sserangecoding PRIVATE "-O3")
