FROM ubuntu:26.04@sha256:513c074113a871b51a8d16ab445c88779d6452d937a164fb5cc479f32668a41d as indexer

RUN apt-get update && apt-get install -y curl libc6-dev python3 build-essential ninja-build git cmake

COPY ./scip-clang /usr/bin/scip-clang
RUN chmod +x /usr/bin/scip-clang && chown $(whoami) /usr/bin/scip-clang

WORKDIR /sources

ENTRYPOINT [ "scip-clang" ]
