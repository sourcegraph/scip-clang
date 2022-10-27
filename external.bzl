load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
load("@com_grail_bazel_compdb//:deps.bzl", "bazel_compdb_deps")
load("@llvm_upstream//utils/bazel:configure.bzl", "llvm_configure")
load("@llvm_upstream//utils/bazel:terminfo.bzl", "llvm_terminfo_disable")
load("@llvm_upstream//utils/bazel:zlib.bzl", "llvm_zlib_external")

def scip_clang_dependencies():
    bazel_skylib_workspace()
    boost_deps()
    bazel_compdb_deps()

    llvm_terminfo_disable(name = "llvm_terminfo")
    llvm_zlib_external(name = "llvm_zlib", external_zlib = "@net_zlib//:zlib")
    llvm_configure(name = "llvm-project")
