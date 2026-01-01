FROM ubuntu:24.04@sha256:c35e29c9450151419d9448b0fd75374fec4fff364a27f176fb458d472dfc9e54 as indexer

RUN apt-get update && apt-get install -y curl libc6-dev python3 build-essential ninja-build git cmake

COPY ./scip-clang /usr/bin/scip-clang
RUN chmod +x /usr/bin/scip-clang && chown $(whoami) /usr/bin/scip-clang

WORKDIR /sources

ENTRYPOINT [ "scip-clang" ]
