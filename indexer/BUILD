load("//:settings.bzl", "ASAN_LINKOPTS")

cc_library(
    name = "scip-clang-lib",
    srcs = glob(
        [
            "*.h",
            "*.cc",
        ],
        exclude = ["main.cc"],
    ),
    visibility = ["//visibility:public"],
    deps = [
        "//indexer/os",
        "//proto:fwd_decls",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/functional:function_ref",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/debugging:failure_signal_handler",
        "@com_google_perfetto//:perfetto",
        "@boost//:date_time",
        "@boost//:interprocess",
        "@boost//:process",
        "@cxxopts",
        "@spdlog",
        "@rapidjson",
        "@wyhash",
        "@llvm-project//llvm:Support",
        # FIXME: The tooling target seems to require at least
        # one backend to be set. Figure out if we can somehow
        # not build any backends at all, which would save on
        # build time.
        "@llvm-project//clang:sema",
        "@llvm-project//clang:ast",
        "@llvm-project//clang:frontend",
        "@llvm-project//clang:tooling",
        "@scip",
    ],
)

cc_binary(
    name = "scip-clang",
    srcs = [
        "main.cc",
    ],
    linkopts = select({
        "//:asan_linkopts": ASAN_LINKOPTS,
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
    deps = [":scip-clang-lib"],
)
