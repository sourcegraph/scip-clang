FROM ubuntu:22.04@sha256:3c61d3759c2639d4b836d32a2d3c83fa0214e36f195a3421018dbaaf79cbe37f as indexer

RUN apt-get update && apt-get install -y curl libc6-dev python3 build-essential ninja-build git cmake

COPY ./scip-clang /usr/bin/scip-clang
RUN chmod +x /usr/bin/scip-clang && chown $(whoami) /usr/bin/scip-clang

WORKDIR /sources

ENTRYPOINT [ "scip-clang" ]
