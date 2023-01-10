load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library")

cc_proto_library(
    name = "proto",
    srcs = ["scip.proto"],
)

cc_library(
    name = "scip",
    srcs = ["scip.pb.cc"],
    hdrs = ["scip.pb.h"],
    deps = [":proto"],
    include_prefix = "scip",
    visibility = ["//visibility:public"],
)