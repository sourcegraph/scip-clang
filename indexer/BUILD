cc_library(
    name = "scip-clang-lib",
    srcs = [
        "CliOptions.h",
        "CompilationDatabase.cc",
        "CompilationDatabase.h",
        "Driver.cc",
        "Driver.h",
        "Enforce.h",
        "Exception.cc",
        "Exception.h",
        "FileSystem.h",
        "IpcMessages.cc",
        "IpcMessages.h",
        "JsonIpcQueue.cc",
        "JsonIpcQueue.h",
        "LLVMAdapter.h",
        "LLVMCommandLineParsing.cc",
        "LLVMCommandLineParsing.h",
        "Logging.cc",
        "Logging.h",
        "Timer.cc",
        "Timer.h",
        "Version.h",
        "Worker.cc",
        "Worker.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//indexer/os",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:flat_hash_map",
        "@boost//:date_time",
        "@boost//:interprocess",
        "@boost//:process",
        "@cxxopts",
        "@spdlog",
        "@rapidjson",
        "@llvm-project//llvm:Support",
        # FIXME: The tooling target seems to require at least
        # one backend to be set. Figure out if we can somehow
        # not build any backends at all, which would save on
        # build time.
        "@llvm-project//clang:sema",
        "@llvm-project//clang:ast",
        "@llvm-project//clang:frontend",
        "@llvm-project//clang:tooling",
    ],
)

cc_binary(
    name = "scip-clang",
    srcs = [
        "main.cc",
    ],
    visibility = ["//tools:__pkg__"],
    deps = [":scip-clang-lib"],
)
