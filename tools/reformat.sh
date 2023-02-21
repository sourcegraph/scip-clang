#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

if [ ! -f "bazel-bin/external/llvm_toolchain/clang-format" ]; then
  bazel build @llvm_toolchain//:clang-format
fi

if [ ! -f "bazel-bin/third_party/bazel_buildtools/buildifier" ]; then
  bazel build //third_party/bazel_buildtools:buildifier
fi

(
  cd "$PROJECT_ROOT"
  git ls-files BUILD WORKSPACE "**/BUILD" "**/.BUILD" "**.bzl" | xargs bazel-bin/third_party/bazel_buildtools/buildifier
  git ls-files "**.cc" "**.h" | xargs bazel-bin/external/llvm_toolchain/clang-format -i
  git ls-files "**.py" | sed -e "s|^|$PWD/|" | xargs bazel run //tools:reformat_python --
)
