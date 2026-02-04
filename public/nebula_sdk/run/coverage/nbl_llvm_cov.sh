#!/bin/bash
# Implement one shell script as one command to invoke cov tool on clang.
# If use clang, "llvm-cov gcov" should be tool of lcov.

${SDK_HOME}/toolchains/clang/bin/llvm-cov gcov $*
