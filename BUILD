load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

bool_flag(
    name = "link_asan_runtime",
    build_setting_default = False,
)

config_setting(
    name = "asan_linkopts",
    flag_values = {
        "//:link_asan_runtime": "true",
    },
)
