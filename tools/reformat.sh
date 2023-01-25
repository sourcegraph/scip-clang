#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

if [ ! -f "bazel-bin/external/llvm_toolchain/clang-format" ]; then
  echo "Missing clang-format binary; run 'bazel build @llvm_toolchain//:clang-format <args>' first" 1>&2
  exit 1
fi

(
  cd "$PROJECT_ROOT"
  git ls-files BUILD WORKSPACE "**/BUILD" "**/.BUILD" "**.bzl" | xargs buildifier
  git ls-files "**.cc" "**.h" | xargs bazel-bin/external/llvm_toolchain/clang-format -i
)
