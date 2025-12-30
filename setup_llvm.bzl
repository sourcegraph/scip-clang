load("@toolchains_llvm//toolchain:rules.bzl", grailbio_llvm_toolchain = "llvm_toolchain")

def setup_llvm_toolchain(name):
    # NOTE: The ASan build uses paths which involve the version.
    # Keep the version list in sync with settings.bzl
    #
    # LLVM 21 uses new naming convention: LLVM-VERSION-Platform.tar.xz
    # with strip prefix LLVM-VERSION-Platform
    grailbio_llvm_toolchain(
        name = name,
        llvm_versions = {
            "linux-aarch64": "21.1.8",
            "linux-x86_64": "21.1.8",
            "darwin-aarch64": "21.1.8",
            "darwin-arm64": "21.1.8",
            "darwin-x86_64": "21.1.8",
            "windows-x86_64": "21.1.8",
        },
        sha256 = {
            "linux-aarch64": "65ce0b329514e5643407db2d02a5bd34bf33d159055dafa82825c8385bd01993",
            "linux-x86_64": "b3b7f2801d15d50736acea3c73982994d025b01c2f035b91ae3b49d1b575732b",
            "darwin-aarch64": "b95bdd32a33a81ee4d40363aaeb26728a26783fcef26a4d80f65457433ea4669",
            "darwin-arm64": "b95bdd32a33a81ee4d40363aaeb26728a26783fcef26a4d80f65457433ea4669",
            "darwin-x86_64": "b95bdd32a33a81ee4d40363aaeb26728a26783fcef26a4d80f65457433ea4669",  # Use ARM64 via Rosetta
            "windows-x86_64": "749d22f565fcd5718dbed06512572d0e5353b502c03fe1f7f17ee8b8aca21a47",
        },
        strip_prefix = {
            "linux-aarch64": "LLVM-21.1.8-Linux-ARM64",
            "linux-x86_64": "LLVM-21.1.8-Linux-X64",
            "darwin-aarch64": "LLVM-21.1.8-macOS-ARM64",
            "darwin-arm64": "LLVM-21.1.8-macOS-ARM64",
            "darwin-x86_64": "LLVM-21.1.8-macOS-ARM64",  # Use ARM64 via Rosetta
            "windows-x86_64": "clang+llvm-21.1.8-x86_64-pc-windows-msvc",
        },
        urls = {
            "linux-aarch64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-Linux-ARM64.tar.xz"],
            "linux-x86_64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-Linux-X64.tar.xz"],
            "darwin-aarch64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-macOS-ARM64.tar.xz"],
            "darwin-arm64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-macOS-ARM64.tar.xz"],
            "darwin-x86_64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/LLVM-21.1.8-macOS-ARM64.tar.xz"],  # Use ARM64 via Rosetta
            "windows-x86_64": ["https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/clang+llvm-21.1.8-x86_64-pc-windows-msvc.tar.xz"],
        },
    )
