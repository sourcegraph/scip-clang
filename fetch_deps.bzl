load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

_BAZEL_SKYLIB_VERSION = "1.3.0"
_PLATFORMS_COMMIT = "3fbc687756043fb58a407c2ea8c944bc2fe1d922"  # 2022 Nov 10
_BAZEL_TOOLCHAIN_VERSION = "0.8.2"
_RULES_BOOST_COMMIT = "ed40cc3c6fb76fe46f40150ebb81ef6a625b6e36"
_LLVM_COMMIT = "e0f3110b854a476c16cce7b44472cd7838d344e9"  # Keep in sync with Version.h
_ABSL_COMMIT = "4ffaea74c1f5408e0757547a1ca0518ad43fa9f1"
_CXXOPTS_VERSION = "3.0.0"
_RAPIDJSON_COMMIT = "a98e99992bd633a2736cc41f96ec85ef0c50e44d"
_WYHASH_COMMIT = "ea3b25e1aef55d90f707c3a292eeb9162e2615d8"
_SPDLOG_COMMIT = "edc51df1bdad8667b628999394a1e7c4dc6f3658"
_PROTOBUF_VERSION = "3.21.12"
_SCIP_COMMIT = "aa0e511dcfefbacc3b96dcc2fe2abd9894416b1e"
# ^ When bumping this version, check if any new fields are introduced
# in the types for which we implement hashing and comparison in
# indexer/ScipExtras.{h,cc}

# When bumping the Perfetto version, tweak the patch to also
# remove the use of 'PERFETTO_NO_SANITIZE_UNDEFINED'
# See https://github.com/google/perfetto/issues/271#issuecomment-1527691232
_PERFETTO_VERSION = "33.1"  # Keep in sync with docs/Development.md
_DOCTEST_VERSION = "2.4.9"
_DTL_VERSION = "1.20"
_RULES_PYTHON_VERSION = "0.18.1"

def fetch_direct_dependencies():
    http_archive(
        name = "bazel_skylib",
        sha256 = "74d544d96f4a5bb630d465ca8bbcfe231e3594e5aae57e1edbf17a6eb3ca2506",
        urls = [
            "https://github.com/bazelbuild/bazel-skylib/releases/download/{0}/bazel-skylib-{0}.tar.gz".format(_BAZEL_SKYLIB_VERSION),
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/{0}/bazel-skylib-{0}.tar.gz".format(_BAZEL_SKYLIB_VERSION),
        ],
    )

    http_archive(
        name = "platforms",
        sha256 = "b4a3b45dc4202e2b3e34e3bc49d2b5b37295fc23ea58d88fb9e01f3642ad9b55",
        strip_prefix = "platforms-%s" % _PLATFORMS_COMMIT,
        urls = ["https://github.com/bazelbuild/platforms/archive/%s.zip" % _PLATFORMS_COMMIT],
    )

    http_archive(
        name = "com_grail_bazel_toolchain",
        sha256 = "c4436850f2edece101371b56971a760aee4b65c0800d55a8b1b8e3f52296ebdd",
        strip_prefix = "bazel-toolchain-%s" % _BAZEL_TOOLCHAIN_VERSION,
        urls = [
            "https://github.com/grailbio/bazel-toolchain/archive/refs/tags/%s.zip" % _BAZEL_TOOLCHAIN_VERSION,
        ],
    )

    http_archive(
        name = "com_github_nelhage_rules_boost",
        sha256 = "2f3120bb4dd42ad58ff5677d4fa6ecbab34a93d94a8ad66f9b07951bff59cdbf",
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
        name = "com_google_perfetto",
        sha256 = "09b3271d3829a13b400447353d442a65f8f88e2df5a26f96778ab66c4cd26ec1",
        strip_prefix = "perfetto-%s/sdk" % _PERFETTO_VERSION,
        build_file = "//third_party:perfetto.BUILD",
        urls = ["https://github.com/google/perfetto/archive/v%s.tar.gz" % _PERFETTO_VERSION],
        patch_args = ["-p1"],
        patches = ["//third_party:perfetto.patch"],
    )

    # Keep the name 'zlib' so that Protobuf doesn't pull in another copy.
    #
    # https://sourcegraph.com/github.com/protocolbuffers/protobuf/-/blob/protobuf_deps.bzl?L48-58
    http_archive(
        name = "zlib",
        build_file = "@scip_clang//third_party:zlib.BUILD",
        sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
        strip_prefix = "zlib-1.2.11",
        urls = [
            "https://mirror.bazel.build/zlib.net/zlib-1.2.11.tar.gz",
            "https://zlib.net/zlib-1.2.11.tar.gz",
        ],
    )

    http_archive(
        name = "llvm_zstd",
        build_file = "@llvm-raw//utils/bazel/third_party_build:zstd.BUILD",
        sha256 = "7c42d56fac126929a6a85dbc73ff1db2411d04f104fae9bdea51305663a83fd0",
        strip_prefix = "zstd-1.5.2",
        urls = [
            "https://github.com/facebook/zstd/releases/download/v1.5.2/zstd-1.5.2.tar.gz",
        ],
    )

    http_archive(
        name = "llvm-raw",
        sha256 = "04b76a5be88331f71a4e4fe96bccfebec302ddd0dbd9418fd5c186a7361c54fb",
        strip_prefix = "llvm-project-%s" % _LLVM_COMMIT,
        build_file_content = "# empty",
        urls = ["https://github.com/llvm/llvm-project/archive/%s.tar.gz" % _LLVM_COMMIT],
    )

    http_archive(
        name = "com_google_protobuf",
        sha256 = "f7042d540c969b00db92e8e1066a9b8099c8379c33f40f360eb9e1d98a36ca26",
        urls = ["https://github.com/protocolbuffers/protobuf/archive/v%s.zip" % _PROTOBUF_VERSION],
        strip_prefix = "protobuf-%s" % _PROTOBUF_VERSION,
    )

    http_archive(
        name = "scip",
        sha256 = "b1d2fc009345857aa32cdddec11b75ce1e5c20430f668044231ed309d48b7355",
        build_file = "@scip_clang//third_party:scip.BUILD",
        strip_prefix = "scip-%s" % _SCIP_COMMIT,
        urls = ["https://github.com/sourcegraph/scip/archive/%s.zip" % _SCIP_COMMIT],
    )

    # Keep the name 'com_google_absl' so that Protobuf doesn't pull in
    # another copy.
    #
    # https://sourcegraph.com/github.com/protocolbuffers/protobuf/-/blob/protobuf_deps.bzl?L39-46
    http_archive(
        name = "com_google_absl",
        sha256 = "fee8ec623d8bbf0ecb9563a8e08ae319d1ca9fdf8c1c84384520a6992f571659",
        strip_prefix = "abseil-cpp-%s" % _ABSL_COMMIT,
        urls = ["https://github.com/abseil/abseil-cpp/archive/%s.zip" % _ABSL_COMMIT],
        patch_args = ["-p1"],
        patches = ["//third_party:abseil.patch"],
    )

    # Abseil also has a flags/argument parsing library, but let's
    # avoid that because it (arguably) needlessly uses global variables.
    http_archive(
        name = "cxxopts",
        sha256 = "1eefdf5af3ba0c66458258de05df2a113262ad5e85cac489de0a456088e9f9b0",
        build_file = "@scip_clang//third_party:cxxopts.BUILD",
        strip_prefix = "cxxopts-%s" % _CXXOPTS_VERSION,
        urls = ["https://github.com/jarro2783/cxxopts/archive/v%s.zip" % _CXXOPTS_VERSION],
    )

    http_archive(
        name = "rapidjson",
        sha256 = "c79acb593f1954b2b217feb170549cb58fe4b9edac48e1d4e7285e03235c54d2",
        build_file = "@scip_clang//third_party:rapidjson.BUILD",
        strip_prefix = "rapidjson-%s" % _RAPIDJSON_COMMIT,
        urls = ["https://github.com/Tencent/rapidjson/archive/%s.zip" % _RAPIDJSON_COMMIT],
    )

    http_archive(
        name = "wyhash",
        sha256 = "ac8ff5dee1f6861614bbb1f2f5a0d57027574cc5fb56e2c47ac69ea2de30bbb0",
        build_file = "@scip_clang//third_party:wyhash.BUILD",
        strip_prefix = "wyhash-%s" % _WYHASH_COMMIT,
        urls = ["https://github.com/wangyi-fudan/wyhash/archive/%s.zip" % _WYHASH_COMMIT],
    )

    # NOTE: fmt also comes through spdlog, we don't have an explicit dep on fmt.
    http_archive(
        name = "spdlog",
        sha256 = "93a270dd7ec8fa672eb4feaef443dc14a4a9edc7b59aea998ae5da6cbf7b7119",
        build_file = "@scip_clang//third_party:spdlog.BUILD",
        strip_prefix = "spdlog-%s" % _SPDLOG_COMMIT,
        urls = ["https://github.com/gabime/spdlog/archive/%s.tar.gz" % _SPDLOG_COMMIT],
    )

    http_archive(
        name = "doctest",
        sha256 = "88a552f832ef3e4e7b733f9ab4eff5d73d7c37e75bebfef4a3339bf52713350d",
        strip_prefix = "doctest-%s" % _DOCTEST_VERSION,
        urls = ["https://github.com/doctest/doctest/archive/v%s.zip" % _DOCTEST_VERSION],
    )

    http_archive(
        name = "dtl",
        sha256 = "579f81bca88f4b9760a59d99c5a95bd8dd5dc2f20f33f1f9b5f64cb08979f54d",
        build_file = "@scip_clang//third_party:dtl.BUILD",
        strip_prefix = "dtl-%s" % _DTL_VERSION,
        urls = ["https://github.com/cubicdaiya/dtl/archive/v%s.tar.gz" % _DTL_VERSION],
    )

    http_file(
        name = "buildifier_darwin_arm64",
        executable = True,
        sha256 = "21fa0d48ef0b7251eb6e3521cbe25d1e52404763cd2a43aa29f69b5380559dd1",
        urls = ["https://github.com/bazelbuild/buildtools/releases/download/6.0.0/buildifier-darwin-arm64"],
    )

    http_file(
        name = "buildifier_linux_amd64",
        executable = True,
        sha256 = "0b51a6cb81bc3b51466ea2210053992654987a907063d0c2b9c03be29de52eff",
        urls = ["https://github.com/bazelbuild/buildtools/releases/download/6.1.0/buildifier-linux-amd64"],
    )

    http_file(
        name = "buildifier_linux_arm64",
        executable = True,
        sha256 = "5acdd65684105f73d1c65ee4737f6cf388afff8674eb88045aa3c204811b02f3",
        urls = ["https://github.com/bazelbuild/buildtools/releases/download/6.1.0/buildifier-linux-arm64"],
    )

    http_archive(
        name = "actionlint_darwin_arm64",
        build_file = "@scip_clang//third_party:actionlint.BUILD",
        sha256 = "5477f8a5a4073ef086525a2512b2bf1201641cd544034ad0c66f329590638242",
        urls = ["https://github.com/rhysd/actionlint/releases/download/v1.6.24/actionlint_1.6.24_darwin_arm64.tar.gz"],
    )

    http_archive(
        name = "actionlint_linux_amd64",
        build_file = "@scip_clang//third_party:actionlint.BUILD",
        sha256 = "3c5818744143a5d6754edd3dcc4c2b32c9dfcdd3bb30e0e108fb5e5c505262d4",
        urls = ["https://github.com/rhysd/actionlint/releases/download/v1.6.24/actionlint_1.6.24_linux_amd64.tar.gz"],
    )

    http_archive(
        name = "actionlint_linux_arm64",
        build_file = "@scip_clang//third_party:actionlint.BUILD",
        sha256 = "93cc9d1f4a01f0658423b41ecf3bd8c17c619003ec683be8bac9264d0361d0d8",
        urls = ["https://github.com/rhysd/actionlint/releases/download/v1.6.24/actionlint_1.6.24_linux_arm64.tar.gz"],
    )

    http_archive(
        name = "rules_python",
        sha256 = "29a801171f7ca190c543406f9894abf2d483c206e14d6acbd695623662320097",
        strip_prefix = "rules_python-%s" % _RULES_PYTHON_VERSION,
        url = "https://github.com/bazelbuild/rules_python/releases/download/{0}/rules_python-{0}.tar.gz".format(_RULES_PYTHON_VERSION),
    )
