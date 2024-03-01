#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

bazel build //third_party:actionlint

(
  cd "$PROJECT_ROOT"
  git ls-files ".github/workflows/**.yml" | xargs bazel-bin/third_party/actionlint
)
