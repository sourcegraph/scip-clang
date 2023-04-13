#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

# Skip linting as actionlint doesn't support substitutions...

# if [ ! -f "bazel-bin/third_party/actionlint" ]; then
#   bazel build //third_party:actionlint
# fi

# (
#   cd "$PROJECT_ROOT"
#   git ls-files ".github/workflows/**.yml" | xargs bazel-bin/third_party/actionlint
# )
