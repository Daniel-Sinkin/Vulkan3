#!/usr/bin/env bash
set -euo pipefail

rm -rf build
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build