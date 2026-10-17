FROM ubuntu:24.04@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac as indexer

RUN apt-get update && apt-get install -y curl libc6-dev python3 build-essential ninja-build git cmake

COPY ./scip-clang /usr/bin/scip-clang
RUN chmod +x /usr/bin/scip-clang && chown $(whoami) /usr/bin/scip-clang

WORKDIR /sources

ENTRYPOINT [ "scip-clang" ]
