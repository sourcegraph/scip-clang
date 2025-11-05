load("@rules_cc//cc:defs.bzl", "cc_library", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")

proto_library(
    name = "fwd_decls_proto",
    srcs = ["fwd_decls.proto"],
)

cc_proto_library(
    name = "fwd_decls",
    visibility = ["//visibility:public"],
    deps = [":fwd_decls_proto"],
)
