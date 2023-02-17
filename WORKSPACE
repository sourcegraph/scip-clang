workspace(name = "scip_clang")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

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

http_archive(
    name = "rules_python",
    sha256 = "29a801171f7ca190c543406f9894abf2d483c206e14d6acbd695623662320097",
    strip_prefix = "rules_python-0.18.1",
    url = "https://github.com/bazelbuild/rules_python/releases/download/0.18.1/rules_python-0.18.1.tar.gz",
)

load("@rules_python//python:repositories.bzl", "py_repositories", "python_register_toolchains")

py_repositories()

python_register_toolchains(
    name = "python39",
    # Available versions are listed in @rules_python//python:versions.bzl.
    # We recommend using the same version your team is already standardized on.
    python_version = "3.9",
)


load("@python39//:defs.bzl", "interpreter")
load("@rules_python//python:pip.bzl", "pip_parse")


pip_parse(
   name = "pypi",
   requirements = "//tools/linter:requirements_lock.txt",
   python_interpreter_target = interpreter,
)

load("@pypi//:requirements.bzl", "install_deps")

# load("@rules_python//python:defs.bzl", "py_binary")

install_deps()
