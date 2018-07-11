#! /usr/bin/sh

# This script runs a minimal test that covers all branches of yalloc.
# It writes coverage_test.html which displays coverage.

set -e

clang test_coverage.c yalloc/yalloc.c -fprofile-instr-generate -g -fcoverage-mapping -o test-binary
./test-binary

llvm-profdata merge -sparse *.profraw -o default.profdata
llvm-cov show test-binary -format=html -instr-profile=default.profdata > coverage_test.html
llvm-cov report -instr-profile default.profdata test-binary

echo "All fine!"
