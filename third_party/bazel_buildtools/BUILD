load("@bazel_skylib//rules:copy_file.bzl", "copy_file")

copy_file(
    name = "buildifier_exe",
    src = select({
        "//config:darwin_arm64": "@buildifier_darwin_arm64//file",
        "//config:linux_x86_64": "@buildifier_linux_amd64//file",
        "//config:linux_arm64": "@buildifier_linux_arm64//file",
    }),
    out = "buildifier",
    allow_symlink = True,
    is_executable = True,
    visibility = ["//tools:__subpackages__"],
)
