#!/usr/bin/env bash
# For some reason, using --config=dev causes clangd errors inside Abseil
# on macOS. So use the default config instead.
bazel build //tools:compdb --spawn_strategy=local

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

if command -v jq &> /dev/null; then
  jq . < "$PROJECT_ROOT/"bazel-bin/tools/compile_commands.json > compile_commands.json
else
  cp "$PROJECT_ROOT/"bazel-bin/tools/compile_commands.json compile_commands.json
fi
