# NOTE(ref: based-on-sorbet): Based on Sorbet's common/os package.
cc_library(
    name = "os",
    srcs = [
        "Os.cc",
        "Os.h",
    ] + select({
        "@platforms//os:linux": ["Linux.cc"],
        "@platforms//os:macos": ["macOS.cc"],
    }),
    hdrs = [
        "Os.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/debugging:symbolize",
        "@spdlog",
    ],
)
