# Indexing test projects

## Indexing LLVM

Requires: `cmake`, `ninja`, a host toolchain.

```bash
git clone https://github.com/llvm/llvm-project --depth=1
cd llvm-project/llvm
cmake -B ../build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-O0" -DCMAKE_CXX_FLAGS="-O0" -DLLVM_ENABLE_PROJECTS="all"
scip-clang --compdb-path=build/compile_commands.json
```

## Indexing Chromium

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

## Indexing Redpanda

Redpanda is an interesting project to test
because it uses C++20 features like support for concurrency.

```bash
git clone https://github.com/redpanda-data/redpanda.git
cd redpanda
sudo ./install-dependencies.sh
sed -e 's/-DCMAKE_BUILD_TYPE=Release/-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-O0 -DCMAKE_CXX_FLAGS=-O0 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON/' build.sh > build-debug.sh
CC=clang CXX=clang++ ./build-debug.sh
scip-clang --compdb-path=build/compile_commands.json
```
