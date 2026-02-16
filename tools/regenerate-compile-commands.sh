#!/usr/bin/env bash
# Generates compile_commands.json for clangd IDE integration.
# Uses hedronvision/bazel-compile-commands-extractor.
# See: https://github.com/hedronvision/bazel-compile-commands-extractor

bazel run //tools:refresh_compile_commands
