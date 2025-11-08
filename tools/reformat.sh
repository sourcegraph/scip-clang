#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

bazel build @llvm_toolchain//:clang-format
bazel build //third_party/bazel_buildtools:buildifier

(
  cd "$PROJECT_ROOT"
  BUILDIFIER_BIN="$(bazel cquery --output=files //third_party/bazel_buildtools:buildifier 2>/dev/null)"
  CLANG_FORMAT_BIN="$(bazel cquery --output=files @llvm_toolchain//:clang-format 2>/dev/null)"
  git ls-files BUILD WORKSPACE "**/BUILD" "**/.BUILD" "**.bzl" | xargs "$BUILDIFIER_BIN"
  git ls-files "**.cc" "**.h" | xargs "$CLANG_FORMAT_BIN" -i
  git ls-files "**.py" | sed -e "s|^|$PWD/|" | xargs bazel run //tools:reformat_python --
)
