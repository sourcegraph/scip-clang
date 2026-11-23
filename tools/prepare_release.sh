#!/usr/bin/env bash

# Bumps the version in Version.h and adds a CHANGELOG entry listing
# the commits since the latest release tag.
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

# Reuse the qualifier of the latest entry, such as "(beta)".
QUALIFIER="$(grep -m 1 '^## v' CHANGELOG.md | cut -d ' ' -f 3-)"

# Squash-merged commit subjects end with "(#123)"; link the PR
# the same way existing CHANGELOG entries do.
CHANGES="$(git log --format='%s' "$LAST_TAG"..HEAD | awk '{
  if (match($0, / \(#[0-9]+\)$/)) {
    print "- " substr($0, 1, RSTART - 1)
    print "  (https://github.com/sourcegraph/scip-clang/pull/" substr($0, RSTART + 3, RLENGTH - 4) ")"
  } else {
    print "- " $0
  }
}')"
if [ -z "$CHANGES" ]; then
  CHANGES="- No code changes. The Docker image is rebuilt with the latest Ubuntu package updates."
fi

ENTRY_FILE="$(mktemp)"
printf '## v%s %s\n\n%s\n\n' "$NEW_VERSION" "$QUALIFIER" "$CHANGES" >"$ENTRY_FILE"
awk -v entry_file="$ENTRY_FILE" '
  !inserted && /^## v/ { while ((getline line < entry_file) > 0) print line; inserted = 1 }
  { print }
' CHANGELOG.md >CHANGELOG.md.new
mv CHANGELOG.md.new CHANGELOG.md
rm "$ENTRY_FILE"

sed -E "s|^#define VERSION \".*\"|#define VERSION \"$NEW_VERSION\"|" indexer/Version.h >indexer/Version.h.new
mv indexer/Version.h.new indexer/Version.h

NEW_VERSION="$NEW_VERSION" ./tools/version_check.sh
echo "Prepared release v$NEW_VERSION"
