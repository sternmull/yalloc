#! /usr/bin/sh

# This script runs the libfuzzer based test for 10 seconds (adjust -max_total_time).
# It writes coverage_libfuzzer.html which displays coverage.

set -e

if [ ! -d corpus_libfuzzer ]
then
  mkdir corpus_libfuzzer
fi

clang test_fuzzer.c yalloc/yalloc.c -fprofile-instr-generate -DUSE_LIBFUZZER -g -O0 -fsanitize=fuzzer -fcoverage-mapping -o test-binary

# Input is 4 bytes for pool-size and 6 bytes per allocation.
# So with 64 bytes we get 10 allocations... which should be more than sufficient to trigger all critical scenarios.
./test-binary corpus_libfuzzer/ -max_total_time=10 -timeout=10 -jobs=8 -max_len=64

llvm-profdata merge -sparse *.profraw -o default.profdata
llvm-cov show test-binary -format=html -instr-profile=default.profdata > coverage_libfuzzer.html
llvm-cov report -instr-profile default.profdata test-binary

echo "All fine!"
