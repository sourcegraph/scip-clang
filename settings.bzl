# Keep LLVM versions list in sync with setup_llvm.bzl
ASAN_LINKOPTS = [
    "-Wl,-rpath,@loader_path/../../../../../../external/llvm_toolchain_llvm/lib/clang/16.0.0/lib/darwin",
    "-Wl,-rpath,@loader_path/../../../../../../external/llvm_toolchain_llvm/lib/clang/16.0.0/lib/linux",
]
