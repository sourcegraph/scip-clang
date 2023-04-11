Download the binary for your platform using:

```bash
TAG=TAG_PLACEHOLDER \
OS="$(uname -s | tr '[:upper:]' '[:lower:]')" \
RELEASE_URL="https://github.com/sourcegraph/scip-clang/releases/download/$TAG" \
bash -c 'curl -L "$RELEASE_URL/scip-clang-$(uname -m)-$OS" -o scip-clang' && \
chmod +x scip-clang
```

The `-dev` binaries are meant for debugging issues (for example, if you run into a crash with `scip-clang`), and are not recommended for general use.
