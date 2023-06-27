# Indexing test projects

- [scip-clang](#scip-clang)
- [LLVM](#llvm)
- [Chromium](#chromium)
- [Redpanda](#redpanda)
- [Postgres](#postgres)
- [Boost](#boost)

## scip-clang

For the sake of this example, I'll only describe how
to index scip-clang with cross-repo code navigation support
for [Abseil](https://github.com/abseil/abseil-cpp/).
See [tools/package-map.json](/tools/package-map.json) for more entries
(the absolute path in it is obtained via `bazel info output_base`)

> Aside: For a full index, one also needs to run `sed -e 's|$(STACK_FRAME_UNLIMITED)||'` on
> the compilation database due to the unexpanded Make variable used
> in compilation commands for LLVM which prevents type-checking from running.

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

> Aside: If indexing `scip-clang` along with 

Here, the `package-map.json` is as follows:

```json
[
  {"path": ".", "package": "scip-clang@v0.1.3"},
  {"path": "./bazel-scip-clang/external/com_google_absl", "package": "abseil-cpp@4ffaea74"}
]
```

The exact versions need to be determined based on current tag
or hashes provided to Bazel.

Other notes:
- Indexing rapidjson requires a recursive clone since it uses GTest
  via a submodule.
- Indexing spdlog requires pass `-DSPDLOG_ENABLE_TESTING=ON` to cmake.

## LLVM

Tested environments: Ubuntu 18.04, Ubuntu 22.04, macOS 13.

Dependencies: `cmake`, `ninja`, a host toolchain with Clang and LLD.
(ld on Linux hits OOM even with 64GB RAM.)

```bash
git clone https://github.com/llvm/llvm-project --depth=1
cd llvm-project/llvm
# The Debug + -g0 combination gives the fastest build
# The LLDB related flags prevent a build error on macOS
cmake -B ../build -G Ninja \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="-g0 -fuse-ld=lld" -DCMAKE_CXX_FLAGS="-g0 -fuse-ld=lld" \
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

## Boost

First, do a recursive clone of the boost monorepo.

```bash
# Use a Clang-based toolchain to avoid any GCC-specific options being
# accidentally used.
sudo apt-get install clang

git clone https://github.com/boostorg/boost --recursive
cd boost
git checkout boost-1.82.0
git submodule update
# This will create a b2 binary which will be used for building
# the code. Bear will then be used to intercept the compilation
# commands. Not all of Boost's code is buildable using CMake.
./bootstrap.sh
```

Invoke the indexer for each project with this Python script:

```python
import json
import os
import subprocess

cwd = os.getcwd()

dirs = []
pmap = []
for dirname in os.listdir("./libs"):
    abs_path = "{}/libs/{}".format(cwd, dirname)
    if not os.path.isdir(abs_path):
        continue
    dirs.append(abs_path)
    pmap.append({"path": abs_path, "package": "boost-{}@boost-1.82.0".format(dirname)})

with open('package-map.json', 'w') as f:
    json.dump(pmap, f)

for d in dirs:
    # Delete any build artifacts so that bear is able to regenerate the compilation database
    subprocess.run(["rm -rf bin.v2"], shell=True)
    # Skip work if the index was uploaded
    if os.path.isfile(d + "/index.scip"):
        continue
    if not os.path.isfile(d + "/compile_commands.json"):
        targets = ""
        if os.path.isfile(d + "/test/Jamfile.v2") or os.path.isfile(d + "/test/Jamfile"):
            targets += " test"
        if os.path.isfile(d + "/example/Jamfile.v2") or os.path.isfile(d + "/example/Jamfile"):
            targets += " example"
        if targets == "":
            print("Directory {} doesn't have any targets for b2, skipping".format(d))
            continue
        print("Generating compilation database for {}".format(d))
        # Turn off PCHs as they're used by the math library, but PCHs are not stable
        # across compiler versions, so we'd need to install the exact matching Clang
        # and use that to compile Boost for PCHs to be read correctly.
        #
        # 'pch=off' doesn't seem to be documented anywhere official except for:
        # https://github.com/boostorg/math/issues/619#issuecomment-829333938
        subprocess.run(["bear -- ../../b2 --toolset=clang pch=off" + targets],
            cwd=d, shell=True, capture_output=True)
    print("Indexing {}".format(d))
    res = subprocess.run(["scip-clang --package-map-path=../../package-map.json --compdb-path=compile_commands.json"], cwd=d, shell=True)
    if res.returncode != 0:
        print("Indexing failed for {}; skipping upload".format(d))
        continue
```
