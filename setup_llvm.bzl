load("@toolchains_llvm//toolchain:rules.bzl", grailbio_llvm_toolchain = "llvm_toolchain")

# macOS SDK path for system libc++ headers (should match `xcrun --show-sdk-path`).
# LLVM 21's bundled libc++ headers reference std::__1::__hash_memory, which doesn't
# exist in Apple's system libc++. We use this path with -nostdinc++ and -isystem below
# to force the use of Apple's SDK headers instead of LLVM's bundled ones.
_MACOS_SDK = "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"

def setup_llvm_toolchain(name):
    # NOTE: The ASan build uses paths which involve the version.
    # Keep the version list in sync with settings.bzl
    #
    # LLVM 21 uses new naming convention: LLVM-VERSION-Platform.tar.xz
    # with strip prefix LLVM-VERSION-Platform
    #
    # On macOS, we use stdlib = "libc++" to use the system's libc++ instead of
    # the bundled LLVM libc++. This avoids the __hash_memory ABI mismatch issue
    # where LLVM 21's libc++ headers reference symbols that Apple's system libc++
    # doesn't have.
    #
    # IMPORTANT: stdlib="libc++" only affects LINKING, not header search paths!
    # By default, the toolchain still uses LLVM's bundled libc++ headers from
    # {toolchain_path_prefix}/include/c++/v1, which causes the ABI mismatch.
    #
    # To fix this, we use cxx_flags to:
    # 1. Add -nostdinc++ to disable the default C++ stdlib header search
    # 2. Use -isystem to add macOS SDK headers explicitly
    #
    # We also set sysroot to the macOS SDK so the toolchain knows where to find
    # system headers and frameworks.
    grailbio_llvm_toolchain(
        name = name,
        # Use system libc++ on macOS to avoid ABI mismatch with LLVM 21's bundled headers.
        # On Linux, use the bundled libc++ (builtin-libc++) for hermetic builds.
        stdlib = {
            "darwin-aarch64": "libc++",
            "": "builtin-libc++",  # Default for all other platforms
        },
        # Point to macOS SDK for system headers and frameworks
        sysroot = {
            "darwin-aarch64": _MACOS_SDK,
        },
        # Add extra cxx_flags to use macOS SDK libc++ headers instead of LLVM's bundled ones.
        # -nostdinc++ disables the default C++ header search paths (including LLVM's bundled headers)
        # -isystem adds the macOS SDK's libc++ headers as a system include path
        # NOTE: We use extra_cxx_flags (not cxx_flags) to ADD to defaults rather than replace them.
        extra_cxx_flags = {
            "darwin-aarch64": [
                "-nostdinc++",
                "-isystem",
                _MACOS_SDK + "/usr/include/c++/v1",
            ],
        },
        # Override link_libs for darwin platforms to use dynamic libc++ instead
        # of the default static -l:libc++.a which doesn't exist on macOS.
        # We explicitly link -lc++ and -lc++abi dynamically.
        link_libs = {
            "darwin-aarch64": ["-lc++", "-lc++abi"],
        },
        llvm_versions = {
            "linux-aarch64": "21.1.8",
            "linux-x86_64": "21.1.8",
            "darwin-aarch64": "21.1.8",
        },
        sha256 = {
            "linux-aarch64": "65ce0b329514e5643407db2d02a5bd34bf33d159055dafa82825c8385bd01993",
            "linux-x86_64": "b3b7f2801d15d50736acea3c73982994d025b01c2f035b91ae3b49d1b575732b",
            "darwin-aarch64": "b95bdd32a33a81ee4d40363aaeb26728a26783fcef26a4d80f65457433ea4669",
        },
        strip_prefix = {
            "linux-aarch64": "LLVM-21.1.8-Linux-ARM64",
            "linux-x86_64": "LLVM-21.1.8-Linux-X64",
            "darwin-aarch64": "LLVM-21.1.8-macOS-ARM64",
        },
        urls = {
            "linux-aarch64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-Linux-ARM64.tar.xz"],
            "linux-x86_64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-Linux-X64.tar.xz"],
            "darwin-aarch64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-macOS-ARM64.tar.xz"],
        },
    )
