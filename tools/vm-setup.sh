# NOTE: Keep in sync with .buildkite/pipeline.yml
sudo apt update
sudo apt install g++ unzip ripgrep fd-find
wget -L https://github.com/bazelbuild/bazelisk/releases/download/v1.15.0/bazelisk-linux-amd64 -O bazel
chmod +x bazel
sudo mv bazel /usr/local/bin/bazel

git clone https://github.com/sourcegraph/scip-clang
cd scip-clang
