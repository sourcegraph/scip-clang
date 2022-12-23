#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

(
  cd "$PROJECT_ROOT"
  git ls-files BUILD WORKSPACE "**/BUILD" "**/.BUILD" "**.bzl" | xargs buildifier
  git ls-files "**.cc" "**.h" | xargs clang-format -i
)
