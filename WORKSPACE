workspace(name = "scip_clang")

load("//:fetch_deps.bzl", "fetch_direct_dependencies")

fetch_direct_dependencies()

# Setup the toolchain before setting up other dependencies
load("@com_grail_bazel_toolchain//toolchain:deps.bzl", "bazel_toolchain_dependencies")

bazel_toolchain_dependencies()

load("@com_grail_bazel_toolchain//toolchain:rules.bzl", "llvm_toolchain")

llvm_toolchain(
    name = "llvm_toolchain",
    llvm_version = "15.0.6",
)

load("@llvm_toolchain//:toolchains.bzl", "llvm_register_toolchains")

llvm_register_toolchains()

load("//:setup_deps.bzl", "setup_dependencies")

setup_dependencies()
