load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
load("@com_grail_bazel_compdb//:deps.bzl", "bazel_compdb_deps")
load("@llvm-raw//utils/bazel:configure.bzl", "llvm_configure")
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")
load("@python_deps//:requirements.bzl", install_python_deps = "install_deps")

def setup_dependencies():
    bazel_skylib_workspace()
    boost_deps()
    bazel_compdb_deps()

    # FIXME: Should we allow all targets in a release build?
    # Limit the number of backends here to save on compile time for now.
    llvm_configure(name = "llvm-project", targets = ["AArch64", "X86"])

    protobuf_deps()

    install_python_deps()
