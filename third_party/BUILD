load("@bazel_skylib//rules:copy_file.bzl", "copy_file")

copy_file(
    name = "actionlint_exe",
    src = select({
        "//config:darwin_arm64": "@actionlint_darwin_arm64//:actionlint",
        "//config:linux_x86_64": "@actionlint_linux_amd64//:actionlint",
        "//config:linux_arm64": "@actionlint_linux_arm64//:actionlint",
    }),
    out = "actionlint",
    allow_symlink = True,
    is_executable = True,
    visibility = ["//tools:__subpackages__"],
)
