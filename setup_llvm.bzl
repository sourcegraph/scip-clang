load("@toolchains_llvm//toolchain:rules.bzl", grailbio_llvm_toolchain = "llvm_toolchain")

def setup_llvm_toolchain(name):
    # NOTE: The ASan build uses paths which involve the version.
    # Keep the version list in sync with settings.bzl
    # At the time of bumping this, the latest version is 17.0.6,
    # but we use 16.0.0 because:
    # - Versions later than 16.0.0 are built on Ubuntu 22.04 or newer,
    #   whereas 16.0.0 is built on Ubuntu 18.04. The newer binaries require
    #   a newer glibc version which isn't currently present on Buildkite machines.
    # - Our release pipeline uses Ubuntu 18.04 so that the binaries work on
    #   older Debian and Ubuntu versions. Using newer toolchain binaries would
    #   hence not work in our release pipeline.
    mapping = {
        "linux-aarch64": {"version": "16.0.0", "triple": "aarch64-linux-gnu", "sha256": "b750ba3120e6153fc5b316092f19b52cf3eb64e19e5f44bd1b962cb54a20cf0a"},
        "linux-x86_64": {"version": "16.0.0", "triple": "x86_64-linux-gnu-ubuntu-18.04", "sha256": "2b8a69798e8dddeb57a186ecac217a35ea45607cb2b3cf30014431cff4340ad1"},
        "darwin-aarch64": {"version": "16.0.0", "triple": "arm64-apple-darwin22.0", "sha256": "2041587b90626a4a87f0de14a5842c14c6c3374f42c8ed12726ef017416409d9"},
        "darwin-arm64": {"version": "16.0.0", "triple": "arm64-apple-darwin22.0", "sha256": "2041587b90626a4a87f0de14a5842c14c6c3374f42c8ed12726ef017416409d9"},
    }
    llvm_versions, sha256, strip_prefix, urls = {}, {}, {}, {}
    for (k, v) in mapping.items():
        llvm_versions[k] = v["version"]
        sha256[k] = v["sha256"]
        if "url" in v:
            urls[k] = [v["url"]]
        else:
            prefix = "clang+llvm-{version}-{triple}".format(version = v["version"], triple = v["triple"])
            strip_prefix[k] = prefix
            urls[k] = ["https://github.com/llvm/llvm-project/releases/download/llvmorg-{version}/{prefix}.tar.xz".format(version = v["version"], prefix = prefix)]

    grailbio_llvm_toolchain(
        name = name,
        llvm_versions = llvm_versions,
        strip_prefix = strip_prefix,
        urls = urls,
        sha256 = sha256,
    )
