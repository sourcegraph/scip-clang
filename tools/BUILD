# TODO(def: cpp-linters): Add clang-format and clang-tidy usage here.

load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_python//python:pip.bzl", "compile_pip_requirements")
load("@python_deps//:requirements.bzl", pip_requirement = "requirement")

refresh_compile_commands(
    name = "refresh_compile_commands",
    exclude_external_sources = True,
    exclude_headers = "external",
    targets = [
        "//proto:fwd_decls",
        "//indexer/os",
        "//indexer:scip-clang",
        "//indexer:scip-clang-lib",
        "//test:test_main",
        "//test:ipc_test_main",
    ],
)

compile_pip_requirements(
    name = "requirements",
    # Suppress warning about changing resolver strategy in Python 3.10
    extra_args = ["--resolver=backtracking"],
    requirements_in = "requirements.in",
    requirements_txt = "requirements_lock.txt",
)

py_binary(
    name = "reformat_python",
    srcs = ["reformat_python.py"],
    deps = [pip_requirement("black")],
)

py_binary(
    name = "analyze_pp_trace",
    srcs = ["analyze_pp_trace.py"],
    deps = [pip_requirement("ruamel.yaml")],
)
