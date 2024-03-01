load("@toolchains_llvm//toolchain:rules.bzl", grailbio_llvm_toolchain = "llvm_toolchain")

def setup_llvm_toolchain(name):
    # NOTE: The ASan build uses paths which involve the version.
    # Keep the version list in sync with settings.bzl
    mapping = {
        "linux-aarch64": {"version": "17.0.6", "triple": "aarch64-linux-gnu", "sha256": "6dd62762285326f223f40b8e4f2864b5c372de3f7de0731cb7cd55ca5287b75a"},
        "linux-x86_64": {"version": "17.0.6", "triple": "x86_64-linux-gnu-ubuntu-22.04", "sha256": "884ee67d647d77e58740c1e645649e29ae9e8a6fe87c1376be0f3a30f3cc9ab3"},
        "darwin-aarch64": {"version": "17.0.6", "triple": "arm64-apple-darwin22.0", "sha256": "1264eb3c2a4a6d5e9354c3e5dc5cb6c6481e678f6456f36d2e0e566e9400fcad"},
        "darwin-arm64": {"version": "17.0.6", "triple": "arm64-apple-darwin22.0", "sha256": "1264eb3c2a4a6d5e9354c3e5dc5cb6c6481e678f6456f36d2e0e566e9400fcad"},
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
