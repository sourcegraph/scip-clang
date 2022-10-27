#!/usr/bin/env bash
bazel build //indexer:compdb

if command -v jq &> /dev/null; then
  jq . < bazel-bin/indexer/compile_commands.json > compile_commands.json
else
  cp bazel-bin/indexer/compile_commands.json compile_commands.json
fi
