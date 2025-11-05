load("@rules_cc//cc:defs.bzl", "cc_proto_library", "cc_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")

proto_library(
    name = "scip_proto",
    srcs = ["scip.proto"],
)

cc_proto_library(
    name = "scip_cc_proto",
    deps = [":scip_proto"],
)

# Wrapper to provide the expected include path for scip/scip.pb.h
cc_library(
    name = "scip",
    hdrs = [":scip_cc_proto"],
    include_prefix = "scip",
    visibility = ["//visibility:public"],
    deps = [":scip_cc_proto"],
)
