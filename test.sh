#!/bin/bash
set -e
clang \
    -Wall \
    -Wextra \
    -Wpedantic \
    -Werror \
    -std=c11 \
    main.c huff.c \
    -o huff
./huff