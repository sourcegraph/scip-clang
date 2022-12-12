cc_library(
    name = "spdlog",
    srcs = glob([
        "src/**/*.cpp",
    ]),
    hdrs = glob([
        "include/spdlog/**/*.h",
    ]),
    copts = [
        "-Iexternal/spdlog/",
    ],
    defines = [
        # Use a compiled library instead of the header-only version
        "SPDLOG_COMPILED_LIB",
    ],
    includes = [
        "include/",
    ],
    linkopts = [
        "-lpthread",
    ],
    linkstatic = 1,
    visibility = ["//visibility:public"],
)
