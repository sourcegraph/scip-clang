load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "scip_proto",
    srcs = ["scip.proto"],
    # Add prefix so generated headers are at scip/scip.pb.h
    import_prefix = "scip",
)

cc_proto_library(
    name = "scip_cc_proto",
    deps = [":scip_proto"],
)

cc_library(
    name = "scip",
    visibility = ["//visibility:public"],
    deps = [":scip_cc_proto"],
)
