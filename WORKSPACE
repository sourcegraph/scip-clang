# Minimal WORKSPACE file for Bzlmod
# The workspace name is still needed even with Bzlmod
workspace(name = "scip_clang")

# Bridge configuration for transitioning to Bzlmod
# All dependency management has been moved to MODULE.bazel

# Load and configure LLVM after MODULE.bazel loads dependencies
load("@llvm-raw//utils/bazel:configure.bzl", "llvm_configure")
load("@llvm-raw//utils/bazel:terminfo.bzl", "llvm_terminfo_disable")

# Disable terminfo for LLVM
llvm_terminfo_disable(name = "llvm_terminfo")

# Configure LLVM with limited targets for faster builds
llvm_configure(
    name = "llvm-project",
    targets = ["AArch64", "X86"],
)

# Load boost dependencies  
load("@com_github_nelhage_rules_boost//:boost/boost.bzl", "boost_deps")
boost_deps()

# Load compilation database dependencies
load("@com_grail_bazel_compdb//:deps.bzl", "bazel_compdb_deps")
bazel_compdb_deps()

# Note: Python pip dependencies are handled via MODULE.bazel
