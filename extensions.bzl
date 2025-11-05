"""Module extensions for scip-clang custom dependencies."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file")

_LLVM_COMMIT = "e0f3110b854a476c16cce7b44472cd7838d344e9"  # Keep in sync with Version.h
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
    # NOTE: This dependency cannot be migrated to MODULE.bazel because:
    # 1. bazel-compilation-database is not available in Bazel Central Registry (BCR)
    # 2. The upstream repository (grailbio/bazel-compilation-database) is archived 
    #    and does not support bzlmod
    # This must remain as an http_archive until a BCR-compatible alternative is available
    http_archive(
        name = "com_grail_bazel_compdb",
        sha256 = "d32835b26dd35aad8fd0ba0d712265df6565a3ad860d39e4c01ad41059ea7eda",
        strip_prefix = "bazel-compilation-database-0.5.2",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/0.5.2.tar.gz"],
    )
    

    # wyhash
    # NOTE: This dependency cannot be migrated to MODULE.bazel because:
    # 1. wyhash is not available in Bazel Central Registry (BCR)
    # 2. The upstream repository (wangyi-fudan/wyhash) does not support bzlmod
    # 3. We need a custom BUILD file to define cc_library targets for the header-only library
    # This must remain as an http_archive until wyhash is added to BCR
    http_archive(
        name = "wyhash",
        sha256 = "ac8ff5dee1f6861614bbb1f2f5a0d57027574cc5fb56e2c47ac69ea2de30bbb0",
        build_file = "@scip_clang//third_party:wyhash.BUILD",
        strip_prefix = "wyhash-%s" % _WYHASH_COMMIT,
        urls = ["https://github.com/wangyi-fudan/wyhash/archive/%s.zip" % _WYHASH_COMMIT],
    )

    # Perfetto
    # NOTE: This dependency cannot be migrated to MODULE.bazel because:
    # 1. perfetto is not available in Bazel Central Registry (BCR)
    # 2. We require a custom BUILD file to define the cc_library target
    # 3. We apply custom patches for undefined behavior sanitizer compatibility
    # This must remain as an http_archive until perfetto is added to BCR with proper module support
    http_archive(
        name = "com_google_perfetto",
        sha256 = "09b3271d3829a13b400447353d442a65f8f88e2df5a26f96778ab66c4cd26ec1",
        strip_prefix = "perfetto-%s/sdk" % _PERFETTO_VERSION,
        build_file = "@scip_clang//third_party:perfetto.BUILD",
        urls = ["https://github.com/google/perfetto/archive/v%s.tar.gz" % _PERFETTO_VERSION],
        patch_args = ["-p1"],
        patches = ["@scip_clang//third_party:perfetto.patch"],
    )

    # DTL (diff template library)
    # NOTE: This dependency cannot be migrated to MODULE.bazel because:
    # 1. dtl is not available in Bazel Central Registry (BCR)
    # 2. The upstream repository (cubicdaiya/dtl) does not support bzlmod
    # 3. We need a custom BUILD file to define cc_library targets
    # This must remain as an http_archive until dtl is added to BCR
    http_archive(
        name = "dtl",
        sha256 = "579f81bca88f4b9760a59d99c5a95bd8dd5dc2f20f33f1f9b5f64cb08979f54d",
        build_file = "@scip_clang//third_party:dtl.BUILD",
        strip_prefix = "dtl-%s" % _DTL_VERSION,
        urls = ["https://github.com/cubicdaiya/dtl/archive/v%s.tar.gz" % _DTL_VERSION],
    )

    # SCIP
    # NOTE: This dependency cannot be migrated to MODULE.bazel because:
    # 1. scip is not available in Bazel Central Registry (BCR)
    # 2. The upstream repository (sourcegraph/scip) does not support bzlmod
    # 3. We need a custom BUILD file to define proto targets for the scip schema
    # This must remain as an http_archive until scip is added to BCR with proper module support
    http_archive(
        name = "scip",
        sha256 = "b1d2fc009345857aa32cdddec11b75ce1e5c20430f668044231ed309d48b7355",
        build_file = "@scip_clang//third_party:scip.BUILD",
        strip_prefix = "scip-%s" % _SCIP_COMMIT,
        urls = ["https://github.com/sourcegraph/scip/archive/%s.zip" % _SCIP_COMMIT],
    )

    # utfcpp
    # NOTE: This dependency cannot be migrated to MODULE.bazel because:
    # 1. utfcpp is not available in Bazel Central Registry (BCR)
    # 2. The upstream repository (nemtrif/utfcpp) does not support bzlmod
    # 3. We need a custom BUILD file to define cc_library targets for the header-only library
    # This must remain as an http_archive until utfcpp is added to BCR
    http_archive(
        name = "utfcpp",
        sha256 = "ffc668a310e77607d393f3c18b32715f223da1eac4c4d6e0579a11df8e6b59cf",
        build_file = "@scip_clang//third_party:utfcpp.BUILD",
        strip_prefix = "utfcpp-%s" % _UTFCPP_VERSION,
        url = "https://github.com/nemtrif/utfcpp/archive/refs/tags/v{0}.tar.gz".format(_UTFCPP_VERSION),
    )

    # llvm_zstd 
    # NOTE: This dependency cannot be migrated to MODULE.bazel because:
    # 1. LLVM requires a specific build setting flag @llvm_zstd//:llvm_enable_zstd
    # 2. The BCR version of zstd doesn't provide this target
    # 3. Adding it via patch would require zstd to depend on bazel_skylib, which it doesn't
    # 4. LLVM's custom BUILD file provides the necessary build setting
    # This must remain as an http_archive with LLVM's custom BUILD overlay
    http_archive(
        name = "llvm_zstd",
        build_file = "@llvm-raw//utils/bazel/third_party_build:zstd.BUILD",
        sha256 = "7c42d56fac126929a6a85dbc73ff1db2411d04f104fae9bdea51305663a83fd0",
        strip_prefix = "zstd-1.5.2",
        urls = [
            "https://github.com/facebook/zstd/releases/download/v1.5.2/zstd-1.5.2.tar.gz",
        ],
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
