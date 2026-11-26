# Pinned by digest (multi-arch index for ubuntu:26.04) so builds are reproducible.
FROM ubuntu:26.04@sha256:d66b366dac61b575bee2aef07e9c26e8070096fbe84a3d4d3eb0da069b5d76db as indexer

# The base image does not ship ca-certificates; it comes from the Ubuntu archive at build time.
# Fail the build if the archive serves a ca-certificates package that still trusts the
# compromised eMudhra intermediate CA chains (INC-541), which 20260816ubuntu0.26.04.1 removes.
ARG MIN_CA_CERTIFICATES_VERSION=20260816ubuntu0.26.04.1
# Upgrade the base image's packages too, so every rebuild picks up Ubuntu security updates.
RUN apt-get update && apt-get upgrade -y && apt-get install -y ca-certificates curl libc6-dev python3 build-essential ninja-build git cmake \
  && CA_VERSION="$(dpkg-query -W -f='${Version}' ca-certificates)" \
  && { dpkg --compare-versions "$CA_VERSION" ge "$MIN_CA_CERTIFICATES_VERSION" \
       || { echo "ca-certificates $CA_VERSION is older than $MIN_CA_CERTIFICATES_VERSION" >&2; exit 1; }; }

COPY ./scip-clang /usr/bin/scip-clang
RUN chmod +x /usr/bin/scip-clang && chown $(whoami) /usr/bin/scip-clang

WORKDIR /sources

ENTRYPOINT [ "scip-clang" ]
