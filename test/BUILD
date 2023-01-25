load(":test_suite.bzl", "scip_clang_test_suite")

cc_binary(
    name = "test_main",
    testonly = 1,
    srcs = ["test_main.cc"],
    visibility = ["//tools:__pkg__"],
    deps = [
        "//indexer:scip-clang-lib",
        "@boost//:process",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/strings",
        "@cxxopts",
        "@doctest//doctest",
        "@doctest//doctest:custom_main",
        "@dtl",
        "@llvm-project//llvm:Support",
        "@spdlog",
    ],
)

cc_binary(
    name = "ipc_test_main",
    testonly = 1,
    srcs = ["ipc_test_main.cc"],
    visibility = ["//tools:__pkg__"],
    deps = [
        "//indexer:scip-clang-lib",
        "@boost//:process",
        "@llvm-project//llvm:Support",
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
    robustness_data = glob(["robustness/*"]),
)
