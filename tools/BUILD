# TODO(def: cpp-linters): Add clang-format and clang-tidy usage here.

load("@com_grail_bazel_compdb//:defs.bzl", "compilation_database")
load("@com_grail_bazel_output_base_util//:defs.bzl", "OUTPUT_BASE")

compilation_database(
    name = "compdb",
    testonly = True,
    output_base = OUTPUT_BASE,
    targets = [
        "//indexer/os",
        "//indexer:scip-clang",
        "//indexer:scip-clang-lib",
        "//test:test_main",
        "//test:ipc_test_main",
    ],
)

genrule(
    name = "check",
    srcs = glob(["**/*.py"]),
    outs = ["check.log"],
    cmd_bash = """(
        $(PYTHON3)  $(location //tools/linter:python_check) --check --verbose $(SRCS)
        ) | tee $@
    """,
    tools = ["//tools/linter:python_check"],
    toolchains=["@rules_python//python:current_py_toolchain"]
)
