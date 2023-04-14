Download the binary for your platform using:

```bash
TAG=TAG_PLACEHOLDER \
OS="$(uname -s | tr '[:upper:]' '[:lower:]')" \
RELEASE_URL="https://github.com/sourcegraph/scip-clang/releases/download/$TAG" \
bash -c 'curl -L "$RELEASE_URL/scip-clang-x86_64-$OS" -o scip-clang' && \
chmod +x scip-clang
```

The `-dev` binaries are meant for debugging issues (for example, if you run into a crash with `scip-clang`), and are not recommended for general use.

The Linux binaries depend on glibc and should work on:
- Debian 10 (Buster), 11 (Bullseye) or newer
- Ubuntu 18.04 (Bionic Beaver) or newer
