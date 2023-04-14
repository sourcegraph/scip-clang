#!/usr/bin/env bash

# Inputs:
# NEW_VERSION should be a string M.N.P

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."
cd "$PROJECT_ROOT"

if ! grep -q "## v$NEW_VERSION" CHANGELOG.md; then
  echo "error: Missing CHANGELOG entry for $NEW_VERSION"
  echo "note: CHANGELOG entries are required for publishing releases"
  exit 1
fi

if ! grep -q "#define VERSION \"$NEW_VERSION\"" indexer/Version.h; then
  echo "error: VERSION in Version.h doesn't match NEW_VERSION=$NEW_VERSION"
  exit 1
fi

if ! grep -q "_LLVM_COMMIT" fetch_deps.bzl; then
  echo "error: Missing _LLVM_COMMIT in fetch_deps.bzl"
  exit 1
fi

LLVM_COMMIT_STRING="$(grep "_LLVM_COMMIT = " fetch_deps.bzl | cut -d ' ' -f 3)"

if ! grep -q "$LLVM_COMMIT_STRING" indexer/Version.h; then
  echo "info: Found LLVM_COMMIT $LLVM_COMMIT_STRING"
  echo "error: LLVM_COMMIT in Version.h doesn't match fetch_deps.bzl"
  exit 1
fi