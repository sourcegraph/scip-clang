#!/usr/bin/env bash

SUFFIX="linux"
if [ "$(uname)" == "Darwin" ]; then
  SUFFIX="macos"
fi

bazel build //tools:compdb --spawn_strategy=local "--config=dev-$SUFFIX"

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

if command -v jq &> /dev/null; then
  jq . < "$PROJECT_ROOT/"bazel-bin/tools/compile_commands.json > compile_commands.json
else
  cp "$PROJECT_ROOT/"bazel-bin/tools/compile_commands.json compile_commands.json
fi
