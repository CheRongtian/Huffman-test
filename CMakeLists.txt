cmake_minimum_required(VERSION 3.10)
project(HuffmanTree VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(huff
    main.c
    bitstream.c
    huff.c
    lz77.c
    checksum.c
    format.c
)

if(MSVC)
    target_compile_options(huff PRIVATE /W4)
else()
    target_compile_options(huff PRIVATE -Wall -Wextra -Wpedantic)
endif()
