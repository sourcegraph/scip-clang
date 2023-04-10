workspace(name = "scip_clang")

load("//:fetch_deps.bzl", "fetch_direct_dependencies")

fetch_direct_dependencies()

load("@io_tweag_rules_nixpkgs//nixpkgs:repositories.bzl", "rules_nixpkgs_dependencies")
rules_nixpkgs_dependencies()

load("@io_tweag_rules_nixpkgs//nixpkgs:nixpkgs.bzl", "nixpkgs_git_repository", "nixpkgs_package", "nixpkgs_cc_configure")
nixpkgs_git_repository(
    name = "nixpkgs",
    revision = "nixpkgs-unstable",
)

nixpkgs_cc_configure(
    name = "toolchain-darwin-aarch64",
    # exec_constraints = [
    #     "@platforms//cpu:x86_64",
    #     "@platforms//os:osx",
    # ],
    repository = "@nixpkgs",
    attribute_path = "clang_15",
    # nix_file_content = "(import <nixpkgs> {}).clang_15",
    # target_constraints = [
    #     "@platforms//cpu:arm64",
    #     "@platforms//os:osx",
    # ],
)

# load("@rules_cc//cc:repositories.bzl", "rules_cc_dependencies", "rules_cc_toolchains")

# rules_cc_dependencies()

# rules_cc_toolchains()

load("@rules_python//python:repositories.bzl", "py_repositories", "python_register_toolchains")

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
