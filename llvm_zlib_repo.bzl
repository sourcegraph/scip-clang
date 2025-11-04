"""Repository rule for creating llvm_zlib wrapper that re-exports zlib headers."""

def _llvm_zlib_repo_impl(ctx):
    ctx.file("BUILD.bazel", """
cc_library(
    name = "zlib",
    defines = ["LLVM_ENABLE_ZLIB=1"],
    hdrs = ["@zlib//:public_headers"],
    deps = ["@zlib//:zlib"],
    visibility = ["//visibility:public"],
)
""")
    ctx.file("WORKSPACE", "")

llvm_zlib_repo = repository_rule(
    implementation = _llvm_zlib_repo_impl,
)
