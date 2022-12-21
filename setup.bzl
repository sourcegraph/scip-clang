load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

_RULES_BOOST_COMMIT = "e83dfef18d91a3e35c8eac9b9aeb1444473c0efd"
_LLVM_COMMIT = "b6e344ce91c8796331fca7644eb8c748ac5391ec"
_ABSL_COMMIT = "9a2c7bf98fa2482d0cbba727dcc4499e6e7c5ee2"
_SPDLOG_COMMIT = "edc51df1bdad8667b628999394a1e7c4dc6f3658"
_BAZEL_SKYLIB_VERSION = "1.3.0"
_RAPIDJSON_COMMIT = "a98e99992bd633a2736cc41f96ec85ef0c50e44d"

def scip_clang_rule_repositories():
    http_archive(
        name = "bazel_skylib",
        sha256 = "74d544d96f4a5bb630d465ca8bbcfe231e3594e5aae57e1edbf17a6eb3ca2506",
        urls = [
            "https://github.com/bazelbuild/bazel-skylib/releases/download/{0}/bazel-skylib-{0}.tar.gz".format(_BAZEL_SKYLIB_VERSION),
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/{0}/bazel-skylib-{0}.tar.gz".format(_BAZEL_SKYLIB_VERSION),
        ],
    )

    http_archive(
        name = "com_github_nelhage_rules_boost",
        sha256 = "dc9140b868de82ae46dd44da73a7d9749b680b1e7d63b0912288c2de2cabcb1d",
        strip_prefix = "rules_boost-%s" % _RULES_BOOST_COMMIT,
        urls = [
            "https://github.com/nelhage/rules_boost/archive/%s.tar.gz" % _RULES_BOOST_COMMIT,
        ],
    )

    http_archive(
        name = "com_grail_bazel_compdb",
        sha256 = "d32835b26dd35aad8fd0ba0d712265df6565a3ad860d39e4c01ad41059ea7eda",
        strip_prefix = "bazel-compilation-database-0.5.2",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/0.5.2.tar.gz"],
    )

    http_archive(
        name = "net_zlib",
        build_file = "@scip_clang//third_party:zlib.BUILD",
        sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
        strip_prefix = "zlib-1.2.11",
        urls = [
            "https://mirror.bazel.build/zlib.net/zlib-1.2.11.tar.gz",
            "https://zlib.net/zlib-1.2.11.tar.gz",
        ],
    )

    http_archive(
        name = "llvm_upstream",
        sha256 = "e5be7d76ba3f801254e5110fc84e2ffbf38267dd5f1bb9f7243ba2099f2d8cdd",
        strip_prefix = "llvm-project-%s" % _LLVM_COMMIT,
        build_file_content = "# empty",
        urls = ["https://github.com/llvm/llvm-project/archive/%s.tar.gz" % _LLVM_COMMIT],
    )

    http_archive(
        name = "com_google_absl",
        sha256 = "0db3f1408edf4e0eb12bd6c46fc01465a009feb2789a2b21ef40f91744a25783",
        strip_prefix = "abseil-cpp-%s" % _ABSL_COMMIT,
        urls = ["https://github.com/abseil/abseil-cpp/archive/%s.zip" % _ABSL_COMMIT],
    )

    http_archive(
        name = "rapidjson",
        sha256 = "c79acb593f1954b2b217feb170549cb58fe4b9edac48e1d4e7285e03235c54d2",
        build_file = "@scip_clang//third_party:rapidjson.BUILD",
        strip_prefix = "rapidjson-%s" % _RAPIDJSON_COMMIT,
        urls = ["https://github.com/Tencent/rapidjson/archive/%s.zip" % _RAPIDJSON_COMMIT],
    )

    # NOTE: fmt also comes through spdlog, we don't have an explicit dep on fmt.
    http_archive(
        name = "spdlog",
        sha256 = "93a270dd7ec8fa672eb4feaef443dc14a4a9edc7b59aea998ae5da6cbf7b7119",
        build_file = "@scip_clang//third_party:spdlog.BUILD",
        strip_prefix = "spdlog-%s" % _SPDLOG_COMMIT,
        urls = ["https://github.com/gabime/spdlog/archive/%s.tar.gz" % _SPDLOG_COMMIT],
    )
