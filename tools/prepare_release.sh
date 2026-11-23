#!/usr/bin/env bash

# Bumps the version in Version.h and adds an editable Docker rebuild entry.
#
# Inputs:
# NEW_VERSION (optional) should be a string M.N.P. Defaults to the
# next patch version after the latest release tag.

set -euo pipefail

PROJECT_ROOT="$(dirname "${BASH_SOURCE[0]}")/.."
cd "$PROJECT_ROOT"

LAST_TAG="$(git describe --tags --abbrev=0 --match 'v[0-9]*')"
IFS=. read -r MAJOR MINOR PATCH <<<"${LAST_TAG#v}"
NEW_VERSION="${NEW_VERSION:-$MAJOR.$MINOR.$((PATCH + 1))}"

if ! [[ "$NEW_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: NEW_VERSION=$NEW_VERSION is not of the form M.N.P"
  exit 1
fi
if [ "$(printf '%s\n%s\n' "${LAST_TAG#v}" "$NEW_VERSION" | sort -V | tail -n 1)" != "$NEW_VERSION" ] ||
  [ "$NEW_VERSION" = "${LAST_TAG#v}" ]; then
  echo "error: NEW_VERSION=$NEW_VERSION must be newer than the latest release $LAST_TAG"
  exit 1
fi

awk -v version="v$NEW_VERSION" '
  !inserted && /^## v/ {
    heading = $0
    sub(/^## v[^ ]+/, "## " version, heading)
    print heading "\n\n- Rebuild the Docker image with the latest Ubuntu package updates.\n"
    print "[Full release notes](https://github.com/sourcegraph/scip-clang/releases/tag/" version ")\n"
    inserted = 1
  }
  { print }
' CHANGELOG.md >CHANGELOG.md.new
mv CHANGELOG.md.new CHANGELOG.md

sed -E "s|^#define VERSION \".*\"|#define VERSION \"$NEW_VERSION\"|" indexer/Version.h >indexer/Version.h.new
mv indexer/Version.h.new indexer/Version.h

NEW_VERSION="$NEW_VERSION" ./tools/version_check.sh
echo "Prepared release v$NEW_VERSION"
