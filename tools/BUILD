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
    ],
)
