#!/usr/bin/env bash
# For some reason, using --config=dev causes clangd errors inside Abseil
# on macOS. So use the default config instead.
#
# The --features flags disable module_maps which is incompatible with the
# grailbio/bazel-compilation-database library (archived, no longer maintained).
# See: https://github.com/grailbio/bazel-compilation-database/issues/101
bazel build //tools:compdb --features=-layering_check --features=-module_maps

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."

if command -v jq &> /dev/null; then
  jq . < "$PROJECT_ROOT/"bazel-bin/tools/compile_commands.json > compile_commands.json
else
  cp "$PROJECT_ROOT/"bazel-bin/tools/compile_commands.json compile_commands.json
fi
