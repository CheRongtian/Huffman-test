#!/bin/bash

set -e

cd "$(dirname "$0")"

echo "========================================"
echo "1. 清理旧测试文件"
echo "========================================"

rm -f test2.txt
rm -f test2.gz
rm -f test2_output.txt
rm -f main

echo
echo "========================================"
echo "2. 生成新的测试文件"
echo "========================================"

cat > test2.txt <<'EOF'
Custom gzip decompression verification
======================================

This file was generated specifically for the final decompression test.

0123456789
ABCDEFGHIJKLMNOPQRSTUVWXYZ
abcdefghijklmnopqrstuvwxyz

The decompressor should rebuild every byte in this file.

Huffman Huffman Huffman Huffman Huffman
LZ77 LZ77 LZ77 LZ77 LZ77
compression compression compression compression
decompression decompression decompression decompression

Repeated section:
apple banana orange apple banana orange
apple banana orange apple banana orange
apple banana orange apple banana orange
apple banana orange apple banana orange
apple banana orange apple banana orange

Symbols:
!@#$%^&*()_+-=[]{};:',.<>/?

Final verification line:
If this line exists in the output file, the decompressor reached the end correctly.

END-OF-FILE
EOF

echo "已生成 test2.txt"
echo "原始文件大小: $(wc -c < test2.txt) bytes"

echo
echo "========================================"
echo "3. 使用系统 gzip 生成 test2.gz"
echo "========================================"

gzip -9 -n -c test2.txt > test2.gz

echo "已生成 test2.gz"
echo "压缩文件大小: $(wc -c < test2.gz) bytes"

echo
echo "========================================"
echo "4. 编译 main.c"
echo "========================================"

clang -o main main.c

echo "编译成功"

echo
echo "========================================"
echo "5. 使用自己的程序解压 test2.gz"
echo "========================================"

./main

echo
echo "========================================"
echo "6. 检查输出文件"
echo "========================================"

if [ ! -f test2_output.txt ]; then
    echo "❌ 测试失败"
    echo "程序没有生成 test2_output.txt"
    exit 1
fi

echo "已生成 test2_output.txt"
echo "解压文件大小: $(wc -c < test2_output.txt) bytes"

echo
echo "========================================"
echo "7. 逐字节比较原文件和解压文件"
echo "========================================"

if cmp -s test2.txt test2_output.txt; then
    echo "✅ 内容完全一致"
    echo "✅ gzip 解压成功"
    echo
    echo "原始文件: test2.txt"
    echo "压缩文件: test2.gz"
    echo "解压文件: test2_output.txt"
    echo
    echo "原始大小: $(wc -c < test2.txt) bytes"
    echo "解压大小: $(wc -c < test2_output.txt) bytes"
    echo
    echo "SHA-256:"
    shasum -a 256 test2.txt
    shasum -a 256 test2_output.txt
    exit 0
else
    echo "❌ 内容不一致"
    echo "❌ gzip 解压失败"
    echo
    echo "下面显示第一个出现差异的位置:"
    cmp test2.txt test2_output.txt || true
    exit 1
fi