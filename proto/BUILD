load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library")

cc_proto_library(
    name = "proto",
    srcs = ["fwd_decls.proto"],
)

cc_library(
    name = "fwd_decls",
    srcs = ["fwd_decls.pb.cc"],
    hdrs = ["fwd_decls.pb.h"],
    copts = [
        "-Wno-unused-function",
        # Generated code uses sprintf in some places, which
        # is deprecated on macOS
        "-Wno-deprecated-declarations",
        "-Wno-covered-switch-default",
    ],
    visibility = ["//visibility:public"],
    deps = [":proto"],
)
