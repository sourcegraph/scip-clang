#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

if [ ! -f "bazel-bin/external/llvm_toolchain/clang-format" ]; then
  echo "Missing clang-format binary; run 'bazel build @llvm_toolchain//:clang-format <args>' first" 1>&2
  exit 1
fi

set +e
if ! command -v poetry > /dev/null; then
  echo "Missing poetry for formatting Python; see https://python-poetry.org/docs/#installation"
elif ! poetry run command -v black > /dev/null; then
  echo "Missing black for formatting Python; run 'poetry install' first"
fi
set -e

(
  cd "$PROJECT_ROOT"
  git ls-files BUILD WORKSPACE "**/BUILD" "**/.BUILD" "**.bzl" | xargs buildifier
  git ls-files "**.cc" "**.h" | xargs bazel-bin/external/llvm_toolchain/clang-format -i
  git ls-files "**.py" | xargs poetry run black
)
