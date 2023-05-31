# Indexing test projects

## scip-clang

For the sake of this example, I'll only describe how
to index scip-clang with cross-repo code navigation support
for [Abseil](https://github.com/abseil/abseil-cpp/).

First, index Abseil.

```
git checkout 4ffaea74
cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-g0" -DABSL_BUILD_TESTING=ON -DABSL_USE_GOOGLETEST_HEAD=ON
scip-clang --compdb-path=build/compile_commands.json --package-map=abseil-package-map.json
```

The `abseil-package-map.json` file used here is:

```json
[ {"path": ".", "package": "abseil-cpp@4ffaea74"} ]
```

Next, index scip-clang itself. We subset out the compilation
database first to not index TUs in other packages.

```bash
jq '[.[] | select(.file | (contains("indexer/") or startswith("test/") or contains("com_google_absl")))]' compile_commands.json > min.json
./bazel-bin/indexer/scip-clang --compdb-path=min.json --package-map-path=package-map.json
```

Here, the `package-map.json` is as follows:

```json
[
  {"path": ".", "package": "scip-clang@v0.1.3"},
  {"path": "./bazel-scip-clang/external/com_google_absl", "package": "abseil-cpp@4ffaea74"}
]
```

The exact versions need to be determined based on current tag
or hashes provided to Bazel.

## LLVM

Tested environments: Ubuntu 18.04, Ubuntu 22.04, macOS 13.

Dependencies: `cmake`, `ninja`, a host toolchain.

```bash
git clone https://github.com/llvm/llvm-project --depth=1
cd llvm-project/llvm
# The Debug + -g0 combination gives the fastest build
# The LLDB related flags prevent a build error on macOS
cmake -B ../build -G Ninja \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-g0" -DCMAKE_CXX_FLAGS="-g0" \
  -DLLDB_INCLUDE_TESTS=OFF -DLLDB_USE_SYSTEM_DEBUGSERVER=ON \
  -DLLVM_ENABLE_PROJECTS="all" 
ninja -C ../build
scip-clang --compdb-path=build/compile_commands.json
```

If there is a build error with some uncommon target,
it may makes sense to replace the
`-DLLVM_ENABLE_PROJECTS="all"` with a narrower set of targets,
which are more likely to build on all platforms,
e.g. `-DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld"`.

## Chromium

Tested environments: Ubuntu 18.04

Dependencies: Covered by instructions below.

```bash
# The Chromium docs say Python 3.6+ is required, which implies that the
# default Python 3.6.9 ought to work, but some code uses keyword arguments
# introduced in Python 3.7. At the time of writing, Python 3.10 has a breaking
# change, so use Python 3.8 instead.
sudo apt-get install software-properties-common -y
sudo add-apt-repository ppa:deadsnakes/ppa -y
sudo apt-get update -y
sudo apt-get install python3.8 -y
sudo update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.8 1

git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
echo 'export PATH="$PATH:${HOME}/depot_tools"' >> ~/.zshrc
source ~/.zshrc
mkdir ~/chromium && cd ~/chromium
fetch --nohooks --nohistory chromium
cd src
./build/install-build-deps.sh
gclient runhooks
gn gen out/X --args='symbol_level=0'
./tools/clang/scripts/generate_compdb.py -p out/X > compile_commands.json
ninja -C out/X
```

## Redpanda

Tested environments: Ubuntu 22.04

Dependencies: Installed by build instructions.

Redpanda is an interesting project to test
because it uses C++20 features like support for concurrency.

```bash
git clone https://github.com/redpanda-data/redpanda.git
cd redpanda
sudo ./install-dependencies.sh
sed -e 's/-DCMAKE_BUILD_TYPE=Release/-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-O0 -DCMAKE_CXX_FLAGS=-O0 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON/' build.sh > build-debug.sh
chmod +x build-debug.sh
CC=clang CXX=clang++ ./build-debug.sh
scip-clang --compdb-path=build/compile_commands.json
```

## Postgres

Tested environments: Ubuntu 22.04

Dependencies: `meson`, `flex`, `bison`, a host toolchain.

Postgres finishes indexing within a few seconds.

```bash
git clone https://github.com/postgres/postgres --depth=1
cd postgres
meson setup ../postgres-build .
ninja -C ../postgres-build
scip-clang --compdb-path=../postgres-build/compile_commands.json
```
