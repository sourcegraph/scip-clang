load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

_BAZEL_SKYLIB_VERSION = "1.3.0"
_PLATFORMS_VERSION = "1.0.0"  # Updated from 2022 commit for visionos support
_BAZEL_TOOLCHAIN_VERSION = "1.6.0"
_RULES_BOOST_COMMIT = "00b9b9ecb9b43564de44ea0b10e22b29dcf84d79"
_LLVM_COMMIT = "e0f3110b854a476c16cce7b44472cd7838d344e9"  # Keep in sync with Version.h
_ABSL_VERSION = "20240722.0"
_CXXOPTS_VERSION = "3.0.0"
_RAPIDJSON_COMMIT = "a98e99992bd633a2736cc41f96ec85ef0c50e44d"
_WYHASH_COMMIT = "ea3b25e1aef55d90f707c3a292eeb9162e2615d8"
_SPDLOG_COMMIT = "486b55554f11c9cccc913e11a87085b2a91f706f"  # v1.16.0
_PROTOBUF_VERSION = "25.3"
_SCIP_COMMIT = "aa0e511dcfefbacc3b96dcc2fe2abd9894416b1e"
_UTFCPP_VERSION = "4.0.5"
# ^ When bumping this version, check if any new fields are introduced
# in the types for which we implement hashing and comparison in
# indexer/ScipExtras.{h,cc}

# When bumping the Perfetto version, tweak the patch to also
# remove the use of 'PERFETTO_NO_SANITIZE_UNDEFINED'
# See https://github.com/google/perfetto/issues/271#issuecomment-1527691232
_PERFETTO_VERSION = "33.1"  # Keep in sync with docs/Development.md
_DOCTEST_VERSION = "2.4.9"
_DTL_VERSION = "1.21"
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
        sha256 = "3384eb1c30762704fbe38e440204e114154086c8fc8a8c2e3e28441028c019a8",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/platforms/releases/download/{0}/platforms-{0}.tar.gz".format(_PLATFORMS_VERSION),
            "https://github.com/bazelbuild/platforms/releases/download/{0}/platforms-{0}.tar.gz".format(_PLATFORMS_VERSION),
        ],
    )

    http_archive(
        name = "toolchains_llvm",
        sha256 = "2b298a1d7ea99679f5edf8af09367363e64cb9fbc46e0b7c1b1ba2b1b1b51058",
        strip_prefix = "toolchains_llvm-v%s" % _BAZEL_TOOLCHAIN_VERSION,
        canonical_id = _BAZEL_TOOLCHAIN_VERSION,
        url = "https://github.com/bazel-contrib/toolchains_llvm/releases/download/v{0}/toolchains_llvm-v{0}.tar.gz".format(_BAZEL_TOOLCHAIN_VERSION),
    )

    http_archive(
        name = "com_github_nelhage_rules_boost",
        sha256 = "a8499f581899ae7356e40e2aab6e985dd2da9c894c91197341aace9a0a6157fe",
        strip_prefix = "rules_boost-%s" % _RULES_BOOST_COMMIT,
        urls = [
            "https://github.com/nelhage/rules_boost/archive/%s.tar.gz" % _RULES_BOOST_COMMIT,
        ],
    )

    _HEDRON_COMMIT = "abb61a688167623088f8768cc9264798df6a9d10"
    http_archive(
        name = "hedron_compile_commands",
        sha256 = "1b08abffbfbe89f6dbee6a5b33753792e8004f6a36f37c0f72115bec86e68724",
        url = "https://github.com/hedronvision/bazel-compile-commands-extractor/archive/%s.tar.gz" % _HEDRON_COMMIT,
        strip_prefix = "bazel-compile-commands-extractor-%s" % _HEDRON_COMMIT,
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
    # Using zlib 1.3.1 to fix macro conflicts with macOS headers in zlib 1.2.11
    http_archive(
        name = "zlib",
        build_file = "@scip_clang//third_party:zlib.BUILD",
        sha256 = "17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c",
        strip_prefix = "zlib-1.3.1",
        urls = [
            "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz",
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
        sha256 = "5156b22536feaa88cf95503153a6b2cd67cc80f20f1218f154b84a12c288a220",
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
        sha256 = "95e90be7c3643e658670e0dd3c1b27092349c34b632c6e795686355f67eca89f",
        strip_prefix = "abseil-cpp-%s" % _ABSL_VERSION,
        urls = ["https://github.com/abseil/abseil-cpp/archive/%s.zip" % _ABSL_VERSION],
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
        sha256 = "d2fef585c9879dd239dc498e2e8a1e22982b3ed67b2d14e78622b7ef25bdfdfa",
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
        sha256 = "90ed2dbf4e6d687737fe25f118bbcb6aed778cecc3f2115d191a032bf8643dbd",
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
        sha256 = "2693315b9093aeacb4ebd91a993fea54fc215057bf0da2659056b4bc033873db",
        urls = ["https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_darwin_arm64.tar.gz"],
    )

    http_archive(
        name = "actionlint_linux_amd64",
        build_file = "@scip_clang//third_party:actionlint.BUILD",
        sha256 = "023070a287cd8cccd71515fedc843f1985bf96c436b7effaecce67290e7e0757",
        urls = ["https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_linux_amd64.tar.gz"],
    )

    http_archive(
        name = "actionlint_linux_arm64",
        build_file = "@scip_clang//third_party:actionlint.BUILD",
        sha256 = "401942f9c24ed71e4fe71b76c7d638f66d8633575c4016efd2977ce7c28317d0",
        urls = ["https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_linux_arm64.tar.gz"],
    )

    http_archive(
        name = "rules_python",
        sha256 = "29a801171f7ca190c543406f9894abf2d483c206e14d6acbd695623662320097",
        strip_prefix = "rules_python-%s" % _RULES_PYTHON_VERSION,
        url = "https://github.com/bazelbuild/rules_python/releases/download/{0}/rules_python-{0}.tar.gz".format(_RULES_PYTHON_VERSION),
    )

    http_archive(
        name = "utfcpp",
        sha256 = "ffc668a310e77607d393f3c18b32715f223da1eac4c4d6e0579a11df8e6b59cf",
        build_file = "@scip_clang//third_party:utfcpp.BUILD",
        strip_prefix = "utfcpp-%s" % _UTFCPP_VERSION,
        url = "https://github.com/nemtrif/utfcpp/archive/refs/tags/v{0}.tar.gz".format(_UTFCPP_VERSION),
    )
