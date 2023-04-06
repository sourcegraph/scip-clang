# NOTE(ref: based-on-sorbet): Based on Sorbet's common/os package.
cc_library(
    name = "os",
    srcs = glob(
        [
            "*.cc",
            "*.h",
        ],
        allow_empty = False,
    ),
    hdrs = [
        "Os.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/debugging:symbolize",
        "@spdlog",
    ],
)
