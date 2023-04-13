load("@com_grail_bazel_toolchain//toolchain:rules.bzl", grailbio_llvm_toolchain = "llvm_toolchain")

def setup_llvm_toolchain(name):
    # NOTE: The ASan build uses paths which involve the version.
    # Keep the version list in sync with settings.bzl
    mapping = {
        # Even though we're using zig cc on Linux, keep these
        # in to keep clang-format working in CI until we figure
        # out a more lightweight solution.
        "linux-aarch64": {"version": "15.0.6", "triple": "aarch64-linux-gnu", "sha256": "8ca4d68cf103da8331ca3f35fe23d940c1b78fb7f0d4763c1c059e352f5d1bec"},
        "linux-x86_64": {"version": "15.0.6", "triple": "x86_64-linux-gnu-ubuntu-18.04", "sha256": "38bc7f5563642e73e69ac5626724e206d6d539fbef653541b34cae0ba9c3f036"},
        "darwin-arm64": {"version": "15.0.6", "triple": "arm64-apple-darwin21.0", "sha256": "32bc7b8eee3d98f72dd4e5651e6da990274ee2d28c5c19a7d8237eb817ce8d91"},
        "darwin-x86_64": {"version": "15.0.7", "triple": "x86_64-apple-darwin21.0", "sha256": "d16b6d536364c5bec6583d12dd7e6cf841b9f508c4430d9ee886726bd9983f1c"},
        "windows": {"version": "15.0.6", "sha256": "8c0305c569391fb842c3a1edb07e63a2c0145a2a601d5a87992ae402b437c48f", "url": "https://github.com/sourcegraph/llvm-toolchain-archives/releases/download/v0-20223-04-10/LLVM-15.0.6-win64.tar.xz"},
    }
    llvm_versions, sha256, strip_prefix, urls = {}, {}, {}, {}
    strip_prefix["windows"] = "LLVM-15.0.6-win64"
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
