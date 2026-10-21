# Pinned by digest (multi-arch index for ubuntu:24.04) so builds are reproducible.
FROM ubuntu:26.04@sha256:c098726dd9b9733f3e885f8de0ef375206a311b0a317eadc40d347b1115fb944 as indexer

# The base image does not ship ca-certificates; it comes from the Ubuntu archive at build time.
# Fail the build if the archive serves a ca-certificates package that still trusts the
# compromised eMudhra intermediate CA chains (INC-541), which 20260816ubuntu0.24.04.1 removes.
ARG MIN_CA_CERTIFICATES_VERSION=20260816ubuntu0.24.04.1
RUN apt-get update && apt-get install -y ca-certificates curl libc6-dev python3 build-essential ninja-build git cmake \
  && CA_VERSION="$(dpkg-query -W -f='${Version}' ca-certificates)" \
  && { dpkg --compare-versions "$CA_VERSION" ge "$MIN_CA_CERTIFICATES_VERSION" \
       || { echo "ca-certificates $CA_VERSION is older than $MIN_CA_CERTIFICATES_VERSION" >&2; exit 1; }; }

COPY ./scip-clang /usr/bin/scip-clang
RUN chmod +x /usr/bin/scip-clang && chown $(whoami) /usr/bin/scip-clang

WORKDIR /sources

ENTRYPOINT [ "scip-clang" ]
