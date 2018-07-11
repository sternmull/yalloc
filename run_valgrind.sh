#!/usr/bin/sh
set -e

echo "Testing if valgrind integration works (unoptimized)"
gcc -g -O0 test_valgrind.c yalloc/yalloc.c -DYALLOC_VALGRIND -o test-binary
valgrind --log-fd=-1 ./test-binary

echo "Testing if valgrind integration works (optimized)"
gcc -g -O2 test_valgrind.c yalloc/yalloc.c -DYALLOC_VALGRIND -o test-binary
valgrind --log-fd=-1 ./test-binary

echo "Testing covarge with valgrind integration (unoptimized)"
gcc -g -O0 test_coverage.c yalloc/yalloc.c -DYALLOC_VALGRIND -o test-binary
valgrind ./test-binary

echo "Testing covarge with valgrind integration (optimized)"
gcc -g -O2 test_coverage.c yalloc/yalloc.c -DYALLOC_VALGRIND -o test-binary
valgrind ./test-binary

echo "Testing with valgrind integration and random testcases (unoptimized)"
gcc -g -O0 test_fuzzer.c yalloc/yalloc.c -DYALLOC_VALGRIND -o test-binary
valgrind ./test-binary -n 100

echo "Testing with valgrind integration and random testcases (optimized)"
gcc -g -O2 test_fuzzer.c yalloc/yalloc.c -DYALLOC_VALGRIND -o test-binary
valgrind ./test-binary -n 100

echo "All fine!"
