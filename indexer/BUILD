cc_binary(
    name = "scip-clang",
    srcs = [
        "CompilationDatabase.cc",
        "CompilationDatabase.h",
        "Driver.cc",
        "Driver.h",
        "IpcMessages.cc",
        "IpcMessages.h",
        "JsonIpcQueue.cc",
        "JsonIpcQueue.h",
        "LLVMAdapter.h",
        "LLVMCommandLineParsing.h",
        "LLVMCommandLineParsing.cc",
        "Logging.cc",
        "Logging.h",
        "FileSystem.h",
        "Worker.cc",
        "Worker.h",
        "main.cc",
    ],
    deps = [
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/container:flat_hash_map",
        "@boost//:date_time",
        "@boost//:interprocess",
        "@boost//:process",
        "@spdlog",
        "@rapidjson",
        "@llvm-project//llvm:Support",
        # FIXME: Figure out how to not build any backends at all.
        # Right now, this requires all different backends to be
        # compiled and linked. Looks like there is a variable
        # LLVM_TARGETS_TO_BUILD which controls this
        "@llvm-project//clang:tooling",
        # "@llvm-project//clang:ast",
    ],
)

# load("//tools:clang.bzl", "clang_tool")
load("@com_grail_bazel_compdb//:defs.bzl", "compilation_database")
load("@com_grail_bazel_output_base_util//:defs.bzl", "OUTPUT_BASE")

# clang_tool("clang-format")
# clang_tool("clang-tidy")
# clang_tool("opt")

compilation_database(
    name = "compdb",
    testonly = True,
    output_base = OUTPUT_BASE,
    targets = [
        "//indexer:scip-clang",
    ],
)
