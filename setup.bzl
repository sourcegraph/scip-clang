load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

_RULES_BOOST_COMMIT = "652b21e35e4eeed5579e696da0facbe8dba52b1f"
_LLVM_COMMIT = "ada9ab610727917561370e976eaea26dbbc20cce"
_ABSL_COMMIT = "522606b7fae37836c138e83f6eec0eabb9947dc0"
_SPDLOG_COMMIT = "8512000f36c2ad9b1265bd78b11c0b34151d6be4"
_BAZEL_SKYLIB_VERSION = "1.3.0"

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
        sha256 = "c1b8b2adc3b4201683cf94dda7eef3fc0f4f4c0ea5caa3ed3feffe07e1fb5b15",
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
        sha256 = "20b1c322fe4b8cec3c6109e878628016f668bca225466580fd9a4da34f09bb18",
        strip_prefix = "llvm-project-%s" % _LLVM_COMMIT,
        build_file_content = "# empty",
        urls = ["https://github.com/llvm/llvm-project/archive/%s.tar.gz" % _LLVM_COMMIT],
    )

    http_archive(
        name = "com_google_absl",
        sha256 = "31b0b6fe3f14875a27c48d5775d05e157bab233065f7c55f0e1f4991c5e95840",
        strip_prefix = "abseil-cpp-%s" % _ABSL_COMMIT,
        urls = ["https://github.com/abseil/abseil-cpp/archive/%s.zip" % _ABSL_COMMIT],
    )

    # NOTE: fmt also comes through spdlog, we don't have an explicit dep on fmt.
    http_archive(
        name = "spdlog",
        sha256 = "413a919a6831e8e86b8cc38ffcbefc1eecfe3ffd365ff7d1e70ec13b91bd160d",
        build_file = "@scip_clang//third_party:spdlog.BUILD",
        strip_prefix = "spdlog-%s" % _SPDLOG_COMMIT,
        urls = ["https://github.com/gabime/spdlog/archive/%s.tar.gz" % _SPDLOG_COMMIT],
    )