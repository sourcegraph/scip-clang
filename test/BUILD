load(":test_suite.bzl", "scip_clang_test_suite")

cc_binary(
    name = "test_main",
    testonly = 1,
    srcs = glob([
        "*.cc",
        "*.h",
    ]),
    visibility = ["//tools:__pkg__"],
    deps = [
        "//indexer:scip-clang-lib",
        "@com_google_absl//absl/strings",
        "@cxxopts",
        "@doctest",
        "@doctest//:doctest_custom_main",
        "@dtl",
        "@llvm-project//llvm:Support",
        "@spdlog",
    ],
)

scip_clang_test_suite(
    compdb_data = glob([
        "compdb/*.json",
        "compdb/*.snapshot.yaml",
    ]),
    preprocessor_data = glob([
        "preprocessor/**/*.yaml",
        "preprocessor/**/*.cc",
        "preprocessor/**/*.h",
    ]),
)
