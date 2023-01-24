# NOTE: Keep in sync with .buildkite/pipeline.yml
sudo apt update
curl -L https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
add-apt-repository 'deb http://apt.llvm.org/focal/  llvm-toolchain-focal-15 main'
sudo apt install clang-15 libtinfo-dev unzip ripgrep fd-find
export CC=clang-15
export CXX=clang++-15
wget -L https://github.com/bazelbuild/bazelisk/releases/download/v1.15.0/bazelisk-linux-amd64 -O bazel
chmod +x bazel
sudo mv bazel /usr/local/bin/bazel

git clone https://github.com/sourcegraph/scip-clang
cd scip-clang
