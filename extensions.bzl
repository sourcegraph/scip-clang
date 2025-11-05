"""Module extensions for scip-clang custom dependencies."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

_LLVM_COMMIT = "e0f3110b854a476c16cce7b44472cd7838d344e9"  # Keep in sync with Version.h
_CXXOPTS_VERSION = "3.0.0"
_RAPIDJSON_COMMIT = "a98e99992bd633a2736cc41f96ec85ef0c50e44d"
_WYHASH_COMMIT = "ea3b25e1aef55d90f707c3a292eeb9162e2615d8"
_SCIP_COMMIT = "aa0e511dcfefbacc3b96dcc2fe2abd9894416b1e"
_UTFCPP_VERSION = "4.0.5"
# ^ When bumping this version, check if any new fields are introduced
# in the types for which we implement hashing and comparison in
# indexer/ScipExtras.{h,cc}

# When bumping the Perfetto version, tweak the patch to also
# remove the use of 'PERFETTO_NO_SANITIZE_UNDEFINED'
# See https://github.com/google/perfetto/issues/271#issuecomment-1527691232
_PERFETTO_VERSION = "33.1"  # Keep in sync with docs/Development.md
_DTL_VERSION = "1.20"



def _scip_deps_impl(mctx):
    """Implementation of scip_deps module extension."""
    
    # Bazel compilation database
    http_archive(
        name = "com_grail_bazel_compdb",
        sha256 = "d32835b26dd35aad8fd0ba0d712265df6565a3ad860d39e4c01ad41059ea7eda",
        strip_prefix = "bazel-compilation-database-0.5.2",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/0.5.2.tar.gz"],
    )
    
    # cxxopts
    http_archive(
        name = "cxxopts",
        sha256 = "1eefdf5af3ba0c66458258de05df2a113262ad5e85cac489de0a456088e9f9b0",
        build_file = "@scip_clang//third_party:cxxopts.BUILD",
        strip_prefix = "cxxopts-%s" % _CXXOPTS_VERSION,
        urls = ["https://github.com/jarro2783/cxxopts/archive/v%s.zip" % _CXXOPTS_VERSION],
    )

    # rapidjson
    http_archive(
        name = "rapidjson",
        sha256 = "c79acb593f1954b2b217feb170549cb58fe4b9edac48e1d4e7285e03235c54d2",
        build_file = "@scip_clang//third_party:rapidjson.BUILD",
        strip_prefix = "rapidjson-%s" % _RAPIDJSON_COMMIT,
        urls = ["https://github.com/Tencent/rapidjson/archive/%s.zip" % _RAPIDJSON_COMMIT],
    )

    # wyhash
    http_archive(
        name = "wyhash",
        sha256 = "ac8ff5dee1f6861614bbb1f2f5a0d57027574cc5fb56e2c47ac69ea2de30bbb0",
        build_file = "@scip_clang//third_party:wyhash.BUILD",
        strip_prefix = "wyhash-%s" % _WYHASH_COMMIT,
        urls = ["https://github.com/wangyi-fudan/wyhash/archive/%s.zip" % _WYHASH_COMMIT],
    )

    # Perfetto
    http_archive(
        name = "com_google_perfetto",
        sha256 = "09b3271d3829a13b400447353d442a65f8f88e2df5a26f96778ab66c4cd26ec1",
        strip_prefix = "perfetto-%s/sdk" % _PERFETTO_VERSION,
        build_file = "@scip_clang//third_party:perfetto.BUILD",
        urls = ["https://github.com/google/perfetto/archive/v%s.tar.gz" % _PERFETTO_VERSION],
        patch_args = ["-p1"],
        patches = ["@scip_clang//third_party:perfetto.patch"],
    )

    # DTL
    http_archive(
        name = "dtl",
        sha256 = "579f81bca88f4b9760a59d99c5a95bd8dd5dc2f20f33f1f9b5f64cb08979f54d",
        build_file = "@scip_clang//third_party:dtl.BUILD",
        strip_prefix = "dtl-%s" % _DTL_VERSION,
        urls = ["https://github.com/cubicdaiya/dtl/archive/v%s.tar.gz" % _DTL_VERSION],
    )

    # SCIP
    http_archive(
        name = "scip",
        sha256 = "b1d2fc009345857aa32cdddec11b75ce1e5c20430f668044231ed309d48b7355",
        build_file = "@scip_clang//third_party:scip.BUILD",
        strip_prefix = "scip-%s" % _SCIP_COMMIT,
        urls = ["https://github.com/sourcegraph/scip/archive/%s.zip" % _SCIP_COMMIT],
    )

    # utfcpp
    http_archive(
        name = "utfcpp",
        sha256 = "ffc668a310e77607d393f3c18b32715f223da1eac4c4d6e0579a11df8e6b59cf",
        build_file = "@scip_clang//third_party:utfcpp.BUILD",
        strip_prefix = "utfcpp-%s" % _UTFCPP_VERSION,
        url = "https://github.com/nemtrif/utfcpp/archive/refs/tags/v{0}.tar.gz".format(_UTFCPP_VERSION),
    )

    # llvm_zstd
    http_archive(
        name = "llvm_zstd",
        build_file = "@llvm-raw//utils/bazel/third_party_build:zstd.BUILD",
        sha256 = "7c42d56fac126929a6a85dbc73ff1db2411d04f104fae9bdea51305663a83fd0",
        strip_prefix = "zstd-1.5.2",
        urls = [
            "https://github.com/facebook/zstd/releases/download/v1.5.2/zstd-1.5.2.tar.gz",
        ],
    )

    # Buildifier tools
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

    # Actionlint tools
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

def _llvm_project_impl(mctx):
    """Implementation of llvm_project module extension."""
    
    # LLVM raw source
    http_archive(
        name = "llvm-raw",
        sha256 = "04b76a5be88331f71a4e4fe96bccfebec302ddd0dbd9418fd5c186a7361c54fb",
        strip_prefix = "llvm-project-%s" % _LLVM_COMMIT,
        build_file_content = "# empty",
        urls = ["https://github.com/llvm/llvm-project/archive/%s.tar.gz" % _LLVM_COMMIT],
    )
    
    # Note: llvm_terminfo and llvm-project configuration are handled in WORKSPACE
    # because they need to load from @llvm-raw which can't be done in module extensions

# Module extension declarations
scip_deps = module_extension(
    implementation = _scip_deps_impl,
)

llvm_project = module_extension(
    implementation = _llvm_project_impl,
)
