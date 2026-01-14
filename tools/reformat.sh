#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

# Try to use the LLVM toolchain clang-format, fall back to system clang-format
# (LLVM 21 binaries require glibc 2.34+ which may not be available on older CI agents)
CLANG_FORMAT=""
if [ ! -f "bazel-bin/external/llvm_toolchain/clang-format" ]; then
  bazel build @llvm_toolchain//:clang-format
fi
if bazel-bin/external/llvm_toolchain/clang-format --version >/dev/null 2>&1; then
  CLANG_FORMAT="bazel-bin/external/llvm_toolchain/clang-format"
elif command -v clang-format >/dev/null 2>&1; then
  echo "Warning: Using system clang-format (LLVM toolchain version not compatible with this system)"
  CLANG_FORMAT="clang-format"
else
  echo "Warning: No clang-format available, skipping C++ formatting"
  CLANG_FORMAT=""
fi

if [ ! -f "bazel-bin/third_party/bazel_buildtools/buildifier" ]; then
  bazel build //third_party/bazel_buildtools:buildifier
fi

(
  cd "$PROJECT_ROOT"
  git ls-files BUILD WORKSPACE "**/BUILD" "**/.BUILD" "**.bzl" | xargs bazel-bin/third_party/bazel_buildtools/buildifier
  if [ -n "$CLANG_FORMAT" ]; then
    git ls-files "**.cc" "**.h" | xargs "$CLANG_FORMAT" -i
  fi
  git ls-files "**.py" | sed -e "s|^|$PWD/|" | xargs bazel run //tools:reformat_python --
)
