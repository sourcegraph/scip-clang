FROM ubuntu:22.04 as build

ARG BUILD_TYPE=release

# Install Bazelisk
RUN apt-get update && apt-get install -y curl libc6-dev python3 build-essential ninja-build git cmake && \
    echo $(uname -p) && \
    curl -fsSL https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-$(uname -p | sed 's/aarch64/arm64/' | sed 's/x86_64/amd64/') > /usr/local/bin/bazel && \
    chmod +x /usr/local/bin/bazel

WORKDIR /sources

COPY . .

RUN bazel build //... --config $BUILD_TYPE

FROM ubuntu:22.04 as indexer

COPY --from=build /sources/bazel-bin/indexer/scip-clang /usr/local/bin/scip-clang

ENTRYPOINT [ "scip-clang" ]
