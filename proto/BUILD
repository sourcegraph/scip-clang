load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "fwd_decls_proto",
    srcs = ["fwd_decls.proto"],
)

cc_proto_library(
    name = "fwd_decls_cc_proto",
    deps = [":fwd_decls_proto"],
)

cc_library(
    name = "fwd_decls",
    visibility = ["//visibility:public"],
    deps = [":fwd_decls_cc_proto"],
)
