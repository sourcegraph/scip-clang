workspace(name = "scip_clang")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "aspect_bazel_lib",
    sha256 = "40ba9d0f62deac87195723f0f891a9803a7b720d7b89206981ca5570ef9df15b",
    strip_prefix = "bazel-lib-2.14.0",
    url = "https://github.com/aspect-build/bazel-lib/releases/download/v2.14.0/bazel-lib-v2.14.0.tar.gz",
)

load("//:fetch_deps.bzl", "fetch_direct_dependencies")

fetch_direct_dependencies()

# Setup the toolchain before setting up other dependencies
load("@toolchains_llvm//toolchain:deps.bzl", "bazel_toolchain_dependencies")

bazel_toolchain_dependencies()

load("//:setup_llvm.bzl", "setup_llvm_toolchain")

setup_llvm_toolchain(name = "llvm_toolchain")

load("@llvm_toolchain//:toolchains.bzl", "llvm_register_toolchains")
load("@rules_python//python:repositories.bzl", "py_repositories", "python_register_toolchains")

llvm_register_toolchains()

python_register_toolchains(
    name = "python_3_10",
    # Remove this once agents stop running tests as root
    # https://github.com/sourcegraph/sourcegraph/issues/47943
    ignore_root_user_error = True,
    python_version = "3.10",
)

py_repositories()

load("@python_3_10//:defs.bzl", "interpreter")
load("@rules_python//python:pip.bzl", "pip_parse")

pip_parse(
    name = "python_deps",
    python_interpreter_target = interpreter,
    requirements = "//tools:requirements_lock.txt",
)

load("//:setup_deps.bzl", "setup_dependencies")

setup_dependencies()
